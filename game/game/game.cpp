#include "game.h"

#include <engine/engine.h>
#include <engine/loading_screen.h>
#include <engine/play_mode.h>
#include <engine/rendering/display_manager.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/renderer.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/assets/asset_manager.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/settings/boot_config.h>
#include <engine/settings/settings.h>
#include <engine/rendering/feature_limits.h>
#include "bundled_files.h"
#include "runner/runner.h"

#include <base/platform/config.hpp>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <filesystem/filesystem.h>
#include <filesystem/pack.h>
#include <hpp/string_view.hpp>
#include <logging/logging.h>
#include <ospp/bundle.h>
#include <ospp/hints.h>
#include <ospp/message_box.h>
#include <reflection/registration.h>

#include <algorithm>
#include <array>
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
/// The option that shows one of the pipeline's debug views, and its value for the lit image.
constexpr const char* DEBUG_PASS_OPTION = "debug-pass";
constexpr int NO_DEBUG_PASS = -1;

/// The option that turns graphics features off over the project's settings, and what separates their names.
constexpr const char* GRAPHICS_OFF_OPTION = "graphics-off";
constexpr char GRAPHICS_OFF_SEPARATOR = ',';

#if UNRAVEL_PLATFORM_EMSCRIPTEN
/// The option the page names the packs with that it downloaded straight into the player's memory, by their paths
/// in the application's folder (fs::parse_host_memory_files).
constexpr const char* MEMORY_FILES_OPTION = "memory-files";
#endif

/// What a failed start tells the player, as far as the start got: the game's name, and the folder of its log.
struct start_report
{
    std::string title{"Unravel Game"};
    std::string log_folder;
};

auto get_start_report() -> start_report&
{
    static start_report report;
    return report;
}

/// Fails the start with what went wrong, which the player is shown.
void fail_start(const std::string& message)
{
    engine::context().get_cached<loading_screen>().fail(message);
}

/// Shows why the game did not start: its window never appears, and the log is in a folder a player does not look
/// in. A browser page reports a failed start itself.
void show_start_failure(const std::string& message)
{
#if !UNRAVEL_PLATFORM_EMSCRIPTEN
    if(engine::is_interrupted())
    {
        return;
    }
    const auto& report = get_start_report();
    const auto log_note = report.log_folder.empty() ? std::string{} : fmt::format("\n\nThe log is in {}.", report.log_folder);
    os::message_box::show(os::message_box::kind::error,
                          report.title,
                          fmt::format("The game could not start.\n\n{}{}", message, log_note));
#else
    (void)message;
#endif
}

/// A feature --graphics-off can turn off: its name and the limit that turns it off.
struct graphics_off_switch
{
    hpp::string_view name;
    void (*turn_off)(feature_limits& limits);
};

constexpr std::array GRAPHICS_OFF_SWITCHES = {
    graphics_off_switch{"gi", [](feature_limits& limits) { limits.global_illumination = false; }},
    graphics_off_switch{"ssr", [](feature_limits& limits) { limits.screen_space_reflections = false; }},
    graphics_off_switch{"ssil", [](feature_limits& limits) { limits.screen_space_indirect_lighting = false; }},
    graphics_off_switch{"ao", [](feature_limits& limits) { limits.ambient_occlusion = false; }},
    graphics_off_switch{"bloom", [](feature_limits& limits) { limits.bloom = false; }},
    graphics_off_switch{"fog", [](feature_limits& limits) { limits.volumetric_fog = false; }},
    graphics_off_switch{"taa", [](feature_limits& limits) { limits.max_anti_aliasing = anti_aliasing_limit::fxaa; }},
    graphics_off_switch{"aa", [](feature_limits& limits) { limits.max_anti_aliasing = anti_aliasing_limit::none; }},
    graphics_off_switch{"soft_shadows", [](feature_limits& limits) { limits.soft_shadows = false; }},
    graphics_off_switch{"contact_shadows", [](feature_limits& limits) { limits.contact_shadows = false; }},
    graphics_off_switch{"subsurface", [](feature_limits& limits) { limits.subsurface = false; }},
    graphics_off_switch{"clouds", [](feature_limits& limits) { limits.volumetric_clouds = false; }},
    graphics_off_switch{"detailed_indirect", [](feature_limits& limits) { limits.detailed_indirect_lighting = false; }},
    graphics_off_switch{"full_precision", [](feature_limits& limits) { limits.full_precision_light_targets = false; }},
};

/// Shows the debug view --debug-pass names in every camera; the lit image without it.
void apply_debug_pass(rtti::context& ctx, const cmd_line::parser& parser)
{
    int pass = NO_DEBUG_PASS;
    if(!parser.try_get(DEBUG_PASS_OPTION, pass) || pass == NO_DEBUG_PASS)
    {
        return;
    }
    APPLOG_INFO("Showing debug pass {} (--{}).", pass, DEBUG_PASS_OPTION);
    ctx.get_cached<rendering_system>().set_debug_pass(pass);
}

/// Turns off one feature by its --graphics-off name; false for a name no switch has.
auto turn_off_graphics_feature(feature_limits& limits, hpp::string_view name) -> bool
{
    const auto found = std::find_if(GRAPHICS_OFF_SWITCHES.begin(),
                                    GRAPHICS_OFF_SWITCHES.end(),
                                    [&](const graphics_off_switch& entry) { return entry.name == name; });
    if(found == GRAPHICS_OFF_SWITCHES.end())
    {
        return false;
    }
    found->turn_off(limits);
    return true;
}

/// Turns off the features --graphics-off names, for this run only: the renderer reads the settings' limits every
/// frame.
void apply_graphics_off(rtti::context& ctx, const cmd_line::parser& parser)
{
    std::string names;
    if(!parser.try_get(GRAPHICS_OFF_OPTION, names) || names.empty())
    {
        return;
    }
    auto& limits = ctx.get<settings>().graphics.limits;
    hpp::string_view rest = names;
    while(!rest.empty())
    {
        const auto end = std::min(rest.find(GRAPHICS_OFF_SEPARATOR), rest.size());
        const auto name = rest.substr(0, end);
        rest = rest.substr(std::min(end + 1, rest.size()));
        if(turn_off_graphics_feature(limits, name))
        {
            APPLOG_INFO("Graphics feature {} is off (--{}).", name, GRAPHICS_OFF_OPTION);
        }
        else
        {
            APPLOG_WARNING("--{} names no graphics feature {}.", GRAPHICS_OFF_OPTION, name);
        }
    }
}

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

#if UNRAVEL_PLATFORM_EMSCRIPTEN
/// Names the packs the page holds in the player's memory, so mounting one maps those bytes where it would
/// otherwise copy a file of the browser's memory file system.
void add_page_memory_files(const cmd_line::parser& parser)
{
    std::string listed;
    parser.try_get(MEMORY_FILES_OPTION, listed);
    const fs::path binary_path = fs::resolve_protocol("binary:/");
    for(const auto& file : fs::parse_host_memory_files(listed))
    {
        fs::add_memory_file(binary_path / fs::from_utf8_string(file.name),
                            reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(file.address)),
                            file.size);
    }
}
#endif

/// Whether a pack is at `pack`: a file, or bytes its host named for the path.
auto has_pack(const fs::path& pack) -> bool
{
    fs::error_code ec;
    return fs::has_memory_file(pack) || fs::exists(pack, ec);
}

auto mount_pack_file(const std::string& protocol, const fs::path& pack) -> bool
{
    std::string error;
    if(!fs::mount_pack(protocol, pack, error))
    {
        fail_start(fmt::format("Failed to mount asset pack: {}", error));
        return false;
    }
    APPLOG_INFO("Mounted asset pack {}", pack.generic_string());
#if UNRAVEL_PLATFORM_EMSCRIPTEN
    // A pack that is a file of the browser's memory file system was copied into the module's memory by mapping
    // it. Nothing reads the file again, and dropping it frees its first copy.
    fs::error_code ec;
    fs::remove(pack, ec);
#endif
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
            fail_start(fmt::format("Failed to mount asset pack: {}", error));
            return false;
        }
        APPLOG_INFO("Mounted asset pack {} from the application package", name);
        region = open_bundled_pack(root / ex::get_patch_pack_file_name(level));
    }
    return true;
}

/// Mounts the first of a web build's set packs (ex::get_set_pack_file_name of each of `sets`) found under `root` over
/// the protocol's pack: of the build's sets, the page downloaded only the one it chose.
template<typename Sets>
auto mount_set_pack(const std::string& protocol, const fs::path& root, const Sets& sets) -> bool
{
    for(const auto* set : sets)
    {
        const auto pack = root / ex::get_set_pack_file_name(set);
        if(has_pack(pack))
        {
            return mount_pack_file(protocol, pack);
        }
    }
    return true;
}

/// Mounts the protocol's pack when the deploy wrote one, then its patches in level order, then a web build's texture
/// pack (the set the browser's GPU reads) and shader pack (the set of the graphics API the page started). A deploy
/// without packs ships loose files.
auto mount_asset_packs(const std::string& protocol) -> bool
{
    const auto root = fs::resolve_protocol(protocol + ":/");
    if(os::bundle::is_supported())
    {
        return mount_bundled_packs(protocol, root);
    }
    if(!has_pack(root / ex::get_pack_file_name()))
    {
        return true;
    }
    if(!mount_pack_file(protocol, root / ex::get_pack_file_name()))
    {
        return false;
    }
    for(std::uint32_t level = 1; has_pack(root / ex::get_patch_pack_file_name(level)); ++level)
    {
        if(!mount_pack_file(protocol, root / ex::get_patch_pack_file_name(level)))
        {
            return false;
        }
    }
    return mount_set_pack(protocol, root, ex::TEXTURE_SET_NAMES) && mount_set_pack(protocol, root, ex::SHADER_SET_NAMES);
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
    // Android's Back reaches the game as a key (KeyCode.AcBack), for its scripts to act on: the system's own
    // answer to it closes the game.
    os::set_hint("HINT_ANDROID_TRAP_BACK_BUTTON", "1");

    if(!engine::create(ctx, parser))
    {
        return false;
    }

    ctx.add<runner>(ctx);
    ctx.add<display_manager>(ctx, parser);

    parser.set_optional<std::string>("a", "appdata", "", "Application data directory. Defaults to binary directory.");
#if UNRAVEL_PLATFORM_EMSCRIPTEN
    parser.set_optional<std::string>("",
                                     MEMORY_FILES_OPTION,
                                     "",
                                     "The packs the page holds in the player's memory, as name:address:size items "
                                     "separated by '|'. Set by the page only.");
#endif
    parser.set_optional<bool>("",
                              "log-here",
                              false,
                              "Write the log and the crash log beside the executable, not in the user's data folder.");
    parser.set_optional<int>("",
                             DEBUG_PASS_OPTION,
                             NO_DEBUG_PASS,
                             "Show one of the renderer's debug views (the editor's view ids), for this run only.");
    parser.set_optional<std::string>("",
                                     GRAPHICS_OFF_OPTION,
                                     "",
                                     "Turn graphics features off for this run only, as a comma-separated list (gi, ssr, "
                                     "ssil, ao, bloom, fog, taa, aa, soft_shadows, contact_shadows, subsurface, clouds, "
                                     "detailed_indirect, full_precision).");

    return true;
}

auto game::init(const cmd_line::parser& parser) -> bool
{
    auto& ctx = engine::context();
    auto& ls = ctx.get_cached<loading_screen>();
    ls.set_on_fail([](const std::string&, const std::string& message)
    {
        show_start_failure(message);
    });

    ls.begin_module("Game Files");
    if(!ls.check(extract_bundled_files()))
    {
        return false;
    }
#if UNRAVEL_PLATFORM_EMSCRIPTEN
    add_page_memory_files(parser);
#endif

    if(!ls.check(init_protocols(parser)))
    {
        return false;
    }
    init_user_protocols(parser);
    open_log(parser);

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

    ls.begin_module("Game Assets");
    if(!ls.check(init_assets(ctx)))
    {
        return false;
    }

    // Full settings load now that asset databases can resolve handles.
    ls.begin_module("Game Settings");
    if(!ls.check(init_settings(ctx)))
    {
        return false;
    }

    ls.begin_module("Window");
    if(!ls.check(show_main_window(ctx)))
    {
        return false;
    }

    if(!engine::init_systems(parser))
    {
        return false;
    }
    apply_debug_pass(ctx, parser);
    apply_graphics_off(ctx, parser);

    ls.begin_module("Game");
    if(!ls.check(ctx.get_cached<runner>().init(ctx)))
    {
        return false;
    }

    {
        auto& am = ctx.get_cached<asset_manager>();
        am.preload_all_assets();
    }

    ls.begin_module("Game Scripts");
    auto& scr = ctx.get_cached<script_system>();
    if(!ls.check(scr.load_app_domain(ctx, false)))
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
    get_start_report().title = product;
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
    auto folder = fs::resolve_protocol(LOGS_PATH);
    if(is_beside_binary || !engine::open_log(folder))
    {
        folder = fs::resolve_protocol("binary:/");
        if(!engine::open_log(folder))
        {
            return;
        }
    }
    get_start_report().log_folder = fs::to_utf8_string(folder);
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
    fail_start(fmt::format("The {} asset index {} {}. The build is incomplete or does not match this version of the "
                           "game. Deploy the game again.",
                           protocol,
                           index.generic_string(),
                           fs::is_readable_file(index) ? "could not be read" : "is missing"));
    return false;
}

auto game::init_settings(rtti::context& ctx) -> bool
{
    auto& s = ctx.add<settings>();
    const auto settings_path = fs::resolve_protocol(SETTINGS_PATH);
    if(!load_from_file(settings_path.string(), s))
    {
        fail_start(fmt::format("Failed to load project settings {}", settings_path.string()));
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
