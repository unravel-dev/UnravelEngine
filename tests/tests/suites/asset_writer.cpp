/*
 * Validation suite for asset_writer: atomic writes through a `.<uuid>.temp` file and the sweep of temp files
 * that outlived their write.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "asset writer"
 *
 * Every atomic write creates a hidden temp file next to its destination and renames it over the destination.
 * A temp file left behind is never picked up by anything, so these pin:
 *   - a successful write leaves only the destination;
 *   - a callback that throws fails the write instead of terminating the process, keeps the old destination and
 *     removes its temp file;
 *   - an empty write is rejected and its temp file removed;
 *   - the stale sweep removes old `.<uuid>.temp` files, recursively and below folder names the narrow code page
 *     cannot represent, and keeps fresh temp files and user files that only resemble the pattern.
 *
 * Environment: UNRAVEL_ASSET_WRITER_TEST_DIR overrides the scratch folder. Every run writes into a fresh
 * subfolder and nothing is deleted.
 */

#include "../tests.h"

#include <engine/assets/impl/asset_writer.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace unravel;

namespace
{

constexpr auto stale_age = std::chrono::hours(2);

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
    const char* configured = std::getenv("UNRAVEL_ASSET_WRITER_TEST_DIR");
    const fs::path base =
        configured ? fs::path(configured) : fs::temp_directory_path() / "unravel_asset_writer_tests";
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const fs::path root = base / ("run_" + std::to_string(stamp));
    fs::error_code err;
    fs::create_directories(root, err);
    return root;
}

void write_text(const fs::path& path, const std::string& text)
{
    fs::error_code err;
    fs::create_directories(path.parent_path(), err);
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void make_stale(const fs::path& path)
{
    fs::error_code err;
    fs::last_write_time(path, fs::file_time_type::clock::now() - stale_age, err);
}

/// Number of files directly in `dir` whose extension is ".temp".
auto count_temp_files(const fs::path& dir) -> int
{
    int count = 0;
    fs::error_code err;
    for(fs::directory_iterator it(dir, err), end; it != end && !err; it.increment(err))
    {
        if(it->path().extension() == ".temp")
        {
            ++count;
        }
    }
    return count;
}

void test_successful_write(const fs::path& run_root)
{
    const fs::path dir = run_root / "success";
    const fs::path destination = dir / "settings.cfg";
    write_text(destination, "old");
    fs::error_code ec;
    asset_writer::atomic_write_file(
        destination,
        [](const fs::path& temp)
        {
            write_text(temp, "new");
        },
        ec);
    check(!ec, "a successful write reports no error");
    check(read_text(destination) == "new", "a successful write replaces the destination");
    check(count_temp_files(dir) == 0, "a successful write leaves no temp file");
}

void test_throwing_callback(const fs::path& run_root)
{
    const fs::path dir = run_root / "throwing";
    const fs::path destination = dir / "settings.cfg";
    write_text(destination, "old");
    fs::error_code ec;
    asset_writer::atomic_write_file(
        destination,
        [](const fs::path& temp)
        {
            write_text(temp, "partial");
            throw std::runtime_error("serialization failed");
        },
        ec);
    check(static_cast<bool>(ec), "a throwing callback reports an error");
    check(read_text(destination) == "old", "a throwing callback keeps the old destination");
    check(count_temp_files(dir) == 0, "a throwing callback leaves no temp file");
}

void test_empty_write(const fs::path& run_root)
{
    const fs::path dir = run_root / "empty";
    const fs::path destination = dir / "settings.cfg";
    write_text(destination, "old");
    fs::error_code ec;
    asset_writer::atomic_write_file(
        destination,
        [](const fs::path& temp)
        {
            write_text(temp, "");
        },
        ec);
    check(static_cast<bool>(ec), "an empty write reports an error");
    check(read_text(destination) == "old", "an empty write keeps the old destination");
    check(count_temp_files(dir) == 0, "an empty write leaves no temp file");
}

void test_stale_sweep(const fs::path& run_root)
{
    const fs::path dir = run_root / "sweep";
    // A folder name outside every narrow code page: the sweep must not convert it.
    const fs::path unicode_dir = dir / fs::path(u8"\u6587\u4ef6");
    const fs::path stale_root = dir / ".0f8fad5b-d9cb-469f-a165-70867728950e.temp";
    const fs::path stale_nested = unicode_dir / ".7c9e6679-7425-40de-944b-e07fc1f90ae7.temp";
    const fs::path fresh = dir / ".a3bb189e-8bf9-3888-9912-ace4e6543002.temp";
    const fs::path user_dot_file = dir / ".settings.temp";
    const fs::path user_plain_file = dir / "notes.temp";
    const fs::path uuid_with_suffix = dir / ".e02fd0e4-00fd-090a-ca30-0d00a0038ba0.temp.bak";
    for(const fs::path& path : {stale_root, stale_nested, fresh, user_dot_file, user_plain_file, uuid_with_suffix})
    {
        write_text(path, "data");
    }
    for(const fs::path& path : {stale_root, stale_nested, user_dot_file, user_plain_file, uuid_with_suffix})
    {
        make_stale(path);
    }
    const std::size_t removed = asset_writer::cleanup_stale_temp_files(dir, true);
    fs::error_code err;
    check(removed == 2, "the sweep reports the two stale temp files it removed");
    check(!fs::exists(stale_root, err), "the sweep removes a stale temp file");
    check(!fs::exists(stale_nested, err), "the sweep recurses below a folder name outside the code page");
    check(fs::exists(fresh, err), "the sweep keeps a temp file younger than the minimum age");
    check(fs::exists(user_dot_file, err), "the sweep keeps a dot-file that is not a uuid temp file");
    check(fs::exists(user_plain_file, err), "the sweep keeps a plain .temp file");
    check(fs::exists(uuid_with_suffix, err), "the sweep keeps a uuid temp name with a further extension");
}

auto run_asset_writer_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const fs::path run_root = get_run_root();
    std::printf("scratch folder %s\n", run_root.string().c_str());
    test_successful_write(run_root);
    test_throwing_callback(run_root);
    test_empty_write(run_root);
    test_stale_sweep(run_root);
    std::printf("asset writer: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("asset writer", run_asset_writer_suite)
