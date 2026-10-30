#include "game.h"

#include <engine/engine.h>
#include <engine/play_mode.h>
#include <engine/rendering/display_manager.h>
#include <engine/rendering/renderer.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/assets/asset_manager.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/settings/boot_config.h>
#include "bundled_files.h"
#include "runner/runner.h"

#include <base/platform/config.hpp>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <filesystem/filesystem.h>
#include <filesystem/pack.h>
#include <logging/logging.h>
#include <ospp/bundle.h>
#include <reflection/registration.h>

#include <fstream>

namespace unravel
{
namespace
{
constexpr const char* SETTINGS_PATH = "app:/settings/settings.cfg";
/// The folder of the log and the crash log. The install folder may not be writable (a Steam library of another
/// user, a read-only drive), and an update or a file check treats files there as foreign.
constexpr const char* LOGS_PATH = "user:/logs";
/// Bytes of an RGBA8 texel.
constexpr std::size_t RGBA8_TEXEL_SIZE = 4;

/// Shows the icon a deploy puts beside the game where the executable carries none (Linux); nothing without one.
void set_window_icon(os::window& window)
{
    const auto file = fs::resolve_protocol("binary:/" + ex::get_window_icon_file_name());
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        return;
    }
    const auto bytes = fs::read_stream(stream);
    bx::DefaultAllocator allocator;
    bimg::ImageContainer* container =
        bimg::imageParse(&allocator, bytes.data(), static_cast<std::uint32_t>(bytes.size()), bimg::TextureFormat::RGBA8);
    if(container == nullptr)
    {
        APPLOG_WARNING("The window icon {} could not be read as an image.", file.generic_string());
        return;
    }
    os::image icon;
    bimg::ImageMip mip;
    if(bimg::imageGetRawData(*container, 0, 0, container->m_data, container->m_size, mip))
    {
        icon.size = {mip.m_width, mip.m_height};
        icon.pixels.assign(mip.m_data, mip.m_data + std::size_t(mip.m_width) * mip.m_height * RGBA8_TEXEL_SIZE);
    }
    bimg::imageFree(container);
    if(!icon.pixels.empty())
    {
        window.set_icon(icon);
    }
}

auto mount_pack_file(const std::string& protocol, const fs::path& pack) -> bool
{
    std::string error;
    if(!fs::mount_pack(protocol, pack, error))
    {
        APPLOG_CRITICAL("Failed to mount asset pack: {}", error);
        return false;
    }
    APPLOG_INFO("Mounted asset pack {}", pack.generic_string());
    return true;
}

/// A pack in the application's package, as a region of the package file; descriptor -1 when absent.
auto open_bundled_pack(const fs::path& pack) -> fs::file_region
{
    const std::string name = get_bundled_name(pack);
    const auto region = os::bundle::open_region(name);
    return {region.descriptor, region.offset, region.size, name};
}

/// mount_asset_packs for packs inside the application's package, which are mapped in place there.
auto mount_bundled_packs(const std::string& protocol, const fs::path& root) -> bool
{
    auto region = open_bundled_pack(root / ex::get_pack_file_name());
    for(std::uint32_t level = 1; region.descriptor >= 0; ++level)
    {
        const std::string name = region.name;
        std::string error;
        if(!fs::mount_pack(protocol, std::move(region), error))
        {
            APPLOG_CRITICAL("Failed to mount asset pack: {}", error);
            return false;
        }
        APPLOG_INFO("Mounted asset pack {} from the application package", name);
        region = open_bundled_pack(root / ex::get_patch_pack_file_name(level));
    }
    return true;
}

/// Mounts the protocol's pack when the deploy wrote one, then its patches in level order. A deploy
/// without packs ships loose files.
auto mount_asset_packs(const std::string& protocol) -> bool
{
    const auto root = fs::resolve_protocol(protocol + ":/");
    if(os::bundle::is_supported())
    {
        return mount_bundled_packs(protocol, root);
    }
    fs::error_code ec;
    if(!fs::exists(root / ex::get_pack_file_name(), ec))
    {
        return true;
    }
    if(!mount_pack_file(protocol, root / ex::get_pack_file_name()))
    {
        return false;
    }
    for(std::uint32_t level = 1; fs::exists(root / ex::get_patch_pack_file_name(level), ec); ++level)
    {
        if(!mount_pack_file(protocol, root / ex::get_patch_pack_file_name(level)))
        {
            return false;
        }
    }
    return true;
}
} // namespace

REFLECTION_REGISTRATION
{
    entt::meta_factory<game>{}
        .type("game"_hs)
        .func<&game::create>("create"_hs)
        .func<&game::init>("init"_hs)
        .func<&game::deinit>("deinit"_hs)
        .func<&game::destroy>("destroy"_hs)
        .func<&game::process>("process"_hs)
        .func<&game::interrupt>("interrupt"_hs);
}

auto game::create(rtti::context& ctx, cmd_line::parser& parser) -> bool
{
    ctx.add<deploy>();

    if(!engine::create(ctx, parser))
    {
        return false;
    }

    ctx.add<runner>(ctx);
    ctx.add<display_manager>(ctx, parser);

    parser.set_optional<std::string>("a", "appdata", "", "Application data directory. Defaults to binary directory.");
    parser.set_optional<bool>("",
                              "log-here",
                              false,
                              "Write the log and the crash log beside the executable, not in the user's data folder.");

    return true;
}

auto game::init(const cmd_line::parser& parser) -> bool
{
    if(!extract_bundled_files())
    {
        return false;
    }

    if(!init_protocols(parser))
    {
        return false;
    }
    init_user_protocols(parser);
    open_log(parser);

    auto& ctx = engine::context();

    // Peek settings for cold boot fields only: nothing is initialized yet, so the peek
    // must not visit the asset handles inside settings.cfg. The full load happens below,
    // once the asset databases can resolve them.
    if(!prepare_boot_config(ctx, parser))
    {
        return false;
    }

    if(!engine::init_core(parser,
                          [&ctx, &parser]() -> bool
                          {
                              return create_main_window(ctx, parser);
                          }))
    {
        return false;
    }

    if(!init_assets(ctx))
    {
        return false;
    }

    // Full settings load now that asset databases can resolve handles.
    if(!init_settings(ctx))
    {
        return false;
    }

    if(!show_main_window(ctx))
    {
        return false;
    }

    if(!engine::init_systems(parser))
    {
        return false;
    }

    if(!ctx.get_cached<runner>().init(ctx))
    {
        return false;
    }

    {
        auto& am = ctx.get_cached<asset_manager>();
        am.preload_all_assets();
    }

    auto& scr = ctx.get_cached<script_system>();
    if(!scr.load_app_domain(ctx, false))
    {
        return false;
    }

    auto& play = ctx.get_cached<play_mode>();
    play.set_active(ctx, true);

    return true;
}

auto game::init_protocols(const cmd_line::parser& parser) -> bool
{
    std::string appdata;
    parser.try_get("appdata", appdata);
    if(!appdata.empty())
    {
        fs::path app_data = appdata;
        fs::add_path_protocol("engine", app_data / "engine");
        fs::add_path_protocol("app", app_data / "app");
    }
    else
    {
        fs::path binary_path = fs::resolve_protocol("binary:/");
        fs::path app_data = binary_path / "data" / "app";
        fs::add_path_protocol("app", app_data);
    }
    return true;
}

void game::init_user_protocols(const cmd_line::parser& parser)
{
    settings::app_settings app{};
    load_app_section_from_file(fs::resolve_protocol(SETTINGS_PATH).string(), app);
    const std::string product =
        app.product.empty() ? fs::executable_path(parser.app_name().c_str()).stem().string() : app.product;
    engine::set_user_protocols(app.company, product);
}

void game::open_log(const cmd_line::parser& parser)
{
    bool is_beside_binary = false;
    parser.try_get("log-here", is_beside_binary);
#if UNRAVEL_PLATFORM_ANDROID
    // binary:/ is the application's own storage there, outside the folder its backup rules keep.
    is_beside_binary = true;
#endif
    if(is_beside_binary || !engine::open_log(fs::resolve_protocol(LOGS_PATH)))
    {
        engine::open_log(fs::resolve_protocol("binary:/"));
    }
}

auto game::prepare_boot_config(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    const fs::path app_root = fs::resolve_protocol("app:/");
    const boot_config project_hint = peek_project_boot_config(app_root);
    const boot_config resolved = resolve_boot_config(parser, project_hint);
    if(ctx.has<boot_config>())
    {
        ctx.get<boot_config>() = resolved;
    }
    else
    {
        ctx.add<boot_config>(resolved);
    }
    APPLOG_INFO("Resolved boot config: renderer={} (cli={}) physics={} (cli={})",
                preferred_renderer_to_string(resolved.renderer),
                resolved.cli.renderer,
                physics_backend_to_string(resolved.physics),
                resolved.cli.physics);
    return true;
}

/// Loads a protocol's asset index. Without a readable one the build is incomplete or does not match the
/// game, so the log names the index, says what is wrong with it and how to fix it.
auto load_asset_index(asset_manager& am, const std::string& protocol) -> bool
{
    if(am.load_database(protocol + ":/"))
    {
        return true;
    }
    const auto index = fs::resolve_protocol(protocol + ":/" + ex::get_asset_index_file_name());
    APPLOG_CRITICAL("The {} asset index {} {}. The build is incomplete or does not match this version of the "
                    "game. Deploy the game again.",
                    protocol,
                    index.generic_string(),
                    fs::is_readable_file(index) ? "could not be read" : "is missing");
    return false;
}

auto game::init_settings(rtti::context& ctx) -> bool
{
    auto& s = ctx.add<settings>();
    const auto settings_path = fs::resolve_protocol(SETTINGS_PATH);
    if(!load_from_file(settings_path.string(), s))
    {
        APPLOG_CRITICAL("Failed to load project settings {}", settings_path.string());
        return false;
    }
    return true;
}

auto game::init_assets(rtti::context& ctx) -> bool
{
    // Before the databases: their index is inside the packs.
    if(!mount_asset_packs("engine") || !mount_asset_packs("app"))
    {
        return false;
    }
    auto& am = ctx.get_cached<asset_manager>();
    return load_asset_index(am, "engine") && load_asset_index(am, "app");
}

auto game::create_main_window(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    // Settings and the backend are not loaded yet; show_main_window sets the final title.
    return ctx.get_cached<display_manager>().create_main_window(ctx, parser, "Unravel Game");
}

auto game::show_main_window(rtti::context& ctx) -> bool
{
    auto* main_window = ctx.get_cached<renderer>().get_main_window();
    if(!main_window)
    {
        return false;
    }
    const auto& s = ctx.get<settings>();
    auto title = fmt::format("Unravel Game <{}>", bgfx::getRendererName(bgfx::getRendererType()));
    if(!s.app.product.empty())
    {
        title = fmt::format("{}", s.app.product);
    }
    if(!s.app.version.empty())
    {
        title += fmt::format(" v{}", s.app.version);
    }
    auto& display = ctx.get_cached<display_manager>();
    display.init(ctx);
    display.set_run_in_background(ctx, s.standalone.run_in_background);
    auto& window = main_window->get_window();
    window.set_title(title);
    set_window_icon(window);
    window.show();
    return true;
}


auto game::deinit() -> bool
{
    auto& ctx = engine::context();

    if(!ctx.get_cached<display_manager>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<runner>().deinit(ctx))
    {
        return false;
    }

    return engine::deinit();
}

auto game::destroy() -> bool
{
    auto& ctx = engine::context();

    ctx.remove<settings>();
    ctx.remove<display_manager>();
    ctx.remove<runner>();
    ctx.remove<deploy>();

    return engine::destroy();
}

auto game::process() -> int
{
    return engine::process();
}

auto game::interrupt() -> bool
{
    return engine::interrupt();
}
} // namespace unravel
