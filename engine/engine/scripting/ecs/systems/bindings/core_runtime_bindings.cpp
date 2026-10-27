#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/events.h>
#include <engine/play_mode.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/renderer.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>
#include <seq/seq.h>
#include <simulation/simulation.h>
#include <uuid/uuid.h>

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
    const auto& size = ctx.get_cached<rendering_system>().get_game_view_size();
    script_rect area{0.0f, 0.0f, float(size.width), float(size.height)};
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

/// The logical DPI Windows reports at 100 % display scaling; the content scale multiplies it.
constexpr float BASE_SCREEN_DPI = 96.0f;

auto internal_m2n_screen_get_dpi() -> float
{
    auto& ctx = engine::context();
    auto* window = ctx.has<renderer>() ? ctx.get_cached<renderer>().get_main_window() : nullptr;
    return window ? window->get_window().get_content_scale() * BASE_SCREEN_DPI : BASE_SCREEN_DPI;
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
        auto reg = dotnet::internal_call_registry("Unravel.Core.Log");
        reg.add_internal_call("internal_m2n_log_trace", dotnet_internal_call(internal_m2n_log_trace));
        reg.add_internal_call("internal_m2n_log_info", dotnet_internal_call(internal_m2n_log_info));
        reg.add_internal_call("internal_m2n_log_warning", dotnet_internal_call(internal_m2n_log_warning));
        reg.add_internal_call("internal_m2n_log_error", dotnet_internal_call(internal_m2n_log_error));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Tests");
        reg.add_internal_call("m2n_test_uuid", dotnet_internal_call(m2n_test_uuid));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.LayerMask");
        reg.add_internal_call("internal_m2n_layers_layer_to_name", dotnet_internal_call(internal_m2n_layers_layer_to_name));
        reg.add_internal_call("internal_m2n_layers_name_to_layer", dotnet_internal_call(internal_m2n_layers_name_to_layer));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Application");
        reg.add_internal_call("internal_m2n_application_quit", dotnet_internal_call(internal_m2n_application_quit));
        reg.add_internal_call("internal_m2n_application_get_persistent_data_path",
                              dotnet_internal_call(internal_m2n_application_get_persistent_data_path));
        reg.add_internal_call("internal_m2n_application_get_temporary_cache_path",
                              dotnet_internal_call(internal_m2n_application_get_temporary_cache_path));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Screen");
        reg.add_internal_call("internal_m2n_screen_get_size", dotnet_internal_call(internal_m2n_screen_get_size));
        reg.add_internal_call("internal_m2n_screen_get_dpi", dotnet_internal_call(internal_m2n_screen_get_dpi));
        reg.add_internal_call("internal_m2n_screen_get_safe_area", dotnet_internal_call(internal_m2n_screen_get_safe_area));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Time");
        reg.add_internal_call("internal_m2n_set_time_scale", dotnet_internal_call(internal_m2n_set_time_scale));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Profiler");
        reg.add_internal_call("internal_m2n_profiler_add_record", dotnet_internal_call(internal_m2n_profiler_add_record));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.GCMonitor");
        reg.add_internal_call("internal_m2n_get_dotnet_heap_size", dotnet_internal_call(dotnet::gc_get_heap_size));
        reg.add_internal_call("internal_m2n_get_dotnet_used_size", dotnet_internal_call(dotnet::gc_get_used_size));
    }
}

} // namespace unravel
