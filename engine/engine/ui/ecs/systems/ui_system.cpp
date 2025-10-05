#include "ui_system.h"
#include "../../rmlui/RmlUi_Backend_Engine.h"
#include "../../rmlui/RmlUi_SystemInterface.h"
#include "../../rmlui/RmlUi_FileInterface.h"
#include "../../rmlui/RmlUi_RenderInterface.h"
#include "../components/ui_document_component.h"
#include "filesystem/filesystem.h"
#include "glm/ext/scalar_constants.hpp"
#include "glm/gtc/epsilon.hpp"


#include <engine/events.h>
#include <engine/input/input.h>
#include <engine/rendering/renderer.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/ecs/components/transform_component.h>

#include <logging/logging.h>


#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Debugger/Debugger.h>
#include <RmlUi/Core.h>

#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/profiler/profiler.h>

#include <string_utils/utils.h>
#include <engine/assets/impl/asset_extensions.h>


namespace unravel
{


auto ui_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    // Connect to engine events
    auto& ev = ctx.get_cached<events>();
    ev.on_frame_update.connect(sentinel_, 500, this, &ui_system::on_frame_update);
    // ev.on_frame_render.connect(sentinel_, -100, this, &ui_system::on_frame_render); // Render UI last
    ev.on_os_event.connect(sentinel_, 100, this, &ui_system::on_os_event);

    // Initialize RmlUi backend
    if(!RmlUi_Backend_Engine::initialize(ctx, "UnravelEngine UI"))
    {
        APPLOG_ERROR("Failed to initialize RmlUi backend");
        return false;
    }

    // Create main UI context
    // Get viewport size from renderer
    int width = 1024, height = 768;
    if(ctx.has<renderer>())
    {
        const auto& rend = ctx.get_cached<renderer>();
        const auto& window = rend.get_main_window();
        if(window)
        {
            auto size = window->get_window().get_size();
            width = static_cast<int>(size.w);
            height = static_cast<int>(size.h);
        }
    }

    // Create RmlUi context
    ui_context_ = Rml::CreateContext("main", Rml::Vector2i(width, height));
    if(!ui_context_)
    {
        APPLOG_ERROR("Failed to create RmlUi context");
        RmlUi_Backend_Engine::shutdown();
        return false;
    }

    // auto primary_display = os::display::get_primary_display_index();
    // auto scale = os::display::get_content_scale(primary_display);
    // ui_context_->SetDensityIndependentPixelRatio(scale);

    Rml::Debugger::Initialise(ui_context_);

    APPLOG_INFO("UI system initialized successfully ({}x{})", width, height);

    // Register component callbacks
    register_component_callbacks(ctx);

    // Load test UI document
    load_fonts();

    return true;
}

auto ui_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    // Remove RmlUi context
    if(ui_context_)
    {

        auto& ecs_system = ctx.get_cached<ecs>();
        auto& scene = ecs_system.get_scene();
        auto& registry = *scene.registry;
    
        // Iterate over all entities with ui_document_component
        auto view = registry.view<ui_document_component>();
    
        for(auto entity : view)
        {
            auto& ui_comp = view.get<ui_document_component>(entity);
            if(ui_comp.document)
            {
                ui_comp.document->Close();
                ui_comp.document = nullptr;
            }
        }


        Rml::RemoveContext("main");
        ui_context_ = nullptr;
    }

    Rml::Debugger::Shutdown();

    // Shutdown RmlUi backend
    RmlUi_Backend_Engine::shutdown();

    APPLOG_INFO("UI system deinitialized successfully");
    return true;
}

void ui_system::on_frame_update(rtti::context& ctx, delta_t dt)
{
    if(!ui_context_)
    {
        return;
    }

    APP_SCOPE_PERF("UI/System Update");

    // Process all UI document components
    update_ui_document_components(ctx);

    // Update RmlUi context
    ui_context_->Update();
}

void ui_system::on_frame_render(const gfx::frame_buffer::ptr& output, delta_t dt)
{
    if(!ui_context_)
    {
        return;
    }

    APP_SCOPE_PERF("UI/System Render");

    // Begin UI rendering
    RmlUi_Backend_Engine::begin_frame();

    // Render RmlUi context
    ui_context_->Render();

    RmlUi_Backend_Engine::present_frame(output);
}


auto ui_system::get_context() -> Rml::Context*
{
    return ui_context_;
}


void ui_system::on_os_event(rtti::context& ctx, os::event& event)
{
    if(!ui_context_)
    {
        return;
    }

    // Get engine window for event processing
    if(!ctx.has<renderer>())
    {
        return;
    }

    // Forward event to RmlUi
    if(RmlEngine::input_event_handler(ui_context_, event))
    {
        if(event.type == os::events::mouse_button || event.type == os::events::mouse_motion)
        {
            auto hover_element = ui_context_->GetHoverElement();
            if(!is_root_element(ctx, hover_element))
            {
                event = {};
            }
        }
    }

    if(event.type == os::events::key_down)
    {
        if(event.key.code == os::key::code::f2)
        {
            bool new_visible = !Rml::Debugger::IsVisible();
            Rml::Debugger::SetVisible(new_visible);
        }
    }

    if(event.type == os::events::display_content_scale_changed)
    {
        // const auto& rend = ctx.get_cached<renderer>();
        // const auto& window = rend.get_main_window();
        // auto scale = window->get_window().get_display_scale();
        // ui_context_->SetDensityIndependentPixelRatio(scale);

    }

    if(event.type == os::events::window)
    {
        if(event.window.type == os::window_event_id::size_changed)
        {
            // const auto& rend = ctx.get_cached<renderer>();
            // const auto& window = rend.get_main_window();
            // auto scale = window->get_window().get_display_scale();
            // ui_context_->SetDensityIndependentPixelRatio(scale);
        }
    }
}

auto ui_system::is_root_element(rtti::context& ctx, Rml::Element* element) -> bool
{
    if(!ui_context_)
    {
        return false;
    }

    if(!element)
    {
        return false;
    }

    if(element->GetTagName() == "#root")
    {
        return true;
    }

    auto& ecs_system = ctx.get_cached<ecs>();
    auto& scene = ecs_system.get_scene();
    auto& registry = *scene.registry;

    // Iterate over all entities with ui_document_component
    auto view = registry.view<ui_document_component>();

    for(auto entity : view)
    {
        auto& ui_comp = view.get<ui_document_component>(entity);
        if(ui_comp.document == element)
        {
            return true;
        }
    }
    return false;
}

void ui_system::load_fonts()
{
    if(!ui_context_)
    {
        return;
    }


    auto load_font = [&](const std::string& path) -> void
    {
        const Rml::String font_path = fs::resolve_protocol(path).string();
        Rml::LoadFontFace(font_path, false);
    };

    // Load font
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-Thin.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-ThinItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-ExtraLight.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-ExtraLightItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-Light.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-LightItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-Regular.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-Italic.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-Medium.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-MediumItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-SemiBold.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-SemiBoldItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-Bold.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-BoldItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-ExtraBold.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-ExtraBoldItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-Black.ttf");
    load_font("engine:/data/fonts/Inter/static/28pt/Inter-BlackItalic.ttf");

}

void ui_system::on_create_component(entt::registry& r, entt::entity e)
{
}

void ui_system::on_destroy_component(entt::registry& r, entt::entity e)
{
    auto& component = r.get<ui_document_component>(e);
    if(component.document)
    {
        component.document->Close();
        component.document = nullptr;
    }
}

void ui_system::register_component_callbacks(rtti::context& ctx)
{
    // This method would register ECS callbacks for ui_document_component
    // creation and destruction. Implementation depends on your ECS system's
    // callback mechanism. For now, this is a placeholder for the architecture.
    APPLOG_INFO("UI component callbacks registered");
}

void ui_system::update_ui_document_components(rtti::context& ctx)
{
    if(!ctx.has<ecs>())
    {
        return;
    }


    auto& ev = ctx.get_cached<events>();

    // if(ev.is_playing)
    {
        auto& input = ctx.get_cached<input_system>();

    
        auto mouse_delta_x = input.manager.get_mouse().get_axis_value(0);
        auto mouse_delta_y = input.manager.get_mouse().get_axis_value(1);

        if(math::epsilonNotEqual(mouse_delta_x, 0.0f, math::epsilon<float>()) || math::epsilonNotEqual(mouse_delta_y, 0.0f, math::epsilon<float>()))
        {
            auto mouse_x = input.manager.get_mouse().get_position().x;
            auto mouse_y = input.manager.get_mouse().get_position().y;
            ui_context_->ProcessMouseMove(mouse_x, mouse_y, 0);
        }    
    }


    auto& ecs_system = ctx.get_cached<ecs>();
    auto& scene = ecs_system.get_scene();
    auto& registry = *scene.registry;

    registry.view<camera_component>().each(
        [&](auto e, auto&& camera)
        {
            auto viewport = camera.get_viewport_size();
            RmlUi_Backend_Engine::set_viewport(viewport.width, viewport.height);
            ui_context_->SetDimensions(Rml::Vector2i(viewport.width, viewport.height));


        });

    // Iterate over all entities with ui_document_component
    auto view = registry.view<ui_document_component>();

    for(auto entity : view)
    {
        auto& ui_comp = view.get<ui_document_component>(entity);

        bool active = registry.all_of<active_component>(entity);
        
        if(!ev.is_playing)
        {
            if(ui_comp.version != ui_comp.asset.version())
            {
                if(ui_comp.document)
                {
                    ui_comp.document->Close();
                    ui_comp.document = nullptr;
                }
            }
        }

        // Skip if no document path is specified
        if(!ui_comp.asset)
        {
            continue;
        }

        // Load document if not already loaded
        if(!ui_comp.is_loaded())
        {
            load_ui_document(ui_comp);
        }

        if(!ui_comp.document)
        {
            continue;
        }

        bool active_and_enabled = active && ui_comp.is_enabled();
        if(active_and_enabled)
        {
            if(!ui_comp.document->IsVisible())
            {
                ui_comp.document->Show();
            }
        }
        else
        {
            if( ui_comp.document->IsVisible())
            {
                ui_comp.document->Hide();
            }
        }

    }
}

auto ui_system::load_ui_document(ui_document_component& component) -> bool
{
    if(!ui_context_)
    {
        APPLOG_ERROR("Cannot load UI document: RmlUi context not available");
        return false;
    }

    auto asset = component.asset.get();
    if(!asset)
    {
        APPLOG_WARNING("Cannot load document: document_path is empty");
        return false;
    }

    // auto compiled_path = resolve_compiled_key(component.asset.id());
            
    auto real = fs::resolve_protocol(component.asset.id());

    Rml::String url_safe_path = Rml::StringUtilities::Replace(real.string(), ':', '|');

    // Load the document using the existing load_document method
    auto raw_document = ui_context_->LoadDocumentFromMemory(asset->content, url_safe_path);
    if(!raw_document)
    {
        APPLOG_ERROR("Failed to load UI document: {}", component.asset.id());
        return false;
    }
    raw_document->ReloadStyleSheet();

    raw_document->SetId("body");

    if(component.document)
    {
        component.document->Close();
    }
    // Create shared_ptr with custom deleter that properly closes the document
    component.document = raw_document;
    component.version = component.asset.version();
    
    return true;
}

} // namespace unravel
