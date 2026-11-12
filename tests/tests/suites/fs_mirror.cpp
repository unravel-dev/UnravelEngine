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
 *   - a rename in letter case leaves exactly the source's files;
 *   - a stale file that cannot be removed fails the mirror (Windows, where an open file is locked);
 *   - a missing source fails with an error;
 *   - a source with no file to mirror succeeds and creates no target;
 *   - have_same_content: equal files, a difference in the last byte, a shorter file, a missing file;
 *   - remove_unlisted_files: files only an earlier list names go with the folders they leave empty, files the
 *     new list names and files no list names stay, a name that leads out of the folder is skipped;
 *   - shared_read_file: reads at any offset, and the file it holds open is replaced and removed meanwhile (what a
 *     deploy does to a site's file a browser still downloads), while it goes on reading the file it opened;
 *   - rename_over: a file another program holds for a moment (a scanner, a watcher) is replaced once it lets go, one
 *     it keeps holding is not and the error says so (Windows), a missing file fails at once.
 * Everything lives in a temp folder.
 */

#include "../tests.h"

#include <filesystem/filesystem.h>
#include <filesystem/mirror.h>
#include <filesystem/shared_file.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

/// How long the stand-in for a scanner holds a file: within what a rename waits for.
constexpr auto HOLD_TIME = std::chrono::milliseconds(60);

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

/// The files under `folder`, by fs::make_path_key of their relative paths.
auto list_file_keys(const fs::path& folder) -> std::vector<std::string>
{
    std::vector<std::string> keys;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_regular_file(ec))
        {
            keys.push_back(fs::make_path_key(it->path().lexically_relative(folder).generic_string()));
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void test_case_only_rename(const fs::path& target)
{
    std::printf("test_case_only_rename\n");
    const auto source = get_temp_folder() / "case_source";
    write_text(source / "Engine.dll", "library");
    mirror(source, target);
    fs::error_code ec;
    fs::remove(source / "Engine.dll", ec);
    write_text(source / "engine.dll", "library, renamed");
    mirror(source, target);
    check(list_file_keys(target) == list_file_keys(source), "after a rename in letter case the target holds the source's files");
    check(read_text(target / "engine.dll") == "library, renamed", "the renamed file keeps its new content");
}

void test_removal_failure(const fs::path& target)
{
    std::printf("test_removal_failure\n");
#ifdef _WIN32
    const auto source = get_temp_folder() / "locked_source";
    write_text(source / "kept.txt", "kept");
    mirror(source, target);
    write_text(target / "locked.bin", "held open");
    // An open stream does not share deletion on Windows, so the stale file cannot be removed.
    std::ifstream holder(target / "locked.bin", std::ios::binary);
    fs::mirror_stats stats;
    fs::error_code ec;
    check(!fs::mirror_directory(source, target, {}, stats, ec) && ec, "a stale file that cannot be removed fails the mirror");
    holder.close();
    check(fs::mirror_directory(source, target, {}, stats, ec) && stats.removed == 1, "once released, it is removed");
#else
    (void)target;
#endif
}

void test_missing_source(const fs::path& target)
{
    std::printf("test_missing_source\n");
    fs::mirror_stats stats;
    fs::error_code ec;
    check(!fs::mirror_directory(get_temp_folder() / "no_such_folder", target, {}, stats, ec) && ec,
          "a missing source fails with an error");
}

void test_nothing_to_mirror(const fs::path& target)
{
    std::printf("test_nothing_to_mirror\n");
    const auto source = get_temp_folder() / "unfiltered_source";
    write_text(source / "a.txt", "not a library");
    const auto only_libraries = [](const fs::path& file)
    {
        return file.extension() == ".dll";
    };
    const auto stats = mirror(source, target, only_libraries);
    fs::error_code ec;
    check(stats.copied == 0 && stats.removed == 0, "a source with no file the filter takes mirrors nothing");
    check(!fs::exists(target, ec), "and creates no target folder");
}

void test_same_content(const fs::path& folder)
{
    std::printf("test_same_content\n");
    // Longer than the block two files are compared by, with the difference in the last block.
    constexpr std::size_t content_size = 150 * 1024;
    const std::string content(content_size, 'a');
    auto changed_end = content;
    changed_end.back() = 'b';
    write_text(folder / "a.bin", content);
    write_text(folder / "same.bin", content);
    write_text(folder / "changed.bin", changed_end);
    write_text(folder / "shorter.bin", content.substr(0, content_size - 1));
    fs::error_code ec;
    check(fs::have_same_content(folder / "a.bin", folder / "same.bin", ec) && !ec, "two files of the same bytes are the same");
    check(!fs::have_same_content(folder / "a.bin", folder / "changed.bin", ec) && !ec, "a file whose last byte differs is not");
    check(!fs::have_same_content(folder / "a.bin", folder / "shorter.bin", ec) && !ec, "nor is a shorter file");
    check(!fs::have_same_content(folder / "a.bin", folder / "missing.bin", ec) && ec, "a missing file fails with an error");
}

void test_unlisted_files(const fs::path& root)
{
    std::printf("test_unlisted_files\n");
    for(const auto* name : {"kept.txt", "old/gone.txt", "old/deep/gone.txt", "shared/kept.txt", "shared/gone.txt", "own/save.txt"})
    {
        write_text(root / name, name);
    }
    write_text(root.parent_path() / "outside.txt", "outside");
    const std::vector<std::string> earlier{"kept.txt", "old/gone.txt", "old/deep/gone.txt", "shared/kept.txt",
                                           "shared/gone.txt", "never_there.txt", "../outside.txt"};
    const std::vector<std::string> kept{"kept.txt", "shared/kept.txt", "new.txt"};
    fs::error_code ec;
    check(fs::remove_unlisted_files(root, earlier, kept, ec) == 3 && !ec, "the files only the earlier list names are removed");
    check(!fs::exists(root / "old", ec), "with the folders they leave empty");
    check(read_text(root / "kept.txt") == "kept.txt" && read_text(root / "shared/kept.txt") == "shared/kept.txt",
          "the files the new list names stay");
    check(!fs::exists(root / "shared/gone.txt", ec) && fs::exists(root / "shared", ec), "a folder that still holds a file stays");
    check(read_text(root / "own/save.txt") == "own/save.txt", "a file no list names is not touched");
    check(read_text(root.parent_path() / "outside.txt") == "outside", "a name that leads out of the folder is skipped");
}

void test_shared_read_file(const fs::path& folder)
{
    std::printf("test_shared_read_file\n");
    const std::string first = "the file as it was opened";
    write_text(folder / "served.txt", first);
    write_text(folder / "next.txt", "the file a deploy replaces it with");
    fs::shared_read_file file;
    check(!file.is_open() && !file.open(folder / "missing.txt"), "a missing file does not open");
    check(file.open(folder / "served.txt") && file.size() == first.size(), "a file opens, and knows its size");
    std::string head(4, '\0');
    check(file.read(0, head.data(), head.size()) == head.size() && head == first.substr(0, 4), "it reads from its start");
    std::string tail(100, '\0');
    const auto tail_offset = first.size() - 6;
    check(file.read(tail_offset, tail.data(), tail.size()) == 6 && tail.substr(0, 6) == first.substr(tail_offset),
          "a read at its end gives the bytes that are left");
    check(file.read(first.size(), tail.data(), tail.size()) == 0, "a read past its end gives none");
    // What a deploy does to a file of the site it updates, while a browser still downloads it.
    fs::error_code ec;
    bool is_copied = false;
    const bool is_replaced = fs::copy_file_if_changed(folder / "next.txt", folder / "served.txt", is_copied, ec);
    check(is_replaced && is_copied, "the open file is replaced: " + ec.message());
    check(read_text(folder / "served.txt") == "the file a deploy replaces it with", "its name is the new file's");
    std::string whole(first.size(), '\0');
    check(file.read(0, whole.data(), whole.size()) == whole.size() && whole == first, "and it still reads the file it opened");
    fs::shared_read_file removed;
    check(removed.open(folder / "next.txt") && fs::remove(folder / "next.txt", ec) && !ec, "an open file is removed: " + ec.message());
    removed.close();
    check(!removed.is_open() && !fs::exists(folder / "next.txt", ec), "and is gone once it is closed");
}

void test_rename_over(const fs::path& folder)
{
    std::printf("test_rename_over\n");
    write_text(folder / "old.txt", "old");
    write_text(folder / "new.txt", "new");
    fs::error_code ec;
    {
        // A plain open, as a virus scanner or a file watcher makes of a file just written: on Windows it keeps the
        // file from being replaced until it is closed.
        std::ifstream holder(folder / "old.txt", std::ios::binary);
        std::thread release(
            [&holder]()
            {
                std::this_thread::sleep_for(HOLD_TIME);
                holder.close();
            });
        fs::rename_over(folder / "new.txt", folder / "old.txt", ec);
        release.join();
    }
    check(!ec && read_text(folder / "old.txt") == "new" && !fs::exists(folder / "new.txt"),
          "a file another program holds for a moment is replaced once it lets go: " + ec.message());
#ifdef _WIN32
    write_text(folder / "newer.txt", "newer");
    {
        std::ifstream holder(folder / "old.txt", std::ios::binary);
        fs::rename_over(folder / "newer.txt", folder / "old.txt", ec);
        check(bool(ec) && fs::exists(folder / "newer.txt"), "a file another program keeps holding is not replaced, and the error says so");
    }
    check(read_text(folder / "old.txt") == "new", "and it holds what it held");
#endif
    const auto begin = std::chrono::steady_clock::now();
    fs::rename_over(folder / "missing.txt", folder / "old.txt", ec);
    check(bool(ec) && std::chrono::steady_clock::now() - begin < HOLD_TIME, "a missing file fails at once");
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
    test_case_only_rename(get_temp_folder() / "case_target");
    test_removal_failure(get_temp_folder() / "locked_target");
    test_missing_source(target);
    test_nothing_to_mirror(get_temp_folder() / "unmade_target");
    test_same_content(get_temp_folder() / "compared");
    test_unlisted_files(get_temp_folder() / "listed" / "root");
    test_shared_read_file(get_temp_folder() / "shared");
    test_rename_over(get_temp_folder() / "renamed");
    fs::remove_all(get_temp_folder(), ec);
    std::printf("fs mirror: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("fs mirror", run_fs_mirror_suite)
