/*
 * Validation suite for fs::syncer mapping resolution.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "fs syncer"
 *
 * The editor keeps the meta and compiled trees in step with the data tree through syncer mappings
 * keyed by extension (".png" -> ".meta", ".png.meta" -> ".asset"). A name with a '.' before its
 * extension used to be keyed by its whole extension chain (".001_baseColor.png") and matched no
 * mapping, so such an asset never got a .meta or a compiled file. These pin:
 *   - a dotted file is handled by the mapping of its real extension and its synced entry keeps the
 *     whole name;
 *   - a file whose extension has no mapping gets no callback;
 *   - a dotted directory is handled by the directory mapping;
 *   - a created or modified file's callback names the synced entries changed in the same batch.
 *
 * Environment: UNRAVEL_FS_SYNCER_TEST_DIR overrides the scratch folder.
 */

#include "../tests.h"

#include <filesystem/filesystem.h>
#include <filesystem/syncer.h>
#include <filesystem/watcher.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace unravel;

namespace
{

constexpr auto EVENT_TIMEOUT = std::chrono::seconds(5);
constexpr auto POLL_STEP = std::chrono::milliseconds(20);

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

auto get_run_root() -> fs::path
{
    const char* configured = std::getenv("UNRAVEL_FS_SYNCER_TEST_DIR");
    const fs::path base = configured ? fs::path(configured) : fs::temp_directory_path() / "unravel_fs_syncer_tests";
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return base / ("run_" + std::to_string(stamp));
}

void write_file(const fs::path& path, const std::string& content = "data\n")
{
    fs::error_code err;
    fs::create_directories(path.parent_path(), err);
    std::ofstream(path, std::ios::binary) << content;
}

struct observed_entries
{
    std::mutex mutex;
    /// File callbacks: (mapping key, file name, synced entries).
    std::vector<std::tuple<std::string, std::string, std::vector<fs::path>>> files;
    std::vector<std::string> directories;

    auto find_file(const std::string& name) -> const std::tuple<std::string, std::string, std::vector<fs::path>>*
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = std::find_if(files.begin(),
                                     files.end(),
                                     [&](const auto& file)
                                     {
                                         return std::get<1>(file) == name;
                                     });
        return it == files.end() ? nullptr : &*it;
    }

    auto has_directory(const std::string& name) -> bool
    {
        std::lock_guard<std::mutex> lock(mutex);
        return std::find(directories.begin(), directories.end(), name) != directories.end();
    }
};

void test_dotted_names()
{
    const fs::path root = get_run_root();
    const fs::path reference = root / "data";
    const fs::path synced = root / "meta";
    write_file(reference / "plain.png");
    write_file(reference / "Torso01.001_baseColor.png");
    write_file(reference / "notes.txt");
    write_file(reference / "Models.v2" / "inner.png");

    observed_entries observed;
    const auto on_file =
        [&](const std::string& key, const fs::path& path, const std::vector<fs::path>& entries, bool, const std::vector<fs::path>&)
    {
        std::lock_guard<std::mutex> lock(observed.mutex);
        observed.files.emplace_back(key, path.filename().string(), entries);
    };
    const auto on_directory =
        [&](const std::string&, const fs::path& path, const std::vector<fs::path>&, bool, const std::vector<fs::path>&)
    {
        std::lock_guard<std::mutex> lock(observed.mutex);
        observed.directories.push_back(path.filename().string());
    };

    fs::syncer syncer;
    syncer.set_mapping(".png", {".meta"}, on_file, on_file, nullptr, nullptr);
    syncer.set_directory_mapping(on_directory, on_directory, nullptr, nullptr);
    syncer.sync(reference, synced);
    const auto deadline = std::chrono::steady_clock::now() + EVENT_TIMEOUT;
    while(std::chrono::steady_clock::now() < deadline
          && (!observed.find_file("Torso01.001_baseColor.png") || !observed.find_file("inner.png")
              || !observed.has_directory("Models.v2")))
    {
        std::this_thread::sleep_for(POLL_STEP);
    }
    syncer.unsync();

    const auto* dotted = observed.find_file("Torso01.001_baseColor.png");
    check(dotted != nullptr, "a dotted file reaches the mapping of its extension");
    if(dotted)
    {
        check(std::get<0>(*dotted) == ".png", "the dotted file is handled as .png");
        const auto& entries = std::get<2>(*dotted);
        check(entries.size() == 1 && entries.front().filename() == "Torso01.001_baseColor.png.meta",
              "the synced entry keeps the whole dotted name");
    }
    check(observed.find_file("plain.png") != nullptr, "a plain file still reaches its mapping");
    check(observed.find_file("notes.txt") == nullptr, "an unmapped extension gets no callback");
    check(observed.has_directory("Models.v2"), "a dotted directory reaches the directory mapping");
    fs::error_code err;
    fs::remove_all(root, err);
}

/// One created or modified file callback: the file, whether it came from the initial listing, and the names of
/// its synced entries the same batch changed.
struct observed_change
{
    std::string name;
    bool is_initial_listing{};
    std::vector<std::string> changed_synced;
};

struct observed_changes
{
    std::mutex mutex;
    std::vector<observed_change> changes;

    /// Waits for a callback of `name` (from the initial listing or not) and copies it to `found`.
    auto wait_for(const std::string& name, bool is_initial_listing, observed_change& found) -> bool
    {
        const auto deadline = std::chrono::steady_clock::now() + EVENT_TIMEOUT;
        while(std::chrono::steady_clock::now() < deadline)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                const auto it = std::find_if(changes.begin(),
                                             changes.end(),
                                             [&](const observed_change& change)
                                             {
                                                 return change.name == name && change.is_initial_listing == is_initial_listing;
                                             });
                if(it != changes.end())
                {
                    found = *it;
                    return true;
                }
            }
            std::this_thread::sleep_for(POLL_STEP);
        }
        return false;
    }
};

/// The editor's meta syncer rewrites a file's .meta to have it compiled, unless the .meta changed in the same
/// batch: then it is compiled already. These pin what the callback is told, with the .meta beside its file as in
/// the editor:
///   - the initial listing names no changed synced entry;
///   - a file that arrives with its .meta in one batch names its .meta;
///   - a file changed alone names none.
void test_same_batch_changes()
{
    const fs::path root = get_run_root();
    const fs::path data = root / "data";
    write_file(data / "kept.png");
    write_file(data / "kept.png.meta");
    observed_changes observed;
    const auto on_file = [&](const std::string&,
                             const fs::path& path,
                             const std::vector<fs::path>&,
                             bool is_initial_listing,
                             const std::vector<fs::path>& changed_synced)
    {
        observed_change change{path.filename().string(), is_initial_listing, {}};
        for(const auto& entry : changed_synced)
        {
            change.changed_synced.push_back(entry.filename().string());
        }
        std::lock_guard<std::mutex> lock(observed.mutex);
        observed.changes.push_back(std::move(change));
    };
    fs::syncer syncer;
    syncer.set_mapping(".png", {".meta"}, on_file, on_file, nullptr, nullptr);
    syncer.sync(data, data);
    observed_change listed;
    check(observed.wait_for("kept.png", true, listed) && listed.changed_synced.empty(),
          "the initial listing names no changed synced entry");
    {
        fs::watcher::scoped_pause pause;
        write_file(data / "arrived.png", "arrived image\n");
        write_file(data / "arrived.png.meta", "arrived meta\n");
    }
    observed_change arrived;
    check(observed.wait_for("arrived.png", false, arrived) && arrived.changed_synced == std::vector<std::string>{"arrived.png.meta"},
          "a file that arrives with its .meta names the .meta as changed with it");
    write_file(data / "kept.png", "edited image\n");
    observed_change edited;
    check(observed.wait_for("kept.png", false, edited) && edited.changed_synced.empty(),
          "a file changed alone names no changed synced entry");
    syncer.unsync();
    fs::error_code err;
    fs::remove_all(root, err);
}

auto run_fs_syncer_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_dotted_names();
    test_same_batch_changes();
    std::printf("fs syncer: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("fs syncer", run_fs_syncer_suite)
