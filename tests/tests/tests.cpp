#include "tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/threading/threader.h>

#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <reflection/reflection.h>
#include <reflection/registration.h>
#include <service/service.h>

#include <map>

#include <entt/core/hashed_string.hpp>
#include <entt/meta/factory.hpp>

namespace unravel
{

namespace
{
int failures{};
bool has_run{};

/// Read during init and kept. The service resets the parser once every module has been
/// initialised, so reading it from process() finds nothing.
std::map<std::string, bool> switches;
std::string suite_filter;

auto suites() -> std::vector<test_suite>&
{
    static std::vector<test_suite> instance;
    return instance;
}
} // namespace

void register_test_suite(test_suite suite)
{
    suites().emplace_back(std::move(suite));
}

auto get_test_suites() -> const std::vector<test_suite>&
{
    return suites();
}

REFLECTION_REGISTRATION
{
    using namespace entt::literals;

    entt::meta_factory<tests>{}
        .type("tests"_hs)
        .func<&tests::create>("create"_hs)
        .func<&tests::init>("init"_hs)
        .func<&tests::deinit>("deinit"_hs)
        .func<&tests::destroy>("destroy"_hs)
        .func<&tests::process>("process"_hs)
        .func<&tests::interrupt>("interrupt"_hs);
}

auto tests::create(rtti::context& ctx, cmd_line::parser& parser) -> bool
{
    if(!engine::create(ctx, parser))
    {
        return false;
    }

    parser.set_optional<std::string>("s", "suite", "", "Run only suites whose name contains this.");
    parser.set_optional<bool>("b", "bench", false, "Run benchmarks as well as correctness checks.");
    parser.set_optional<bool>("bo", "bench-only", false, "Run benchmarks and skip the checks.");
    parser.set_optional<bool>("bp", "binary-probe", false, "Include the binary-archive probe.");

    return true;
}

auto tests::wants(const std::string& option) -> bool
{
    const auto it = switches.find(option);
    return it != switches.end() && it->second;
}

auto tests::init(const cmd_line::parser& parser) -> bool
{
    for(const char* option : {"bench", "bench-only", "binary-probe"})
    {
        bool value = false;
        parser.try_get(option, value);
        switches[option] = value;
    }
    parser.try_get("suite", suite_filter);

    auto& ctx = engine::context();

    // Somewhere for test assets to live. A real directory rather than a fiction, because the
    // asset database keys off the protocol and the suites write compiled prefabs through the
    // same path the editor does.
    const fs::path binary_path = fs::resolve_protocol("binary:/");
    const fs::path app_data = binary_path / "data" / "tests";
    fs::error_code err;
    fs::create_directories(app_data, err);
    fs::add_path_protocol("app", app_data);

    // Threading and assets only. The renderer wants a device and the suites do not want a
    // window; anything that needs one does not belong in here.
    if(!ctx.get_cached<threader>().init(ctx))
    {
        APPLOG_CRITICAL("Failed to initialise threading.");
        return false;
    }

    if(!ctx.get_cached<asset_manager>().init(ctx))
    {
        APPLOG_CRITICAL("Failed to initialise the asset manager.");
        return false;
    }

    return true;
}

auto tests::process() -> int
{
    if(has_run)
    {
        return SERVICE_RESULT_EXIT;
    }
    has_run = true;

    auto& ctx = engine::context();

    for(const auto& suite : get_test_suites())
    {
        if(!suite_filter.empty() && suite.name.find(suite_filter) == std::string::npos)
        {
            continue;
        }

        APPLOG_INFO("=== suite: {} ===", suite.name);
        failures += suite.run(ctx);
    }

    if(failures == 0)
    {
        APPLOG_INFO("All suites passed.");
    }
    else
    {
        APPLOG_ERROR("{} failing check(s).", failures);
    }

    return SERVICE_RESULT_EXIT;
}

auto tests::deinit() -> bool
{
    // The two that were initialised, innermost first: assets hand work to the thread pool.
    auto& ctx = engine::context();
    ctx.get_cached<asset_manager>().deinit(ctx);
    ctx.get_cached<threader>().deinit(ctx);
    return true;
}

auto tests::destroy() -> bool
{
    // Deliberately not engine::destroy(). It tears down every service the engine creates,
    // and this runner starts almost none of them - the renderer, audio, scripting and the
    // ECS systems were never initialised, and taking them down unwinds through state that
    // was never built. The process is about to exit either way.
    return true;
}

auto tests::interrupt() -> bool
{
    return true;
}

auto tests::failure_count() -> int
{
    return failures;
}

} // namespace unravel
