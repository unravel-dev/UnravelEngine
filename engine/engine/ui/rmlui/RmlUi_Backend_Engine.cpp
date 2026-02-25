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
#include <RmlUi/Core/Plugin.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/ElementDocument.h>
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
    
    struct PreloadStylesheetPlugin : public Rml::Plugin
    {
        Rml::SharedPtr<Rml::StyleSheetContainer> default_stylesheet = nullptr;

        void set_default_stylesheet(Rml::SharedPtr<Rml::StyleSheetContainer> stylesheet) 
        {
            default_stylesheet = std::move(stylesheet);
        }

        void OnDocumentLoad(Rml::ElementDocument* document) 
        {
            // Inject user agent stylesheet for default style
            if (default_stylesheet) 
            {
                Rml::SharedPtr<Rml::StyleSheetContainer> new_stylesheet = std::make_shared<Rml::StyleSheetContainer>();
                new_stylesheet->MergeStyleSheetContainer(*default_stylesheet);
                if (document->GetStyleSheetContainer()) 
                {
                    new_stylesheet->MergeStyleSheetContainer(*document->GetStyleSheetContainer());
                }
                document->SetStyleSheetContainer(new_stylesheet);
            }
        }
    };
    // Global backend data
    struct BackendData
    {
        std::unique_ptr<PreloadStylesheetPlugin> unravel_plugin = nullptr;
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

        
    data->unravel_plugin = std::make_unique<PreloadStylesheetPlugin>();
    auto path = fs::resolve_protocol("engine:/data/ui/rml.rcss");
    Rml::SharedPtr<Rml::StyleSheetContainer> ss = Rml::Factory::InstanceStyleSheetFile(path.string());
    data->unravel_plugin->set_default_stylesheet(ss);
    // Register Unravel plugin
    Rml::RegisterPlugin(data->unravel_plugin.get());

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
        // Unregister Unravel plugin
        Rml::UnregisterPlugin(data->unravel_plugin.get());
        data->unravel_plugin.reset();
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

void begin_frame(RmlUi_FrameState& state)
{
    if (data && data->initialized)
    {
        data->render_interface.begin_frame(state);
    }
}

void end_frame()
{
    if (data && data->initialized)
    {
        data->render_interface.end_frame();
    }
}

} // namespace RmlUi_Backend_Engine
} // namespace unravel
