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
 *   - a dotted directory is handled by the directory mapping.
 *
 * Environment: UNRAVEL_FS_SYNCER_TEST_DIR overrides the scratch folder.
 */

#include "../tests.h"

#include <filesystem/filesystem.h>
#include <filesystem/syncer.h>

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

void write_file(const fs::path& path)
{
    fs::error_code err;
    fs::create_directories(path.parent_path(), err);
    std::ofstream(path, std::ios::binary) << "data\n";
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
    const auto on_file = [&](const std::string& key, const fs::path& path, const std::vector<fs::path>& entries, bool)
    {
        std::lock_guard<std::mutex> lock(observed.mutex);
        observed.files.emplace_back(key, path.filename().string(), entries);
    };
    const auto on_directory = [&](const std::string&, const fs::path& path, const std::vector<fs::path>&, bool)
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

auto run_fs_syncer_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_dotted_names();
    std::printf("fs syncer: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("fs syncer", run_fs_syncer_suite)
