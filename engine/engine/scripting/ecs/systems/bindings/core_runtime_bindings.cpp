#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/events.h>
#include <engine/play_mode.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/display_manager.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/renderer.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>
#include <seq/seq.h>
#include <simulation/simulation.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <iterator>

namespace unravel
{
namespace
{

void internal_m2n_log_trace(const std::string& message, const std::string& func, const std::string& file, int line)
{
    APPLOG_TRACE_LOC(file.c_str(), line, func.c_str(), message);
}

void internal_m2n_log_info(const std::string& message, const std::string& func, const std::string& file, int line)
{
    APPLOG_INFO_LOC(file.c_str(), line, func.c_str(), message);
}

void internal_m2n_log_warning(const std::string& message, const std::string& func, const std::string& file, int line)
{
    APPLOG_WARNING_LOC(file.c_str(), line, func.c_str(), message);
}

void internal_m2n_log_error(const std::string& message, const std::string& func, const std::string& file, int line)
{
    APPLOG_ERROR_LOC(file.c_str(), line, func.c_str(), message);
}

//-------------------------------------------------------------------------

void internal_m2n_application_quit()
{
    auto delay = seq::delay(0ms);
    delay.on_end.connect(
        []()
        {
            auto& ctx = engine::context();
            auto& ev = ctx.get_cached<events>();
            ctx.get_cached<play_mode>().set_active(ctx, false);
        });

    seq::queue(delay, "script");
}

/// A protocol's folder as C# paths are written: created when missing, forward slashes, no trailing separator.
auto get_script_folder(const std::string& protocol_root) -> std::string
{
    fs::path folder = fs::resolve_protocol(protocol_root).lexically_normal();
    if(!folder.has_filename())
    {
        folder = folder.parent_path();
    }
    fs::error_code err;
    fs::create_directories(folder, err);
    if(err)
    {
        APPLOG_ERROR("Cannot create {}: {}", folder.generic_string(), err.message());
    }
    return folder.generic_string();
}

auto internal_m2n_application_get_persistent_data_path() -> std::string
{
    return get_script_folder("user:/");
}

auto internal_m2n_application_get_temporary_cache_path() -> std::string
{
    return get_script_folder("cache:/");
}

auto internal_m2n_screen_get_size() -> math::vec2
{
    auto& ctx = engine::context();
    const auto& size = ctx.get_cached<rendering_system>().get_game_view_size();
    return {float(size.width), float(size.height)};
}

/// A rectangle as it crosses to C#: the layout of the C# Rect struct.
struct script_rect
{
    float x{};
    float y{};
    float width{};
    float height{};
};

auto internal_m2n_screen_get_safe_area() -> script_rect
{
    auto& ctx = engine::context();
    const auto& rendering = ctx.get_cached<rendering_system>();
    const auto& size = rendering.get_game_view_size();
    script_rect area{0.0f, 0.0f, float(size.width), float(size.height)};
    if(const auto& device = rendering.get_game_view_device())
    {
        area.x = device->inset_left;
        area.y = device->inset_top;
        area.width = float(size.width) - device->inset_left - device->inset_right;
        area.height = float(size.height) - device->inset_top - device->inset_bottom;
        return area;
    }
    auto* window = ctx.has<renderer>() ? ctx.get_cached<renderer>().get_main_window() : nullptr;
    if(window == nullptr)
    {
        return area;
    }
    // In the player the game view is the window. The editor's Game panel is a part of its window,
    // and nothing covers it.
    const auto window_size = window->get_window().get_size();
    if(window_size.w != size.width || window_size.h != size.height)
    {
        return area;
    }
    const auto insets = window->get_window().get_safe_area_insets();
    area.x = float(insets.left);
    area.y = float(insets.top);
    area.width = float(int(size.width) - insets.left - insets.right);
    area.height = float(int(size.height) - insets.top - insets.bottom);
    return area;
}

/// The density a content scale of 1 stands for on the platform: Android's mdpi and the iOS point are 160 and 163
/// pixels per inch, desktops report 96 at 100 % display scaling.
#if UNRAVEL_PLATFORM_ANDROID
constexpr float BASE_SCREEN_DPI = 160.0f;
#elif UNRAVEL_PLATFORM_IOS
constexpr float BASE_SCREEN_DPI = 163.0f;
#else
constexpr float BASE_SCREEN_DPI = 96.0f;
#endif

auto internal_m2n_screen_get_dpi() -> float
{
    auto& ctx = engine::context();
    if(const auto& device = ctx.get_cached<rendering_system>().get_game_view_device())
    {
        return device->dpi;
    }
    auto* window = ctx.has<renderer>() ? ctx.get_cached<renderer>().get_main_window() : nullptr;
    return window ? window->get_window().get_content_scale() * BASE_SCREEN_DPI : BASE_SCREEN_DPI;
}

/// A resolution as it crosses to C#: the layout of the C# Resolution struct.
struct script_resolution
{
    int width{};
    int height{};
    int refresh_rate{};
};

auto to_script_resolution(const os::display::mode& mode) -> script_resolution
{
    return {int(mode.w), int(mode.h), int(mode.refresh_rate)};
}

/// The player's display options; null in the editor, whose window is not the game's to change.
auto find_display_manager() -> display_manager*
{
    auto& ctx = engine::context();
    return ctx.has<display_manager>() ? &ctx.get_cached<display_manager>() : nullptr;
}

auto find_main_window() -> os::window*
{
    auto& ctx = engine::context();
    auto* window = ctx.has<renderer>() ? ctx.get_cached<renderer>().get_main_window() : nullptr;
    return window ? &window->get_window() : nullptr;
}

/// Says once why a script's display change does nothing in the editor.
void log_editor_display_change()
{
    static bool is_logged = false;
    if(!is_logged)
    {
        is_logged = true;
        APPLOG_INFO("Display changes from scripts apply in the player. The editor keeps its own window, vsync and "
                    "frame cap.");
    }
}

void log_editor_background_change()
{
    static bool is_logged = false;
    if(!is_logged)
    {
        is_logged = true;
        APPLOG_INFO("Application.runInBackground applies in the player. The editor keeps its frame rate while it is "
                    "minimized.");
    }
}

/// The window mode of a C# FullScreenMode, whose values are the window_mode's.
auto to_window_mode(int mode, window_mode& result) -> bool
{
    if(mode < int(window_mode::windowed) || mode > int(window_mode::exclusive_fullscreen))
    {
        return false;
    }
    result = static_cast<window_mode>(mode);
    return true;
}

auto internal_m2n_screen_get_full_screen_mode() -> int
{
    auto* display = find_display_manager();
    return int(display ? display->get_options().mode : window_mode::windowed);
}

void internal_m2n_screen_set_resolution(int width, int height, int mode, int refresh_rate)
{
    auto* display = find_display_manager();
    if(display == nullptr)
    {
        log_editor_display_change();
        return;
    }
    window_mode new_mode{};
    if(!to_window_mode(mode, new_mode))
    {
        return;
    }
    const usize32_t size{std::uint32_t(std::max(width, 0)), std::uint32_t(std::max(height, 0))};
    display->set_window(engine::context(), new_mode, size, std::uint32_t(std::max(refresh_rate, 0)));
}

void internal_m2n_screen_set_full_screen_mode(int mode)
{
    auto* display = find_display_manager();
    if(display == nullptr)
    {
        log_editor_display_change();
        return;
    }
    window_mode new_mode{};
    if(!to_window_mode(mode, new_mode))
    {
        return;
    }
    const auto& options = display->get_options();
    display->set_window(engine::context(), new_mode, options.size, options.refresh_rate);
}

auto internal_m2n_screen_get_display_index() -> int
{
    auto* window = find_main_window();
    return window ? window->get_display_index() : os::display::get_primary_display_index();
}

void internal_m2n_screen_set_display_index(int display_index)
{
    auto* display = find_display_manager();
    if(display == nullptr)
    {
        log_editor_display_change();
        return;
    }
    display->set_display(engine::context(), display_index);
}

auto internal_m2n_screen_get_current_resolution() -> script_resolution
{
    auto* display = find_display_manager();
    if(display && display->get_options().mode == window_mode::exclusive_fullscreen)
    {
        const auto& options = display->get_options();
        return {int(options.size.width), int(options.size.height), int(options.refresh_rate)};
    }
    return to_script_resolution(os::display::get_desktop_mode(internal_m2n_screen_get_display_index()));
}

auto internal_m2n_screen_get_resolutions(int display_index) -> std::vector<script_resolution>
{
    if(display_index < 0 || display_index >= os::display::get_available_displays_count())
    {
        return {};
    }
    const auto modes = sort_display_modes(os::display::get_available_modes(display_index));
    std::vector<script_resolution> resolutions;
    resolutions.reserve(modes.size());
    std::transform(modes.begin(), modes.end(), std::back_inserter(resolutions), to_script_resolution);
    return resolutions;
}

auto internal_m2n_screen_get_display_count() -> int
{
    return os::display::get_available_displays_count();
}

auto internal_m2n_screen_get_display_name(int display_index) -> std::string
{
    if(display_index < 0 || display_index >= os::display::get_available_displays_count())
    {
        return {};
    }
    return os::display::get_name(display_index);
}

auto internal_m2n_screen_get_vsync() -> bool
{
    auto& ctx = engine::context();
    return ctx.has<renderer>() && ctx.get_cached<renderer>().get_vsync();
}

void internal_m2n_screen_set_vsync(bool vsync)
{
    auto* display = find_display_manager();
    if(display == nullptr)
    {
        log_editor_display_change();
        return;
    }
    display->set_vsync(engine::context(), vsync);
}

auto internal_m2n_application_get_target_frame_rate() -> int
{
    return static_cast<int>(engine::context().get_cached<simulation>().get_max_fps());
}

void internal_m2n_application_set_target_frame_rate(int frame_rate)
{
    auto* display = find_display_manager();
    if(display == nullptr)
    {
        log_editor_display_change();
        return;
    }
    display->set_max_fps(engine::context(), static_cast<std::uint32_t>(std::max(frame_rate, 0)));
}

auto internal_m2n_application_get_run_in_background() -> bool
{
    return engine::context().get_cached<simulation>().get_max_inactive_fps() == 0;
}

void internal_m2n_application_set_run_in_background(bool run_in_background)
{
    auto* display = find_display_manager();
    if(display == nullptr)
    {
        log_editor_background_change();
        return;
    }
    display->set_run_in_background(engine::context(), run_in_background);
}

void internal_m2n_set_time_scale(float scale)
{
    auto& ctx = engine::context();
    auto& sim = ctx.get_cached<simulation>();
    sim.set_time_scale(scale);
}

void internal_m2n_profiler_add_record(const std::string& name, float time_ms)
{
    auto profiler = get_app_profiler();
    if(profiler)
    {
        profiler->add_record(name, time_ms);
    }
}

auto m2n_test_uuid(const hpp::uuid& uid) -> hpp::uuid
{
    APPLOG_INFO("{}:: From C# {}", __func__, hpp::to_string(uid));

    auto newuid = generate_uuid();
    APPLOG_INFO("{}:: New C++ {}", __func__, hpp::to_string(newuid));

    return newuid;
}

auto internal_m2n_layers_layer_to_name(int layer) -> const std::string&
{
    auto& ctx = engine::context();
    auto& csettings = ctx.get<settings>();

    if(layer >= csettings.layer.layers.size())
    {
        dotnet::raise_exception("System", "Exception", fmt::format("Layer index {} is out of bounds.", layer));

        static const std::string empty;
        return empty;
    }
    return csettings.layer.layers[layer];
}

auto internal_m2n_layers_name_to_layer(const std::string& name) -> int
{
    auto& ctx = engine::context();
    auto& csettings = ctx.get<settings>();

    auto it = std::find(csettings.layer.layers.begin(), csettings.layer.layers.end(), name);
    if(it != csettings.layer.layers.end())
    {
        return static_cast<int>(std::distance(csettings.layer.layers.begin(), it));
    }

    return -1;
}

} // namespace

void register_core_runtime_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    {
        // Safe on any thread: scripts log from their worker threads too.
        auto reg = dotnet::internal_call_registry("Unravel.Core.Log");
        reg.add_internal_call("internal_m2n_log_trace", dotnet_internal_call(internal_m2n_log_trace));
        reg.add_internal_call("internal_m2n_log_info", dotnet_internal_call(internal_m2n_log_info));
        reg.add_internal_call("internal_m2n_log_warning", dotnet_internal_call(internal_m2n_log_warning));
        reg.add_internal_call("internal_m2n_log_error", dotnet_internal_call(internal_m2n_log_error));

    }
    {
        // Safe on any thread: it only logs.
        auto reg = dotnet::internal_call_registry("Unravel.Core.Tests");
        reg.add_internal_call("m2n_test_uuid", dotnet_internal_call(m2n_test_uuid));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.LayerMask");
        reg.add_internal_call("internal_m2n_layers_layer_to_name", script_internal_call(internal_m2n_layers_layer_to_name));
        reg.add_internal_call("internal_m2n_layers_name_to_layer", script_internal_call(internal_m2n_layers_name_to_layer));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Application");
        reg.add_internal_call("internal_m2n_application_quit", script_internal_call(internal_m2n_application_quit));
        reg.add_internal_call("internal_m2n_application_get_persistent_data_path",
                              script_internal_call(internal_m2n_application_get_persistent_data_path));
        reg.add_internal_call("internal_m2n_application_get_temporary_cache_path",
                              script_internal_call(internal_m2n_application_get_temporary_cache_path));
        reg.add_internal_call("internal_m2n_application_get_target_frame_rate",
                              script_internal_call(internal_m2n_application_get_target_frame_rate));
        reg.add_internal_call("internal_m2n_application_set_target_frame_rate",
                              script_internal_call(internal_m2n_application_set_target_frame_rate));
        reg.add_internal_call("internal_m2n_application_get_run_in_background",
                              script_internal_call(internal_m2n_application_get_run_in_background));
        reg.add_internal_call("internal_m2n_application_set_run_in_background",
                              script_internal_call(internal_m2n_application_set_run_in_background));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Screen");
        reg.add_internal_call("internal_m2n_screen_get_size", script_internal_call(internal_m2n_screen_get_size));
        reg.add_internal_call("internal_m2n_screen_get_dpi", script_internal_call(internal_m2n_screen_get_dpi));
        reg.add_internal_call("internal_m2n_screen_get_safe_area", script_internal_call(internal_m2n_screen_get_safe_area));
        reg.add_internal_call("internal_m2n_screen_get_full_screen_mode",
                              script_internal_call(internal_m2n_screen_get_full_screen_mode));
        reg.add_internal_call("internal_m2n_screen_set_full_screen_mode",
                              script_internal_call(internal_m2n_screen_set_full_screen_mode));
        reg.add_internal_call("internal_m2n_screen_set_resolution", script_internal_call(internal_m2n_screen_set_resolution));
        reg.add_internal_call("internal_m2n_screen_get_current_resolution",
                              script_internal_call(internal_m2n_screen_get_current_resolution));
        reg.add_internal_call("internal_m2n_screen_get_resolutions", script_internal_call(internal_m2n_screen_get_resolutions));
        reg.add_internal_call("internal_m2n_screen_get_display_count",
                              script_internal_call(internal_m2n_screen_get_display_count));
        reg.add_internal_call("internal_m2n_screen_get_display_name", script_internal_call(internal_m2n_screen_get_display_name));
        reg.add_internal_call("internal_m2n_screen_get_display_index",
                              script_internal_call(internal_m2n_screen_get_display_index));
        reg.add_internal_call("internal_m2n_screen_set_display_index",
                              script_internal_call(internal_m2n_screen_set_display_index));
        reg.add_internal_call("internal_m2n_screen_get_vsync", script_internal_call(internal_m2n_screen_get_vsync));
        reg.add_internal_call("internal_m2n_screen_set_vsync", script_internal_call(internal_m2n_screen_set_vsync));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Time");
        reg.add_internal_call("internal_m2n_set_time_scale", script_internal_call(internal_m2n_set_time_scale));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Profiler");
        reg.add_internal_call("internal_m2n_profiler_add_record", script_internal_call(internal_m2n_profiler_add_record));

    }
    {
        // Safe on any thread: the runtime's own counters.
        auto reg = dotnet::internal_call_registry("Unravel.Core.GCMonitor");
        reg.add_internal_call("internal_m2n_get_dotnet_heap_size", dotnet_internal_call(dotnet::gc_get_heap_size));
        reg.add_internal_call("internal_m2n_get_dotnet_used_size", dotnet_internal_call(dotnet::gc_get_used_size));
    }
}

} // namespace unravel
