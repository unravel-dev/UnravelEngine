/*
 * RmlUi Engine Backend Implementation
 */

#include "RmlUi_Backend_Engine.h"
#include "RmlUi_SystemInterface.h"
#include "RmlUi_RenderInterface.h"
#include "RmlUi_FileInterface.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Debugger/Debugger.h>

#include <logging/logging.h>
#include <engine/engine.h>
#include <engine/rendering/renderer.h>

namespace unravel
{
namespace RmlUi_Backend_Engine
{

namespace
{
    // Global backend data
    struct BackendData
    {
        RmlUi_SystemInterface system_interface;
        RmlUi_RenderInterface render_interface;
        RmlUi_FileInterface file_interface;
        rtti::context* engine_ctx = nullptr;
        bool running = true;
        bool initialized = false;
    };

    std::unique_ptr<BackendData> data;
}

auto initialize(rtti::context& ctx, const char* window_name, int width, int height) -> bool
{
    APPLOG_TRACE("{}::{}", "RmlUi_Backend_Engine", __func__);

    if (data)
    {
        APPLOG_ERROR("RmlUi backend already initialized");
        return false;
    }

    data = std::make_unique<BackendData>();
    data->engine_ctx = &ctx;

    // Initialize system interface
    if (!data->system_interface.init(ctx))
    {
        APPLOG_ERROR("Failed to initialize RmlUi system interface");
        data.reset();
        return false;
    }

    // Initialize render interface
    if (!data->render_interface.init(ctx))
    {
        APPLOG_ERROR("Failed to initialize RmlUi render interface");
        data->system_interface.shutdown();
        data.reset();
        return false;
    }

    // Set up RmlUi with our interfaces
    Rml::SetSystemInterface(&data->system_interface);
    Rml::SetRenderInterface(&data->render_interface);
    Rml::SetFileInterface(&data->file_interface);

    // Initialize RmlUi core
    if (!Rml::Initialise())
    {
        APPLOG_ERROR("Failed to initialize RmlUi core");
        data->render_interface.shutdown();
        data->system_interface.shutdown();
        data.reset();
        return false;
    }

    // Set viewport size
    data->render_interface.set_viewport(width, height);

    // Connect to engine's renderer for window access
    if (ctx.has<renderer>())
    {
        auto& rend = ctx.get_cached<renderer>();
        data->system_interface.set_window(rend.get_main_window());
    }

    data->initialized = true;
    return true;
}

void shutdown()
{
    APPLOG_TRACE("{}::{}", "RmlUi_Backend_Engine", __func__);

    if (!data)
    {
        APPLOG_WARNING("RmlUi backend not initialized");
        return;
    }

    if (data->initialized)
    {
        // Shutdown RmlUi core
        Rml::Shutdown();

        // Cleanup our interfaces
        data->render_interface.shutdown();
        data->system_interface.shutdown();

        data->initialized = false;
    }

    data.reset();
}

auto get_system_interface() -> Rml::SystemInterface*
{
    if (!data)
    {
        APPLOG_ERROR("RmlUi backend not initialized");
        return nullptr;
    }
    return &data->system_interface;
}

auto get_render_interface() -> Rml::RenderInterface*
{
    if (!data)
    {
        APPLOG_ERROR("RmlUi backend not initialized");
        return nullptr;
    }
    return &data->render_interface;
}

auto process_events(Rml::Context* context, KeyDownCallback key_down_callback) -> bool
{
    if (!data || !data->initialized)
    {
        return false;
    }

    // The engine handles event polling, so we just return the running state
    // Events are forwarded to RmlUi through the ui_system::on_os_event handler
    return data->running;
}

void request_exit()
{
    if (data)
    {
        data->running = false;
    }
}

void begin_frame()
{
    if (data && data->initialized)
    {
        data->render_interface.begin_frame();
    }
}

void present_frame(const gfx::frame_buffer::ptr& framebuffer)
{
    if (data && data->initialized)
    {
        data->render_interface.end_frame(framebuffer);
    }
}

void set_viewport(int width, int height)
{
    if (data && data->initialized)
    {
        data->render_interface.set_viewport(width, height);
    }
}

} // namespace RmlUi_Backend_Engine
} // namespace unravel
