/*
 * Validation suite for the user:/ and cache:/ protocols.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "user protocols"
 *
 * engine::create points both at folders of binary:/. engine::set_user_protocols moves them to the
 * per-user folders named after a game's company and product: the game's saves (Application.persistentDataPath)
 * and its rebuildable files, such as the renderer's pipeline cache. These pin:
 *   - before a game names them, both stay inside binary:/;
 *   - user:/ is the product folder inside the company folder, and both folders exist;
 *   - an empty company leaves the company level out;
 *   - cache:/ is a "cache" folder inside user:/ and exists.
 *
 * The suite creates its folders in the user's application data and removes them again; the protocols
 * it moves are put back.
 */

#include "../tests.h"

#include <engine/engine.h>
#include <filesystem/filesystem.h>

#include <cstdio>
#include <string>

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

void check_equal(const fs::path& actual, const fs::path& expected, const std::string& what)
{
    ++g_checks;
    if(actual.lexically_normal() != expected.lexically_normal())
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %s, expected %s)\n",
                    what.c_str(),
                    actual.generic_string().c_str(),
                    expected.generic_string().c_str());
    }
}

constexpr const char* TEST_COMPANY = "UnravelTestsCompany";
constexpr const char* TEST_PRODUCT = "UnravelTestsProduct";
constexpr const char* TEST_PRODUCT_WITHOUT_COMPANY = "UnravelTestsProductWithoutCompany";

/// A protocol's folder without the trailing separator resolve_protocol leaves on a bare root.
auto resolve_folder(const std::string& protocol_root) -> fs::path
{
    fs::path folder = fs::resolve_protocol(protocol_root).lexically_normal();
    return folder.has_filename() ? folder : folder.parent_path();
}

void test_defaults_in_binary()
{
    check_equal(resolve_folder("user:/"), resolve_folder("binary:/user"), "user:/ starts as binary:/user");
    check_equal(resolve_folder("cache:/"), resolve_folder("binary:/.cache"), "cache:/ starts as binary:/.cache");
}

/// Names the folders after the test company and product; returns the company folder to remove afterwards.
auto test_named_folders() -> fs::path
{
    engine::set_user_protocols(TEST_COMPANY, TEST_PRODUCT);
    const fs::path user = resolve_folder("user:/");
    const fs::path cache = resolve_folder("cache:/");
    fs::error_code err;
    check(user.filename() == TEST_PRODUCT, "user:/ is the product folder");
    check(user.parent_path().filename() == TEST_COMPANY, "the product folder is inside the company folder");
    check(fs::is_directory(user, err), "the user folder exists");
    check_equal(cache, user / "cache", "cache:/ is the cache folder inside user:/");
    check(fs::is_directory(cache, err), "the cache folder exists");
    return user.parent_path();
}

/// Names the folders after a product alone; returns the product folder to remove afterwards.
auto test_empty_company(const fs::path& company_folder) -> fs::path
{
    engine::set_user_protocols({}, TEST_PRODUCT_WITHOUT_COMPANY);
    const fs::path user = resolve_folder("user:/");
    check(user.filename() == TEST_PRODUCT_WITHOUT_COMPANY, "without a company user:/ is still the product folder");
    check_equal(user.parent_path(),
                company_folder.parent_path(),
                "without a company the product folder sits where company folders do");
    return user;
}

/// Removes a folder the suite created; only one with the name the suite gave it, since a failed
/// set_user_protocols leaves user:/ inside binary:/.
void remove_test_folder(const fs::path& folder, const char* name)
{
    if(folder.filename() != name)
    {
        return;
    }
    fs::error_code err;
    fs::remove_all(folder, err);
}

} // namespace

auto run_user_protocols_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const fs::path initial_user = resolve_folder("user:/");
    const fs::path initial_cache = resolve_folder("cache:/");
    test_defaults_in_binary();
    const fs::path company_folder = test_named_folders();
    const fs::path product_folder = test_empty_company(company_folder);
    fs::add_path_protocol("user", initial_user);
    fs::add_path_protocol("cache", initial_cache);
    remove_test_folder(company_folder, TEST_COMPANY);
    remove_test_folder(product_folder, TEST_PRODUCT_WITHOUT_COMPANY);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("user protocols", run_user_protocols_suite)
