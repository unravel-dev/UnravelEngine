#include "asset_writer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>  // open
#include <unistd.h> // fsync, close
#endif

#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <logging/logging.h>

namespace unravel
{
namespace asset_writer
{

namespace
{
constexpr const char charset[] = "0123456789"
                                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz";

constexpr size_t max_index = (sizeof(charset) - 1);

using random_generator_t = ::std::mt19937;

static const auto make_seeded_engine = []()
{
    std::random_device r;
    std::hash<std::thread::id> hasher;
    std::seed_seq seed(std::initializer_list<typename random_generator_t::result_type>{
                                                                                        static_cast<typename random_generator_t::result_type>(
                                                                                            std::chrono::system_clock::now().time_since_epoch().count()),
                                                                                        static_cast<typename random_generator_t::result_type>(hasher(std::this_thread::get_id())),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r()});
    return random_generator_t(seed);
};

std::string generate_random_string(size_t len)
{
    static thread_local random_generator_t engine(make_seeded_engine());

    std::uniform_int_distribution<> dist(0, max_index);

    std::string str;
    str.reserve(len);

    for (size_t i = 0; i < len; i++)
    {
        str.push_back(charset[dist(engine)]);
    }

    return str;
}

//------------------------------------------------------------------------------
// Recognise our temp-file pattern: a hidden file named `.<UUID>.temp` (the
// leading dot is what hides it in most file browsers).
//------------------------------------------------------------------------------
auto looks_like_temp_file(const fs::path& p) noexcept -> bool
{
    const auto name = p.filename().string();
    if(name.size() < 2 || name.front() != '.')
    {
        return false;
    }
    return p.extension() == ".temp";
}

// Forward declaration; used by the cleanup function below.
auto remove_temp_with_retry(const fs::path& temp,
                            int max_retries,
                            int base_delay_ms,
                            std::string* last_error_out) noexcept -> bool;

//------------------------------------------------------------------------------
// Single-entry helper used by `cleanup_stale_temp_files`. Lives in the
// anonymous namespace (rather than being a lambda) so that __FUNCTION__ inside
// the APPLOG_* macros expands to a useful name, and to keep the outer function
// below the cognitive-complexity threshold.
//------------------------------------------------------------------------------
void try_remove_stale_temp(const fs::path& p,
                           fs::file_time_type now,
                           std::chrono::seconds min_age,
                           std::size_t& removed_counter) noexcept
{
    if(!looks_like_temp_file(p))
    {
        return;
    }

    fs::error_code stat_ec;
    const auto write_time = fs::last_write_time(p, stat_ec);
    if(stat_ec)
    {
        return;
    }
    if(std::chrono::duration_cast<std::chrono::seconds>(now - write_time) < min_age)
    {
        // Probably a concurrent in-flight write — leave it alone.
        return;
    }

    std::string diagnostic;
    if(remove_temp_with_retry(p, 5, 10, &diagnostic))
    {
        ++removed_counter;
        APPLOG_INFO("asset_writer: Removed stale temp file: {}", p.generic_string());
    }
    else
    {
        APPLOG_WARNING("asset_writer: Could not remove stale temp file: {} ({})",
                       p.generic_string(),
                       diagnostic);
    }
}

} // namespace
#define ATOMIC_SAVE
auto sync_file(const fs::path& temp, fs::error_code& ec) noexcept -> bool
{
#ifdef _WIN32
    // flush via FlushFileBuffers
    {
        HANDLE h = CreateFileW(temp.wstring().c_str(),
                               GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if(h == INVALID_HANDLE_VALUE)
        {
            ec = fs::error_code(static_cast<int>(GetLastError()), std::system_category());
            return false;
        }
        if(!FlushFileBuffers(h))
        {
            ec = fs::error_code(static_cast<int>(GetLastError()), std::system_category());
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);
    }
#else
    int fd = ::open(temp.c_str(), O_RDWR);
    if(fd < 0)
    {
        ec = fs::error_code(errno, std::generic_category());
        return false;
    }
    if(::fsync(fd) < 0)
    {
        ec = fs::error_code(errno, std::generic_category());
        ::close(fd);
        return false;
    }
    ::close(fd);
#endif
    return true;
}

//------------------------------------------------------------------------------
// Atomically rename src -> dst, overwriting dst if it exists.
//------------------------------------------------------------------------------
auto atomic_rename_file(const fs::path& src, const fs::path& dst, fs::error_code& ec) noexcept -> bool
{
    ec.clear();
    fs::rename(src, dst, ec);
    return !ec;
}

namespace
{

//------------------------------------------------------------------------------
// Retry fs::rename a few times with exponential backoff. On Windows, rename can
// fail transiently when AV scanners, indexers, or other processes hold the
// destination open with restrictive sharing modes. Most of those are released
// within a few hundred milliseconds.
//------------------------------------------------------------------------------
auto atomic_rename_with_retry(const fs::path& src,
                              const fs::path& dst,
                              fs::error_code& ec,
                              int max_retries = 5,
                              int base_delay_ms = 10) noexcept -> bool
{
    for(int i = 0; i < max_retries; ++i)
    {
        ec.clear();
        fs::rename(src, dst, ec);
        if(!ec)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(base_delay_ms * (1 << i)));
    }
    return false;
}

#ifdef _WIN32
//------------------------------------------------------------------------------
// Windows-specific POSIX-style delete. Opens the file with shared delete access
// and asks the OS to unlink the name as soon as we close our handle, regardless
// of any other open handles. This succeeds in every case where:
//   - the file exists, AND
//   - at least one other holder of the file opened it with FILE_SHARE_DELETE
// which covers most file watchers, asset preview thumbnailers, indexers, and
// Defender's on-access scanner.
//
// Prefers `FileDispositionInfoEx` with POSIX semantics (Windows 10 RS1+) — this
// unlinks the *name* immediately so the path becomes reusable even if the OS
// keeps the file around until the last handle is closed. Falls back to
// `FileDispositionInfo` on older systems.
//------------------------------------------------------------------------------
auto windows_force_delete(const fs::path& path, DWORD& last_error) noexcept -> bool
{
    last_error = 0;

    HANDLE h = CreateFileW(path.wstring().c_str(),
                           DELETE | SYNCHRONIZE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if(h == INVALID_HANDLE_VALUE)
    {
        last_error = GetLastError();
        return false;
    }

    bool ok = false;

#if defined(FILE_DISPOSITION_FLAG_POSIX_SEMANTICS)
    FILE_DISPOSITION_INFO_EX disp_ex{};
    disp_ex.Flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS;
    ok = (SetFileInformationByHandle(h, FileDispositionInfoEx, &disp_ex, sizeof(disp_ex)) != FALSE);
    if(!ok)
    {
        last_error = GetLastError();
    }
#endif

    if(!ok)
    {
        FILE_DISPOSITION_INFO disp{};
        disp.DeleteFile = TRUE;
        ok = (SetFileInformationByHandle(h, FileDispositionInfo, &disp, sizeof(disp)) != FALSE);
        if(!ok)
        {
            last_error = GetLastError();
        }
    }

    CloseHandle(h);
    return ok;
}
#endif

//------------------------------------------------------------------------------
// Try to remove a temporary file with retries for transient locks (e.g.
// antivirus scanning on Windows). Returns true if the file was removed.
//
// `last_error_out` (optional) receives the most recent platform error so the
// caller can include it in diagnostics when all attempts fail.
//
// Defaults give ~1.6s of total wait time spread across 8 attempts; each sleep
// is capped at 500 ms so we never block the caller on a runaway exponential.
// On Windows, falls back to a POSIX-style delete that succeeds even when other
// processes still hold the file open (as long as they used FILE_SHARE_DELETE).
//------------------------------------------------------------------------------
auto remove_temp_with_retry(const fs::path& temp,
                            int max_retries = 8,
                            int base_delay_ms = 10,
                            std::string* last_error_out = nullptr) noexcept -> bool
{
    fs::error_code remove_ec;
    for(int i = 0; i < max_retries; ++i)
    {
        fs::remove(temp, remove_ec);
        if(!remove_ec || !fs::exists(temp, remove_ec))
        {
            return true;
        }

        // Exponential backoff with a 500 ms cap per sleep.
        const int shift = std::min(i, 16); // guard against UB on 1<<i for very large i
        const int sleep_ms = std::min(base_delay_ms * (1 << shift), 500);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    // Last-resort Windows fallback: unlink via a shared-delete handle so we don't
    // need the file to be lock-free at this exact moment. Other platforms have
    // already been served by `fs::remove` above (which uses unlink and doesn't
    // suffer from Windows-style sharing-violation failures).
#ifdef _WIN32
    DWORD win_err = 0;
    if(windows_force_delete(temp, win_err))
    {
        return true;
    }
    if(last_error_out != nullptr)
    {
        *last_error_out = "fs::remove=" + remove_ec.message() +
                          " (code " + std::to_string(remove_ec.value()) + "); " +
                          "windows_force_delete=" + std::to_string(win_err);
    }
    return false;
#else
    if(last_error_out != nullptr)
    {
        *last_error_out =
            "fs::remove=" + remove_ec.message() + " (code " + std::to_string(remove_ec.value()) + ")";
    }
    return false;
#endif
}

//------------------------------------------------------------------------------
// Process-global deferred-cleanup queue.
//
// When a temp file can't be removed immediately (typically because an external
// process — file watcher, AV scanner, indexer, asset preview — is briefly
// holding it open), we add it to this queue. The queue is drained at the start
// of every subsequent atomic write/copy, by which point the external holder has
// almost always released the file. This avoids leaking temp files to the next
// process startup while keeping individual write paths fast.
//------------------------------------------------------------------------------
auto deferred_cleanup_mutex() -> std::mutex&
{
    static std::mutex m;
    return m;
}

auto deferred_cleanup_paths() -> std::vector<fs::path>&
{
    static std::vector<fs::path> v;
    return v;
}

void enqueue_deferred_cleanup(fs::path p) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lk(deferred_cleanup_mutex());
        deferred_cleanup_paths().push_back(std::move(p));
    }
    catch(...) // NOLINT(bugprone-empty-catch): noexcept context, can't propagate; cleanup_stale_temp_files at startup is the safety net
    {
        // Allocation/lock failure: drop the path. It'll be picked up by
        // cleanup_stale_temp_files on next startup.
    }
}

void drain_deferred_cleanup() noexcept
{
    std::vector<fs::path> snapshot;
    try
    {
        std::lock_guard<std::mutex> lk(deferred_cleanup_mutex());
        snapshot.swap(deferred_cleanup_paths());
    }
    catch(...)
    {
        return;
    }

    if(snapshot.empty())
    {
        return;
    }

    std::vector<fs::path> still_pending;
    still_pending.reserve(snapshot.size());

    for(auto& p : snapshot)
    {
        // Cheaper retry budget here: we've already given the file ~1.6 s when
        // the original write tried to delete it. Subsequent attempts just need
        // to catch the moment the holder lets go.
        fs::error_code probe_ec;
        if(!fs::exists(p, probe_ec))
        {
            // Already gone (maybe the watcher cleaned it up). Drop silently.
            continue;
        }

        if(remove_temp_with_retry(p, 3, 5))
        {
            APPLOG_TRACE("asset_writer: Deferred cleanup removed temp file: {}", p.generic_string());
        }
        else
        {
            still_pending.push_back(std::move(p));
        }
    }

    if(!still_pending.empty())
    {
        try
        {
            std::lock_guard<std::mutex> lk(deferred_cleanup_mutex());
            auto& dst = deferred_cleanup_paths();
            dst.insert(dst.end(),
                       std::make_move_iterator(still_pending.begin()),
                       std::make_move_iterator(still_pending.end()));
        }
        catch(...) // NOLINT(bugprone-empty-catch): noexcept context; orphans get cleaned at next startup
        {
            // If we can't re-enqueue, the files become orphans for next startup.
        }
    }
}

//------------------------------------------------------------------------------
// RAII guard for a temp file. Removes the file on destruction unless commit()
// has been called. This makes cleanup automatic on every exit path including
// exceptions thrown from the callback in atomic_write_file (we are noexcept,
// so an unwinding exception would terminate — but the destructor still runs).
//
// Always logs at WARNING when the file existed but couldn't be removed; that's
// the case the user was hitting (silent leak when AV holds the file briefly).
//------------------------------------------------------------------------------
class temp_file_guard
{
public:
    explicit temp_file_guard(fs::path path) noexcept : path_(std::move(path))
    {
    }

    ~temp_file_guard() noexcept
    {
        if(committed_ || path_.empty())
        {
            return;
        }
        fs::error_code probe_ec;
        if(!fs::exists(path_, probe_ec))
        {
            // Never created, or already gone — nothing to do.
            return;
        }

        std::string diagnostic;
        if(remove_temp_with_retry(path_, 8, 10, &diagnostic))
        {
            return;
        }

        // External holder still has the file open. Queue it for retry on the
        // next atomic write rather than warning the user about a leak that
        // we'll almost certainly recover from in milliseconds.
        APPLOG_TRACE(
            "asset_writer: Temp file still locked after immediate retries; queued for deferred cleanup: {} ({})",
            path_.generic_string(),
            diagnostic);
        enqueue_deferred_cleanup(path_);
    }

    temp_file_guard(const temp_file_guard&) = delete;
    auto operator=(const temp_file_guard&) -> temp_file_guard& = delete;
    temp_file_guard(temp_file_guard&&) = delete;
    auto operator=(temp_file_guard&&) -> temp_file_guard& = delete;

    void commit() noexcept
    {
        committed_ = true;
    }

private:
    fs::path path_;
    bool committed_{false};
};

} // namespace

//------------------------------------------------------------------------------
// Generate a unique temp‑path in `dir`.
// Bounded retries to avoid a runaway loop if fs::exists is permanently failing.
// Returns false (with ec set) on error so callers can detect failure.
//------------------------------------------------------------------------------
auto make_temp_path(const fs::path& dir, fs::path& out, fs::error_code& ec) noexcept -> bool
{
    ec.clear();
    if(!fs::exists(dir, ec) || ec)
    {
        return false;
    }
    if(!fs::is_directory(dir, ec) || ec)
    {
        return false;
    }

    constexpr int max_attempts = 100;
    for(int attempt = 0; attempt < max_attempts; ++attempt)
    {
        out = dir / ("." + hpp::to_string(generate_uuid()) + ".temp");
        fs::error_code exists_ec;
        const bool exists = fs::exists(out, exists_ec);
        if(exists_ec)
        {
            // Can't probe the path — bail rather than silently looping.
            ec = exists_ec;
            return false;
        }
        if(!exists)
        {
            out.make_preferred();
            return true;
        }
    }

    ec = std::make_error_code(std::errc::file_exists);
    return false;
}

//------------------------------------------------------------------------------
// Atomically copy src -> dst via:
//   1) copy_file(src, temp)
//   2) flush temp to disk
//   3) atomic rename(temp, dst) — retried, since transient locks on Windows
//      (AV, indexers, file watchers) often fail the first call
//
// The RAII guard removes the temp file on any early return.
//------------------------------------------------------------------------------
auto atomic_copy_file(const fs::path& src, const fs::path& dst, fs::error_code& ec) noexcept -> bool
{
    ec.clear();

    // Opportunistic: any previously-locked temp files from this process have
    // probably been released by now. Try to clean them up before we start a
    // new write so the directory doesn't accumulate orphans.
    drain_deferred_cleanup();

    if(!fs::exists(src, ec) || ec)
    {
        if(!ec)
        {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
        }
        return false;
    }
    if(!fs::is_regular_file(src, ec) || ec)
    {
        if(!ec)
        {
            ec = std::make_error_code(std::errc::invalid_argument);
        }
        return false;
    }

    fs::path temp;
    if(!make_temp_path(dst.parent_path(), temp, ec))
    {
        return false;
    }

    temp_file_guard guard(temp);

    fs::copy_file(src, temp, fs::copy_options::overwrite_existing, ec);
    if(ec)
    {
        return false;
    }

    if(!sync_file(temp, ec))
    {
        return false;
    }

    if(!atomic_rename_with_retry(temp, dst, ec))
    {
        return false;
    }

    guard.commit();
    return true;
}

void atomic_write_file(const fs::path& dst,
                       const std::function<void(const fs::path&)>& callback,
                       fs::error_code& ec) noexcept
{
    ec.clear();

    // Same opportunistic drain as in atomic_copy_file.
    drain_deferred_cleanup();

    fs::path temp;
    if(!make_temp_path(dst.parent_path(), temp, ec))
    {
        return;
    }

    temp_file_guard guard(temp);

    callback(temp);

    if(!fs::exists(temp, ec) || ec)
    {
        if(!ec)
        {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
        }
        return;
    }

    if(!sync_file(temp, ec))
    {
        return;
    }

    if(!atomic_rename_with_retry(temp, dst, ec))
    {
        return;
    }

    guard.commit();
}

//------------------------------------------------------------------------------
// Scan `dir` for orphaned `.<UUID>.temp` files older than `min_age` and remove
// them. Skips files that look newer than `min_age` so we don't race with a
// concurrent atomic write in another process.
//------------------------------------------------------------------------------
auto cleanup_stale_temp_files(const fs::path& dir,
                              bool recursive,
                              std::chrono::seconds min_age) noexcept -> std::size_t
{
    // Give the in-process deferred queue a final chance — files still queued
    // from earlier writes in this session are usually unlocked by now and we'd
    // rather not leave them as "stale" leftovers for the next startup.
    drain_deferred_cleanup();

    fs::error_code ec;
    if(!fs::exists(dir, ec) || ec)
    {
        return 0;
    }
    if(!fs::is_directory(dir, ec) || ec)
    {
        return 0;
    }

    const auto now = fs::file_time_type::clock::now();
    std::size_t removed = 0;

    auto walk = [&](const auto& begin, const auto& end) -> void
    {
        for(auto it = begin; it != end; ++it)
        {
            const auto& entry = *it;
            fs::error_code is_file_ec;
            if(entry.is_regular_file(is_file_ec) && !is_file_ec)
            {
                try_remove_stale_temp(entry.path(), now, min_age, removed);
            }
        }
    };

    fs::error_code iter_ec;
    if(recursive)
    {
        walk(fs::recursive_directory_iterator(dir, iter_ec), fs::recursive_directory_iterator{});
    }
    else
    {
        walk(fs::directory_iterator(dir, iter_ec), fs::directory_iterator{});
    }

    return removed;
}

} // namespace asset_writer
} // namespace unravel
