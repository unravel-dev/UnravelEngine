/*
 * Validation suite for the filesystem watcher (fs::watcher).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "fs watcher"
 *   <build-dir>/bin/unravel-tests --suite "fs watcher" --bench    (adds latency and fan-out timings)
 *
 * The watcher reports created / modified / removed / renamed by diffing full directory scans. A rename is a
 * missing entry paired with a created one of the same size and extension chain whose modification time is not
 * newer, so both halves have to be visible to the diff. These pin:
 *   - every rename shape the editor and external tools produce - same folder, across folders, a folder with its
 *     children - comes out as one renamed entry per path and nothing else;
 *   - non-atomic moves (copy then delete, delete then move in) pair up for gaps shorter than the hold (half the
 *     poll interval);
 *   - a safe save (delete, then rename a temp file over the name) is a modification, never removed + created;
 *   - a plain copy is still reported as created;
 *   - pause buffers and resume delivers every change once;
 *   - unwatch returns only after a running callback finished, and works from inside the callback;
 *   - per-format filters on one shared listener each receive only their files;
 *   - a watch of a subfolder served by its parent's listener only sees its subtree;
 *   - a recursive watch is never served by a non-recursive listener at the same path.
 *
 * Environment: UNRAVEL_WATCH_TEST_DIR overrides the scratch folder. Every run writes into a fresh subfolder and
 * nothing is deleted.
 */

#include "../tests.h"

#include <filesystem/filesystem.h>
#include <filesystem/pattern_filter.h>
#include <filesystem/watcher.h>
#include <hpp/optional.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace unravel;

namespace
{
using namespace std::literals;
using clock_type = std::chrono::steady_clock;
using entry_status = fs::watcher::entry_status;

/// The interval every editor watch uses.
constexpr auto POLL_INTERVAL = 500ms;
constexpr auto EVENT_TIMEOUT = 4s;
/// Long enough for a held removal to come out and for a stray event to show up after the expected ones.
constexpr auto QUIET_CHECK = 1500ms;
/// Gaps between the two halves of a non-atomic move, all shorter than the hold (half the poll interval).
constexpr std::chrono::milliseconds MOVE_GAPS[] = {0ms, 50ms, 100ms, 150ms};
/// A two-step scenario whose steps ended up further apart than this is past the hold and not checked.
constexpr auto MAX_PAIRED_GAP = POLL_INTERVAL / 2 - 30ms;
/// A filesystem step is retried while another process (an antivirus scan of a file just written) holds
/// the file, the way asset_writer retries its renames.
constexpr int STEP_ATTEMPTS = 10;
constexpr auto STEP_RETRY_DELAY = 10ms;
constexpr std::chrono::milliseconds SAFE_SAVE_GAPS[] = {0ms, 200ms};
constexpr auto CALLBACK_BLOCK = 400ms;
constexpr int PAUSED_FILE_COUNT = 20;
constexpr int LATENCY_TRIALS = 16;
constexpr int LATENCY_DIRS = 10;
constexpr int LATENCY_FILES_PER_DIR = 30;
constexpr int LATENCY_MAX_PHASE_MS = 700;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto to_ms(clock_type::duration duration) -> double
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

auto same_path(const fs::path& lhs, const fs::path& rhs) -> bool
{
    return lhs.lexically_normal() == rhs.lexically_normal();
}

struct recorded_entry
{
    fs::watcher::entry entry;
    clock_type::time_point time;
};

using recorded_entries = std::vector<recorded_entry>;

/// Collects every live (non-initial) entry one watch delivers.
class recorder
{
public:
    auto make_callback() -> fs::watcher::notify_callback
    {
        return [this](const std::vector<fs::watcher::entry>& entries, bool is_initial_list)
        {
            if(is_initial_list)
            {
                return;
            }
            const auto now = clock_type::now();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for(const auto& entry : entries)
                {
                    entries_.push_back({entry, now});
                }
                ++callbacks_;
            }
            changed_.notify_all();
        };
    }

    /// Position to look for entries from.
    auto mark() -> std::size_t
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    auto wait_for(std::size_t from, const std::function<bool(const recorded_entry&)>& predicate) -> bool
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock,
                                 EVENT_TIMEOUT,
                                 [&]
                                 {
                                     return std::any_of(entries_.begin() + std::ptrdiff_t(from), entries_.end(), predicate);
                                 });
    }

    auto entries_since(std::size_t from) -> recorded_entries
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {entries_.begin() + std::ptrdiff_t(from), entries_.end()};
    }

    auto callback_count() -> int
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return callbacks_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    recorded_entries entries_;
    int callbacks_ = 0;
};

auto is_status(const fs::path& path, entry_status status) -> std::function<bool(const recorded_entry&)>
{
    return [path, status](const recorded_entry& recorded)
    {
        return recorded.entry.status == status && same_path(recorded.entry.path, path);
    };
}

auto is_rename(const fs::path& from, const fs::path& to) -> std::function<bool(const recorded_entry&)>
{
    return [from, to](const recorded_entry& recorded)
    {
        return recorded.entry.status == entry_status::renamed && same_path(recorded.entry.path, to) &&
               same_path(recorded.entry.last_path, from);
    };
}

auto count_matching(const recorded_entries& entries, const std::function<bool(const recorded_entry&)>& predicate)
    -> std::size_t
{
    return std::size_t(std::count_if(entries.begin(), entries.end(), predicate));
}

/// Entries that mention the path, as the current or the previous name.
auto count_touching(const recorded_entries& entries, const fs::path& path) -> std::size_t
{
    return count_matching(entries,
                          [&path](const recorded_entry& recorded)
                          {
                              return same_path(recorded.entry.path, path) ||
                                     (recorded.entry.status == entry_status::renamed &&
                                      same_path(recorded.entry.last_path, path));
                          });
}

/// Prints what was delivered, for a failed check.
void dump_entries(const recorded_entries& entries)
{
    for(const auto& recorded : entries)
    {
        std::printf("    %s\n", fs::to_string(recorded.entry).c_str());
    }
}

void write_file(const fs::path& path, const std::string& content)
{
    fs::error_code err;
    fs::create_directories(path.parent_path(), err);
    std::ofstream stream(path, std::ios::trunc);
    stream << content;
}

void append_file(const fs::path& path, const std::string& content)
{
    std::ofstream stream(path, std::ios::app);
    stream << content;
}

auto get_run_root() -> const fs::path&
{
    static const fs::path root = []
    {
        const char* configured = std::getenv("UNRAVEL_WATCH_TEST_DIR");
        const fs::path base = configured ? fs::path(configured) : fs::temp_directory_path() / "unravel_fs_watcher_tests";
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        fs::error_code err;
        return fs::absolute(base / ("run_" + std::to_string(stamp)), err).lexically_normal();
    }();
    return root;
}

/// A watched folder and a staging folder on the same volume outside it.
struct test_tree
{
    fs::path root;
    fs::path staging;
};

auto make_tree(const std::string& name) -> test_tree
{
    test_tree tree;
    tree.root = get_run_root() / name / "watched";
    tree.staging = get_run_root() / name / "staging";
    fs::error_code err;
    fs::create_directories(tree.root, err);
    fs::create_directories(tree.staging, err);
    return tree;
}

auto watch_tree(const fs::path& root,
                recorder& rec,
                const std::string& filter = "*",
                bool recursive = true) -> std::uint64_t
{
    return fs::watcher::watch(root, fs::pattern_filter(filter), recursive, true, POLL_INTERVAL, rec.make_callback(), "fs watcher test");
}

using fs_step = std::function<void(fs::error_code&)>;

auto run_step(const fs_step& step) -> fs::error_code
{
    fs::error_code err;
    for(int attempt = 0; attempt < STEP_ATTEMPTS; ++attempt)
    {
        err.clear();
        step(err);
        if(!err)
        {
            break;
        }
        std::this_thread::sleep_for(STEP_RETRY_DELAY);
    }
    return err;
}

/// Two filesystem steps with a gap between them, timed as they really ran.
struct two_steps
{
    fs::error_code error;
    clock_type::duration gap{};
};

auto run_two_steps(const fs_step& first, std::chrono::milliseconds gap, const fs_step& second) -> two_steps
{
    two_steps result;
    result.error = run_step(first);
    const auto first_done = clock_type::now();
    std::this_thread::sleep_for(gap);
    if(!result.error)
    {
        result.error = run_step(second);
    }
    result.gap = clock_type::now() - first_done;
    return result;
}

/// Whether a scenario ran as intended: a failed step is a failure of its own, a gap past the hold is skipped.
auto is_checkable(const two_steps& steps, const std::string& what) -> bool
{
    if(steps.error)
    {
        check(false, what + ": a filesystem step failed: " + steps.error.message());
        return false;
    }
    if(steps.gap > MAX_PAIRED_GAP)
    {
        std::printf("  note: %s took %.0f ms between its steps, past the hold; not checked\n", what.c_str(), to_ms(steps.gap));
        return false;
    }
    return true;
}

/// Copies a file into the staging folder with its modification time, the way an OS copy keeps it.
auto stage_copy(const fs::path& source, const fs::path& staged) -> void
{
    fs::error_code err;
    fs::copy_file(source, staged, fs::copy_options::overwrite_existing, err);
    const auto source_time = fs::last_write_time(source, err);
    if(fs::last_write_time(staged, err) != source_time)
    {
        fs::last_write_time(staged, source_time, err);
    }
}

void test_lifecycle()
{
    std::printf("test_lifecycle\n");
    const auto tree = make_tree("lifecycle");
    const fs::path file = tree.root / "a.txt";
    const fs::path folder = tree.root / "folder";
    write_file(folder / "inner.txt", "inner");
    write_file(folder / "deeper" / "leaf.txt", "leaf");
    recorder rec;
    const auto id = watch_tree(tree.root, rec);

    auto mark = rec.mark();
    write_file(file, "one");
    check(rec.wait_for(mark, is_status(file, entry_status::created)), "a new file is reported as created");
    mark = rec.mark();
    append_file(file, "two");
    check(rec.wait_for(mark, is_status(file, entry_status::modified)), "a write is reported as modified");
    mark = rec.mark();
    fs::watcher::touch(file, false, fs::now() + 5s);
    check(rec.wait_for(mark, is_status(file, entry_status::modified)), "a new modification time alone is reported");
    mark = rec.mark();
    fs::error_code err;
    fs::remove(file, err);
    check(rec.wait_for(mark, is_status(file, entry_status::removed)), "a deleted file is reported as removed");
    mark = rec.mark();
    fs::remove_all(folder, err);
    check(rec.wait_for(mark, is_status(folder / "deeper" / "leaf.txt", entry_status::removed)),
          "a deleted folder reports its children as removed");
    std::this_thread::sleep_for(QUIET_CHECK);
    const auto entries = rec.entries_since(mark);
    check(count_matching(entries, is_status(folder, entry_status::removed)) == 1, "the deleted folder is removed once");
    check(count_matching(entries, is_status(folder / "inner.txt", entry_status::removed)) == 1,
          "a child of the deleted folder is removed once");
    fs::watcher::unwatch(id);
}

void test_rename_same_folder()
{
    std::printf("test_rename_same_folder\n");
    const auto tree = make_tree("rename_same_folder");
    const fs::path from = tree.root / "before.dat";
    const fs::path to = tree.root / "after.dat";
    write_file(from, "rename me");
    recorder rec;
    const auto id = watch_tree(tree.root, rec);
    const auto mark = rec.mark();
    const auto rename_err = run_step(
        [&](fs::error_code& step_err) -> void
        {
            fs::rename(from, to, step_err);
        });
    check(!rename_err, "the rename itself succeeded");
    check(rec.wait_for(mark, is_rename(from, to)), "a rename in one folder is reported as renamed");
    std::this_thread::sleep_for(QUIET_CHECK);
    const auto entries = rec.entries_since(mark);
    check(count_touching(entries, from) == 1 && count_touching(entries, to) == 1,
          "a rename in one folder is the only entry for both names");
    fs::watcher::unwatch(id);
}

void test_move_across_folders()
{
    std::printf("test_move_across_folders\n");
    const auto tree = make_tree("move_across_folders");
    const fs::path from = tree.root / "one" / "moved.dat";
    const fs::path to = tree.root / "two" / "moved.dat";
    write_file(from, "move me");
    fs::error_code err;
    fs::create_directories(to.parent_path(), err);
    recorder rec;
    const auto id = watch_tree(tree.root, rec);
    const auto mark = rec.mark();
    const auto rename_err = run_step(
        [&](fs::error_code& step_err) -> void
        {
            fs::rename(from, to, step_err);
        });
    check(!rename_err, "the rename itself succeeded");
    check(rec.wait_for(mark, is_rename(from, to)), "a move across folders is reported as renamed");
    std::this_thread::sleep_for(QUIET_CHECK);
    const auto entries = rec.entries_since(mark);
    check(count_touching(entries, from) == 1 && count_touching(entries, to) == 1,
          "a move across folders is the only entry for both names");
    fs::watcher::unwatch(id);
}

void test_rename_folder_with_children()
{
    std::printf("test_rename_folder_with_children\n");
    const auto tree = make_tree("rename_folder");
    const fs::path from = tree.root / "folder";
    const fs::path to = tree.root / "renamed";
    const std::vector<fs::path> children = {"a.dat", "b.png", fs::path("sub") / "c.dat"};
    for(const auto& child : children)
    {
        write_file(from / child, "child " + child.generic_string());
    }
    recorder rec;
    const auto id = watch_tree(tree.root, rec);
    const auto mark = rec.mark();
    const auto rename_err = run_step(
        [&](fs::error_code& step_err) -> void
        {
            fs::rename(from, to, step_err);
        });
    check(!rename_err, "the rename itself succeeded");
    check(rec.wait_for(mark, is_rename(from, to)), "a folder rename is reported as renamed");
    std::this_thread::sleep_for(QUIET_CHECK);
    const auto entries = rec.entries_since(mark);
    const int failures_before = g_failures;
    check(count_touching(entries, from) == 1, "the renamed folder has one entry");
    for(const auto& child : children)
    {
        check(count_matching(entries, is_rename(from / child, to / child)) == 1,
              "child " + child.generic_string() + " of a renamed folder is renamed");
        check(count_touching(entries, to / child) == 1,
              "child " + child.generic_string() + " of a renamed folder has no other entry");
    }
    if(g_failures != failures_before)
    {
        dump_entries(entries);
    }
    fs::watcher::unwatch(id);
}

void test_copy_then_delete()
{
    std::printf("test_copy_then_delete\n");
    const auto tree = make_tree("copy_then_delete");
    for(const auto gap : MOVE_GAPS)
    {
        write_file(tree.root / ("source_" + std::to_string(gap.count()) + ".dat"),
                   "copy then delete " + std::to_string(gap.count()));
    }
    recorder rec;
    const auto id = watch_tree(tree.root, rec);
    for(const auto gap : MOVE_GAPS)
    {
        const std::string suffix = std::to_string(gap.count());
        const fs::path from = tree.root / ("source_" + suffix + ".dat");
        const fs::path to = tree.root / "moved" / ("target_" + suffix + ".dat");
        const fs::path staged = tree.staging / ("target_" + suffix + ".dat");
        fs::error_code err;
        fs::create_directories(to.parent_path(), err);
        stage_copy(from, staged);
        const std::string what = "copy then delete " + suffix + " ms apart";
        const auto mark = rec.mark();
        const auto steps = run_two_steps(
            [&](fs::error_code& step_err) -> void
            {
                fs::rename(staged, to, step_err);
            },
            gap,
            [&](fs::error_code& step_err) -> void
            {
                fs::remove(from, step_err);
            });
        const bool renamed = rec.wait_for(mark, is_rename(from, to));
        std::this_thread::sleep_for(QUIET_CHECK);
        if(!is_checkable(steps, what))
        {
            continue;
        }
        const auto entries = rec.entries_since(mark);
        const bool paired = renamed && count_touching(entries, from) == 1 && count_touching(entries, to) == 1;
        check(paired, what + " is one rename");
        if(!paired)
        {
            dump_entries(entries);
        }
    }
    fs::watcher::unwatch(id);
}

void test_delete_then_move_in()
{
    std::printf("test_delete_then_move_in\n");
    const auto tree = make_tree("delete_then_move_in");
    for(const auto gap : MOVE_GAPS)
    {
        write_file(tree.root / ("source_" + std::to_string(gap.count()) + ".dat"),
                   "delete then move in " + std::to_string(gap.count()));
    }
    recorder rec;
    const auto id = watch_tree(tree.root, rec);
    for(const auto gap : MOVE_GAPS)
    {
        const std::string suffix = std::to_string(gap.count());
        const fs::path from = tree.root / ("source_" + suffix + ".dat");
        const fs::path to = tree.root / "moved" / ("target_" + suffix + ".dat");
        const fs::path staged = tree.staging / ("target_" + suffix + ".dat");
        fs::error_code err;
        fs::create_directories(to.parent_path(), err);
        stage_copy(from, staged);
        const std::string what = "delete then move in " + suffix + " ms apart";
        const auto mark = rec.mark();
        const auto steps = run_two_steps(
            [&](fs::error_code& step_err) -> void
            {
                fs::remove(from, step_err);
            },
            gap,
            [&](fs::error_code& step_err) -> void
            {
                fs::rename(staged, to, step_err);
            });
        const bool renamed = rec.wait_for(mark, is_rename(from, to));
        std::this_thread::sleep_for(QUIET_CHECK);
        if(!is_checkable(steps, what))
        {
            continue;
        }
        const auto entries = rec.entries_since(mark);
        const bool paired = renamed && count_touching(entries, from) == 1 && count_touching(entries, to) == 1;
        check(paired, what + " is one rename");
        if(!paired)
        {
            dump_entries(entries);
        }
    }
    fs::watcher::unwatch(id);
}

void test_safe_save()
{
    std::printf("test_safe_save\n");
    const auto tree = make_tree("safe_save");
    for(const auto gap : SAFE_SAVE_GAPS)
    {
        write_file(tree.root / ("document_" + std::to_string(gap.count()) + ".dat"), "first version");
    }
    recorder rec;
    const auto id = watch_tree(tree.root, rec);
    for(const auto gap : SAFE_SAVE_GAPS)
    {
        const std::string suffix = std::to_string(gap.count());
        const fs::path document = tree.root / ("document_" + suffix + ".dat");
        const fs::path temp = tree.root / ("document_" + suffix + ".dat.save");
        const std::string what = "a safe save " + suffix + " ms apart";
        const auto mark = rec.mark();
        write_file(temp, "second, longer version " + suffix);
        const auto steps = run_two_steps(
            [&](fs::error_code& step_err) -> void
            {
                fs::remove(document, step_err);
            },
            gap,
            [&](fs::error_code& step_err) -> void
            {
                fs::rename(temp, document, step_err);
            });
        const bool modified = rec.wait_for(mark, is_status(document, entry_status::modified));
        std::this_thread::sleep_for(QUIET_CHECK);
        if(!is_checkable(steps, what))
        {
            continue;
        }
        const auto entries = rec.entries_since(mark);
        const bool split = count_matching(entries, is_status(document, entry_status::removed)) != 0 ||
                           count_matching(entries, is_status(document, entry_status::created)) != 0;
        check(modified && !split, what + " is a modification, not removed + created");
        if(!modified || split)
        {
            dump_entries(entries);
        }
    }
    fs::watcher::unwatch(id);
}

void test_copy_is_created()
{
    std::printf("test_copy_is_created\n");
    const auto tree = make_tree("copy_is_created");
    const fs::path original = tree.root / "original.dat";
    const fs::path copy = tree.root / "copies" / "original.dat";
    write_file(original, "keep me");
    recorder rec;
    const auto id = watch_tree(tree.root, rec);
    const auto mark = rec.mark();
    fs::error_code err;
    fs::create_directories(copy.parent_path(), err);
    fs::copy_file(original, copy, err);
    check(rec.wait_for(mark, is_status(copy, entry_status::created)), "a copy whose original stays is created");
    std::this_thread::sleep_for(QUIET_CHECK);
    const auto entries = rec.entries_since(mark);
    check(count_touching(entries, original) == 0, "the original of a copy has no entry");
    fs::watcher::unwatch(id);
}

void test_pause_resume()
{
    std::printf("test_pause_resume\n");
    const auto tree = make_tree("pause_resume");
    recorder rec;
    const auto id = watch_tree(tree.root, rec);
    const auto mark = rec.mark();
    {
        fs::watcher::scoped_pause pause;
        for(int index = 0; index < PAUSED_FILE_COUNT; ++index)
        {
            write_file(tree.root / ("paused_" + std::to_string(index) + ".dat"), "written while paused");
        }
        std::this_thread::sleep_for(POLL_INTERVAL * 2);
        check(rec.entries_since(mark).empty(), "nothing is delivered while paused");
    }
    const fs::path last = tree.root / ("paused_" + std::to_string(PAUSED_FILE_COUNT - 1) + ".dat");
    check(rec.wait_for(mark, is_status(last, entry_status::created)), "resume delivers what changed while paused");
    std::this_thread::sleep_for(QUIET_CHECK);
    const auto entries = rec.entries_since(mark);
    bool each_once = true;
    for(int index = 0; index < PAUSED_FILE_COUNT; ++index)
    {
        each_once &= count_touching(entries, tree.root / ("paused_" + std::to_string(index) + ".dat")) == 1;
    }
    check(each_once, "every change made while paused is delivered once");
    fs::watcher::unwatch(id);
}

void test_filters_on_shared_listener()
{
    std::printf("test_filters_on_shared_listener\n");
    // One filter per source format, the way the asset watcher registers them on the compiled folder.
    const std::vector<std::string> formats = {
        ".etex", ".png", ".jpg", ".jpeg", ".tga", ".dds", ".ktx", ".ktx2", ".pvr", ".exr", ".hdr",
        ".bmp", ".gif", ".psd", ".sc", ".emesh", ".gltf", ".glb", ".obj", ".fbx", ".FBX",
        ".dae", ".blend", ".3ds", ".mat", ".ematerial", ".anim", ".pfb", ".spfb", ".phm", ".ephmaterial",
        ".eaudioclip", ".ogg", ".wav", ".flac", ".mp3", ".ttf", ".otf", ".cs", ".rhtml", ".rcss"};
    const auto tree = make_tree("filters");
    std::vector<std::unique_ptr<recorder>> recorders;
    std::vector<std::uint64_t> ids;
    for(const auto& format : formats)
    {
        recorders.push_back(std::make_unique<recorder>());
        ids.push_back(watch_tree(tree.root, *recorders.back(), "*" + format + ".asset"));
    }
    std::vector<std::size_t> marks;
    for(auto& rec : recorders)
    {
        marks.push_back(rec->mark());
    }
    std::vector<fs::path> files;
    fs::watcher::pause();
    for(std::size_t index = 0; index < formats.size(); ++index)
    {
        files.push_back(tree.root / ("dir_" + std::to_string(index % 4)) / ("asset_" + std::to_string(index) + formats[index] + ".asset"));
        write_file(files.back(), "compiled");
    }
    const auto resumed = clock_type::now();
    fs::watcher::resume();
    bool all_arrived = true;
    clock_type::time_point first = clock_type::time_point::max();
    clock_type::time_point last = clock_type::time_point::min();
    for(std::size_t index = 0; index < formats.size(); ++index)
    {
        all_arrived &= recorders[index]->wait_for(marks[index], is_status(files[index], entry_status::created));
    }
    std::this_thread::sleep_for(QUIET_CHECK);
    bool only_own = true;
    for(std::size_t index = 0; index < formats.size(); ++index)
    {
        const auto entries = recorders[index]->entries_since(marks[index]);
        only_own &= entries.size() == 1;
        for(const auto& recorded : entries)
        {
            first = std::min(first, recorded.time);
            last = std::max(last, recorded.time);
        }
    }
    check(all_arrived, "every per-format watcher receives its file");
    check(only_own, "every per-format watcher receives only its file");
    if(tests::wants("bench") && first <= last)
    {
        std::printf("  fan-out: %zu watchers, resume -> first callback %.2f ms, dispatch spread %.2f ms\n",
                    formats.size(),
                    to_ms(first - resumed),
                    to_ms(last - first));
    }
    for(const auto id : ids)
    {
        fs::watcher::unwatch(id);
    }
}

void test_subfolder_watch_on_parent_listener()
{
    std::printf("test_subfolder_watch_on_parent_listener\n");
    const auto tree = make_tree("subfolder");
    fs::error_code err;
    fs::create_directories(tree.root / "sub", err);
    fs::create_directories(tree.root / "other", err);
    recorder parent;
    recorder child;
    const auto parent_id = watch_tree(tree.root, parent);
    // Spelled differently from the parent's tree on purpose; it names the same folder.
    const auto child_id = watch_tree(tree.root / "other" / ".." / "sub", child);
    const auto parent_mark = parent.mark();
    const auto child_mark = child.mark();
    const fs::path outside = tree.root / "other" / "outside.dat";
    const fs::path inside = tree.root / "sub" / "inside.dat";
    write_file(outside, "outside");
    write_file(inside, "inside");
    check(parent.wait_for(parent_mark, is_status(outside, entry_status::created)), "the parent watch sees its tree");
    check(child.wait_for(child_mark, is_status(inside, entry_status::created)), "the subfolder watch sees its subtree");
    std::this_thread::sleep_for(QUIET_CHECK);
    check(count_touching(child.entries_since(child_mark), outside) == 0,
          "the subfolder watch does not see a sibling folder");
    fs::watcher::unwatch(child_id);
    fs::watcher::unwatch(parent_id);
}

void test_recursive_after_non_recursive()
{
    std::printf("test_recursive_after_non_recursive\n");
    const auto tree = make_tree("recursive_after_flat");
    fs::error_code err;
    fs::create_directories(tree.root / "deep", err);
    recorder flat;
    recorder deep;
    const auto flat_id = watch_tree(tree.root, flat, "*", false);
    const auto deep_id = watch_tree(tree.root, deep, "*", true);
    const auto mark = deep.mark();
    const fs::path file = tree.root / "deep" / "nested.dat";
    write_file(file, "nested");
    check(deep.wait_for(mark, is_status(file, entry_status::created)),
          "a recursive watch after a non-recursive one at the same path sees nested files");
    fs::watcher::unwatch(deep_id);
    fs::watcher::unwatch(flat_id);
}

void test_unwatch_inside_callback()
{
    std::printf("test_unwatch_inside_callback\n");
    const auto tree = make_tree("unwatch_inside");
    std::atomic<std::uint64_t> id{0};
    std::atomic<int> calls{0};
    std::atomic<bool> unwatched{false};
    auto callback = [&](const std::vector<fs::watcher::entry>&, bool is_initial_list)
    {
        if(is_initial_list)
        {
            return;
        }
        ++calls;
        fs::watcher::unwatch(id.load());
        unwatched = true;
    };
    id = fs::watcher::watch(tree.root, fs::pattern_filter("*"), true, true, POLL_INTERVAL, callback, "fs watcher test");
    write_file(tree.root / "first.dat", "first");
    const auto deadline = clock_type::now() + EVENT_TIMEOUT;
    while(!unwatched && clock_type::now() < deadline)
    {
        std::this_thread::sleep_for(10ms);
    }
    check(unwatched.load(), "unwatch from inside the callback returns");
    write_file(tree.root / "second.dat", "second");
    std::this_thread::sleep_for(QUIET_CHECK);
    check(calls.load() == 1, "no callback runs after unwatch from inside the callback");
}

void test_unwatch_waits_for_callback()
{
    std::printf("test_unwatch_waits_for_callback\n");
    const auto tree = make_tree("unwatch_waits");
    std::atomic<int> calls{0};
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    auto callback = [&](const std::vector<fs::watcher::entry>&, bool is_initial_list)
    {
        if(is_initial_list)
        {
            return;
        }
        ++calls;
        running = true;
        std::this_thread::sleep_for(CALLBACK_BLOCK);
        finished = true;
    };
    const auto id =
        fs::watcher::watch(tree.root, fs::pattern_filter("*"), true, true, POLL_INTERVAL, callback, "fs watcher test");
    write_file(tree.root / "first.dat", "first");
    const auto deadline = clock_type::now() + EVENT_TIMEOUT;
    while(!running && clock_type::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    check(running.load(), "the blocking callback started");
    fs::watcher::unwatch(id);
    check(finished.load(), "unwatch returns only after the running callback finished");
    const int calls_at_unwatch = calls.load();
    write_file(tree.root / "second.dat", "second");
    std::this_thread::sleep_for(QUIET_CHECK);
    check(calls.load() == calls_at_unwatch, "no callback runs after unwatch returned");
}

/// Event times observed by the latency watcher, keyed by file name.
struct event_log
{
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::string, std::vector<clock_type::time_point>> times_by_name;

    auto wait_for_event(const std::string& name, std::size_t previous_count) -> hpp::optional<clock_type::time_point>
    {
        std::unique_lock<std::mutex> lock(mutex);
        const bool arrived = changed.wait_for(lock,
                                              EVENT_TIMEOUT,
                                              [&]
                                              {
                                                  return times_by_name[name].size() > previous_count;
                                              });
        if(!arrived)
        {
            return hpp::nullopt;
        }
        return times_by_name[name][previous_count];
    }

    auto count(const std::string& name) -> std::size_t
    {
        std::lock_guard<std::mutex> lock(mutex);
        return times_by_name[name].size();
    }
};

void print_latency(const char* label, std::vector<double> samples)
{
    if(samples.empty())
    {
        std::printf("  %-42s no samples\n", label);
        return;
    }
    std::sort(samples.begin(), samples.end());
    const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / double(samples.size());
    std::printf("  %-42s min %7.1f  median %7.1f  mean %7.1f  max %7.1f ms\n",
                label,
                samples.front(),
                samples[samples.size() / 2],
                mean,
                samples.back());
}

/// Two hops the way the editor chains them: a source change is seen by a scan, the callback rewrites the
/// sibling .meta inside the same tree (as the meta syncer does), and that write is seen by a later scan.
void bench_latency()
{
    std::printf("bench_latency\n");
    const auto tree = make_tree("latency");
    for(int dir = 0; dir < LATENCY_DIRS; ++dir)
    {
        for(int file = 0; file < LATENCY_FILES_PER_DIR; ++file)
        {
            write_file(tree.root / ("dir_" + std::to_string(dir)) / ("file_" + std::to_string(file) + ".src"), "seed\n");
        }
    }
    event_log log;
    auto callback = [&log](const std::vector<fs::watcher::entry>& entries, bool is_initial_list)
    {
        if(is_initial_list)
        {
            return;
        }
        const auto now = clock_type::now();
        std::vector<fs::path> meta_to_write;
        {
            std::lock_guard<std::mutex> lock(log.mutex);
            for(const auto& entry : entries)
            {
                log.times_by_name[entry.path.filename().string()].push_back(now);
                if(entry.path.extension() == ".src" && entry.status == entry_status::modified)
                {
                    meta_to_write.push_back(entry.path);
                }
            }
        }
        for(const auto& source : meta_to_write)
        {
            fs::path meta = source;
            meta += ".meta";
            append_file(meta, "meta\n");
        }
        log.changed.notify_all();
    };
    const auto id = fs::watcher::watch(tree.root, fs::pattern_filter("*"), true, true, POLL_INTERVAL, callback, "fs watcher test");
    std::mt19937 random(12345u);
    std::uniform_int_distribution<int> phase_ms(0, LATENCY_MAX_PHASE_MS);
    std::vector<double> first_hop;
    std::vector<double> second_hop;
    std::vector<double> both;
    for(int trial = 0; trial < LATENCY_TRIALS; ++trial)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(phase_ms(random)));
        const std::string name = "file_" + std::to_string(trial % LATENCY_FILES_PER_DIR) + ".src";
        const fs::path source = tree.root / ("dir_" + std::to_string(trial % LATENCY_DIRS)) / name;
        const std::size_t source_before = log.count(name);
        const std::size_t meta_before = log.count(name + ".meta");
        const auto written = clock_type::now();
        append_file(source, "edit " + std::to_string(trial) + "\n");
        const auto source_seen = log.wait_for_event(name, source_before);
        const auto meta_seen = log.wait_for_event(name + ".meta", meta_before);
        if(!source_seen || !meta_seen)
        {
            std::printf("  trial %d timed out\n", trial);
            continue;
        }
        first_hop.push_back(to_ms(*source_seen - written));
        second_hop.push_back(to_ms(*meta_seen - *source_seen));
        both.push_back(to_ms(*meta_seen - written));
    }
    fs::watcher::unwatch(id);
    print_latency("hop 1: write -> callback", first_hop);
    print_latency("hop 2: callback writes .meta -> callback", second_hop);
    print_latency("both hops", both);
}

auto run_fs_watcher_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    std::printf("scratch folder %s\n", get_run_root().string().c_str());
    if(!tests::wants("bench-only"))
    {
        test_lifecycle();
        test_rename_same_folder();
        test_move_across_folders();
        test_rename_folder_with_children();
        test_copy_then_delete();
        test_delete_then_move_in();
        test_safe_save();
        test_copy_is_created();
        test_pause_resume();
        test_filters_on_shared_listener();
        test_subfolder_watch_on_parent_listener();
        test_recursive_after_non_recursive();
        test_unwatch_inside_callback();
        test_unwatch_waits_for_callback();
    }
    if(tests::wants("bench") || tests::wants("bench-only"))
    {
        bench_latency();
    }
    std::printf("fs watcher: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("fs watcher", run_fs_watcher_suite)
