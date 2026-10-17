/*
 * Validation suite for directory mirroring, which deploys use to update an earlier build in place.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "fs mirror"
 *
 * This pins:
 *   - a first mirror copies every file; a second one copies nothing;
 *   - a changed file is copied again and gets the source's write time;
 *   - files the source no longer has are removed, with the folders they leave empty;
 *   - files the filter turns down are neither copied nor kept;
 *   - a missing source fails with an error.
 * Everything lives in a temp folder.
 */

#include "../tests.h"

#include <filesystem/filesystem.h>
#include <filesystem/mirror.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace
{

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

auto get_temp_folder() -> fs::path
{
    return fs::temp_directory_path() / "unravel-tests-fs-mirror";
}

void write_text(const fs::path& file, const std::string& text)
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

auto read_text(const fs::path& file) -> std::string
{
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

auto mirror(const fs::path& from, const fs::path& to, const fs::mirror_filter& include = {}) -> fs::mirror_stats
{
    fs::mirror_stats stats;
    fs::error_code ec;
    check(fs::mirror_directory(from, to, include, stats, ec), "the mirror succeeds: " + ec.message());
    return stats;
}

void test_first_and_repeated_mirror(const fs::path& source, const fs::path& target)
{
    std::printf("test_first_and_repeated_mirror\n");
    write_text(source / "a.dll", "library");
    write_text(source / "sub/b.txt", "settings");
    write_text(source / "sub/deep/c.json", "{}");
    const auto first = mirror(source, target);
    check(first.copied == 3 && first.unchanged == 0, "the first mirror copies every file");
    check(read_text(target / "sub/deep/c.json") == "{}", "nested files arrive with their content");
    const auto second = mirror(source, target);
    check(second.copied == 0 && second.unchanged == 3 && second.removed == 0, "a second mirror copies nothing");
}

void test_changed_file_is_copied(const fs::path& source, const fs::path& target)
{
    std::printf("test_changed_file_is_copied\n");
    write_text(source / "a.dll", "library, version two");
    const auto stats = mirror(source, target);
    check(stats.copied == 1 && stats.unchanged == 2, "only the changed file is copied");
    check(read_text(target / "a.dll") == "library, version two", "the copy has the new content");
    fs::error_code ec;
    check(fs::last_write_time(target / "a.dll", ec) == fs::last_write_time(source / "a.dll", ec),
          "the copy gets the source's write time");
}

void test_removed_files_go(const fs::path& source, const fs::path& target)
{
    std::printf("test_removed_files_go\n");
    fs::error_code ec;
    fs::remove_all(source / "sub/deep", ec);
    write_text(source / "d.txt", "new");
    write_text(target / "stray.bin", "left from an older build");
    const auto stats = mirror(source, target);
    check(stats.copied == 1 && stats.removed == 2, "the new file is copied, the vanished and stray ones removed");
    check(!fs::exists(target / "sub/deep", ec), "a folder left empty is removed");
    check(fs::exists(target / "sub/b.txt", ec), "files the source still has stay");
}

void test_filter(const fs::path& source, const fs::path& target)
{
    std::printf("test_filter\n");
    write_text(source / "a.pdb", "symbols");
    const auto without_symbols = [](const fs::path& file)
    {
        return file.extension() != ".pdb";
    };
    mirror(source, target);
    fs::error_code ec;
    check(fs::exists(target / "a.pdb", ec), "without a filter the file is mirrored");
    const auto stats = mirror(source, target, without_symbols);
    check(!fs::exists(target / "a.pdb", ec) && stats.removed == 1, "a file the filter turns down is removed");
}

void test_missing_source(const fs::path& target)
{
    std::printf("test_missing_source\n");
    fs::mirror_stats stats;
    fs::error_code ec;
    check(!fs::mirror_directory(get_temp_folder() / "no_such_folder", target, {}, stats, ec) && ec,
          "a missing source fails with an error");
}

auto run_fs_mirror_suite(rtti::context&) -> int
{
    g_checks = 0;
    g_failures = 0;
    fs::error_code ec;
    fs::remove_all(get_temp_folder(), ec);
    const auto source = get_temp_folder() / "source";
    const auto target = get_temp_folder() / "target";
    test_first_and_repeated_mirror(source, target);
    test_changed_file_is_copied(source, target);
    test_removed_files_go(source, target);
    test_filter(source, target);
    test_missing_source(target);
    fs::remove_all(get_temp_folder(), ec);
    std::printf("fs mirror: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("fs mirror", run_fs_mirror_suite)
