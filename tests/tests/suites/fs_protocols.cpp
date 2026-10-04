/*
 * Validation suite for path protocols (fs::add_path_protocol / resolve_protocol / convert_to_protocol).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "fs protocols"
 *
 * convert_to_protocol maps an absolute path back to "protocol:/relative". The editor derives every asset key
 * from it, and a compiled file whose key does not map back is treated as an orphan and deleted, so a root that
 * fails to match costs data. These pin:
 *   - a root registered in a different letter case than the folder on disk still matches (case-insensitive
 *     filesystems only), as when the executable is launched through a differently cased path;
 *   - a root spelled with ".." or a trailing separator matches;
 *   - a root only matches whole folder names: "Data" is not a prefix of "DataExtra";
 *   - the longest matching root wins;
 *   - a path that does not exist yet converts, and resolve_protocol maps the result back to the same file.
 *
 * Environment: UNRAVEL_PROTOCOL_TEST_DIR overrides the scratch folder. Every run writes into a fresh subfolder
 * and nothing is deleted; the protocols it registers are removed again.
 */

#include "../tests.h"

#include <filesystem/filesystem.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace unravel;

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

void check_equal(const std::string& actual, const std::string& expected, const std::string& what)
{
    ++g_checks;
    if(actual != expected)
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %s, expected %s)\n", what.c_str(), actual.c_str(), expected.c_str());
    }
}

auto get_run_root() -> fs::path
{
    const char* configured = std::getenv("UNRAVEL_PROTOCOL_TEST_DIR");
    const fs::path base = configured ? fs::path(configured) : fs::temp_directory_path() / "unravel_protocol_tests";
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const fs::path root = base / ("run_" + std::to_string(stamp));
    fs::error_code err;
    fs::create_directories(root, err);
    return fs::canonical(root, err);
}

void write_file(const fs::path& path)
{
    fs::error_code err;
    fs::create_directories(path.parent_path(), err);
    std::ofstream stream(path);
    stream << "protocol test";
}

/// The same path with the case of every ASCII letter flipped.
auto flip_case(const fs::path& path) -> fs::path
{
    std::string text = path.string();
    for(auto& c : text)
    {
        if(c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
        else if(c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

/// Registers protocols for the duration of a test and removes them again.
class scoped_protocols
{
public:
    ~scoped_protocols()
    {
        for(const auto& name : names_)
        {
            fs::get_path_protocols().erase(name);
        }
    }

    void add(const std::string& name, const fs::path& root)
    {
        fs::add_path_protocol(name, root);
        names_.push_back(name);
    }

private:
    std::vector<std::string> names_;
};

auto converted(const fs::path& path) -> std::string
{
    return fs::convert_to_protocol(path).generic_string();
}

auto is_case_insensitive_filesystem(const fs::path& existing) -> bool
{
    fs::error_code err;
    return fs::equivalent(existing, flip_case(existing), err) && !err;
}

void test_differently_cased_root(const fs::path& run_root)
{
    std::printf("test_differently_cased_root\n");
    const fs::path root = run_root / "Cased" / "Root";
    write_file(root / "Sub" / "File.txt");
    if(!is_case_insensitive_filesystem(root))
    {
        std::printf("  skipped: the filesystem is case-sensitive\n");
        return;
    }
    scoped_protocols protocols;
    protocols.add("casetest", flip_case(root));
    check_equal(converted(root / "Sub" / "File.txt"),
                "casetest:/Sub/File.txt",
                "a root registered in another letter case matches the on-disk path");
    check_equal(converted(flip_case(root / "Sub" / "File.txt")),
                "casetest:/Sub/File.txt",
                "a path spelled in another letter case converts to the on-disk spelling");
    fs::error_code err;
    check(fs::exists(fs::resolve_protocol("casetest:/Sub/File.txt"), err), "the converted path resolves to the file");
}

void test_root_spellings(const fs::path& run_root)
{
    std::printf("test_root_spellings\n");
    const fs::path root = run_root / "Spellings" / "Root";
    write_file(root / "a.txt");
    scoped_protocols protocols;
    protocols.add("dotdot", run_root / "Spellings" / "Other" / ".." / "Root");
    check_equal(converted(root / "a.txt"), "dotdot:/a.txt", "a root spelled with .. matches");
    fs::get_path_protocols().erase("dotdot");
    protocols.add("trailing", root / "");
    check_equal(converted(root / "a.txt"), "trailing:/a.txt", "a root with a trailing separator matches");
}

void test_whole_folder_names(const fs::path& run_root)
{
    std::printf("test_whole_folder_names\n");
    const fs::path root = run_root / "Boundary" / "Data";
    const fs::path sibling_file = run_root / "Boundary" / "DataExtra" / "b.txt";
    write_file(root / "a.txt");
    write_file(sibling_file);
    scoped_protocols protocols;
    protocols.add("boundary", root);
    check_equal(converted(root / "a.txt"), "boundary:/a.txt", "a file under the root converts");
    check(converted(sibling_file).rfind("boundary:", 0) != 0, "a sibling folder sharing the root's prefix does not convert");
}

void test_longest_root_wins(const fs::path& run_root)
{
    std::printf("test_longest_root_wins\n");
    const fs::path outer = run_root / "Nest";
    const fs::path inner = outer / "Inner";
    write_file(inner / "c.txt");
    write_file(outer / "d.txt");
    scoped_protocols protocols;
    protocols.add("outer", outer);
    protocols.add("inner", inner);
    check_equal(converted(inner / "c.txt"), "inner:/c.txt", "the longest matching root wins");
    check_equal(converted(outer / "d.txt"), "outer:/d.txt", "a file only under the outer root converts to it");
}

void test_missing_file_round_trip(const fs::path& run_root)
{
    std::printf("test_missing_file_round_trip\n");
    const fs::path root = run_root / "RoundTrip";
    write_file(root / "present.txt");
    scoped_protocols protocols;
    protocols.add("roundtrip", root);
    const fs::path missing = root / "not" / "yet" / "there.txt";
    const auto key = converted(missing);
    check_equal(key, "roundtrip:/not/yet/there.txt", "a path that does not exist yet converts");
    fs::error_code err;
    check(fs::weakly_canonical(fs::resolve_protocol(key), err) == fs::weakly_canonical(missing, err),
          "resolve_protocol maps the converted path back to the same file");
}

auto run_fs_protocols_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const fs::path run_root = get_run_root();
    std::printf("scratch folder %s\n", run_root.string().c_str());
    test_differently_cased_root(run_root);
    test_root_spellings(run_root);
    test_whole_folder_names(run_root);
    test_longest_root_wins(run_root);
    test_missing_file_round_trip(run_root);
    std::printf("fs protocols: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("fs protocols", run_fs_protocols_suite)
