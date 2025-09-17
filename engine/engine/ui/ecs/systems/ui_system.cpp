#include "ui_system.h"
#include "../../rmlui/RmlUi_Backend_Engine.h"
#include "../../rmlui/RmlUi_Platform_Engine.h"
#include "../components/ui_document_component.h"
#include "filesystem/filesystem.h"


#include <engine/events.h>
#include <engine/input/input.h>
#include <engine/rendering/renderer.h>
#include <engine/rendering/ecs/components/camera_component.h>
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
namespace
{
auto resolve_compiled_key(const std::string& key) -> std::string
{
    return string_utils::replace(key + ".asset", ex::get_data_directory(), ex::get_compiled_directory());
}
class unravel_file_interface : public Rml::FileInterface
{

    auto to_handle(FILE* f) -> Rml::FileHandle
    {
        return reinterpret_cast<Rml::FileHandle>(f);
    }
    auto to_file(Rml::FileHandle f) -> FILE*
    {
        return reinterpret_cast<FILE*>(f);
    }
public:
    auto Resolve(const Rml::String& url) -> Rml::String
    {
        auto key = fs::convert_to_protocol(url);
        if(fs::has_known_protocol(key))
        {
            auto compiled_path = resolve_compiled_key(key.string());
            auto real = fs::resolve_protocol(compiled_path).string();
            return real;
        }
        return url;
    }

    Rml::FileHandle Open(const Rml::String& url) override
    {
        auto real = Resolve(url);
        if(FILE* f = std::fopen(real.c_str(), "rb"))
        {
            return to_handle(f);
        }
        return 0; // not found
    }

    void Close(Rml::FileHandle file) override
    {
        if(!file)
        {
            return;
        }
        // If it was ours, it’s still just a FILE* here in this example.
        std::fclose(to_file(file));
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override
    {
        return std::fread(buffer, 1, size, to_file(file));
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override
    {
        return std::fseek(to_file(file), offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override
    {
        return std::ftell(to_file(file));
    }
};
}



auto ui_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    // Connect to engine events
    auto& ev = ctx.get_cached<events>();
    ev.on_frame_update.connect(sentinel_, 500, this, &ui_system::on_frame_update);
    ev.on_frame_render.connect(sentinel_, -100, this, &ui_system::on_frame_render); // Render UI last
    ev.on_os_event.connect(sentinel_, 2000, this, &ui_system::on_os_event);

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
    Rml::SetFileInterface(new unravel_file_interface());

    auto primary_display = os::display::get_primary_display_index();
    auto scale = os::display::get_content_scale(primary_display);
    ui_context_->SetDensityIndependentPixelRatio(scale);

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

void ui_system::on_frame_render(rtti::context& ctx, delta_t dt)
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

    auto& ecs_system = ctx.get_cached<ecs>();
    auto& scene = ecs_system.get_scene();
    auto& registry = *scene.registry;

    gfx::frame_buffer::ptr obuffer;
    registry.view<camera_component>().each(
        [&](auto e, auto&& camera)
                {
                    const auto& rview = camera.get_render_view();
                    obuffer = rview.fbo_safe_get("OBUFFER");
                });
            
    RmlUi_Backend_Engine::present_frame(obuffer);
}

void ui_system::on_window_resize(int width, int height)
{
    if(ui_context_)
    {
        ui_context_->SetDimensions(Rml::Vector2i(width, height));

        auto primary_display = os::display::get_primary_display_index();
        auto scale = os::display::get_content_scale(primary_display);
        ui_context_->SetDensityIndependentPixelRatio(scale);
    }

    // Update backend viewport
    RmlUi_Backend_Engine::set_viewport(width, height);
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

    // Handle window resize events
    if(event.type == os::events::window && event.window.type == os::window_event_id::resized)
    {
        on_window_resize(event.window.data1, event.window.data2);
    }

    if(event.type == os::events::key_down)
    {
        if(event.key.code == os::key::code::f2)
        {
            bool new_visible = !Rml::Debugger::IsVisible();
            Rml::Debugger::SetVisible(new_visible);
        }
    }
}

auto ui_system::is_root_element(rtti::context& ctx, Rml::Element* element) -> bool
{
    if(!ui_context_)
    {
        return false;
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
    load_font("engine:/data/fonts/Inter/static/Inter-Thin.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-ThinItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/Inter-ExtraLight.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-ExtraLightItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/Inter-Light.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-LightItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/Inter-Regular.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-RegularItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/Inter-Medium.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-MediumItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/Inter-SemiBold.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-SemiBoldItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/Inter-Bold.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-BoldItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/Inter-ExtraBold.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-ExtraBoldItalic.ttf");
    load_font("engine:/data/fonts/Inter/static/Inter-Black.ttf");
    // load_font("engine:/data/fonts/Inter/static/Inter-BlackItalic.ttf");

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

    if(ev.is_playing)
    {
        auto& input = ctx.get_cached<input_system>();

        // mapper.map("Mouse Left", input::mouse_button::left_button);
        // mapper.map("Mouse Right", input::mouse_button::right_button);
        // mapper.map("Mouse Middle", input::mouse_button::middle_button);

        // mapper.map("Mouse X", input::mouse_axis::x);
        // mapper.map("Mouse Y", input::mouse_axis::y);
        // mapper.map("Mouse ScrollWheel", input::mouse_axis::scroll);
        // if(input.is_pressed("Mouse Left"))
        // {
        //     ui_context_->ProcessMouseButtonDown(0, 0);
        // }
        // if(input.is_pressed("Mouse Right"))
        // {
        //     ui_context_->ProcessMouseButtonDown(1, 0);
        // }
        // if(input.is_pressed("Mouse Middle"))
        // {
        //     ui_context_->ProcessMouseButtonDown(2, 0);
        // }

        // if(input.is_released("Mouse Left"))
        // {
        //     ui_context_->ProcessMouseButtonUp(0, 0);
        // }
        // if(input.is_released("Mouse Right"))
        // {
        //     ui_context_->ProcessMouseButtonUp(1, 0);
        // }
        // if(input.is_released("Mouse Middle"))
        // {
        //     ui_context_->ProcessMouseButtonUp(2, 0);
        // }

        // if(input.manager.get_mouse().get_axis_value(0) != 0.0f || input.manager.get_mouse().get_axis_value(1) != 0.0f)
        // {
        //     ui_context_->ProcessMouseMove(input.manager.get_mouse().get_position().x, input.manager.get_mouse().get_position().y, 0);
        // }

        // if(input.manager.get_mouse().get_scroll() != 0.0f)
        // {
        //     ui_context_->ProcessMouseWheel(input.manager.get_mouse().get_scroll(), 0);
        // }

    

    // case os::events::key_down:
    // {
    //     auto rml_key = convert_key(event.key.code);
    //     auto modifiers = get_key_modifier_state();
    //     handled = context->ProcessKeyDown(rml_key, modifiers);
    //     if (event.key.code == os::key::code::enter || event.key.code == os::key::code::kp_enter)
    //     {
    //         handled |= context->ProcessTextInput('\n');
    //     }
    //     break;
    // }
    
    // case os::events::key_up:
    // {
    //     auto rml_key = convert_key(event.key.code);
    //     auto modifiers = get_key_modifier_state();
    //     handled = context->ProcessKeyUp(rml_key, modifiers);
    //     break;
    // }
    
    // case os::events::text_input:
    // {
    //     // Convert text input to RmlUi character events
    //     for (char c : event.text.text)
    //     {
    //         if (c != 0)
    //         {
    //             handled = context->ProcessTextInput(c) || handled;
    //         }
    //     }
    //     break;
    // }
    
    // case os::events::mouse_button:
    // {
    //     auto rml_button = convert_mouse_button(event.button.button);
    //     auto modifiers = get_key_modifier_state();
        
    //     if (event.button.state_id == os::state::pressed)
    //     {
    //         handled = context->ProcessMouseButtonDown(rml_button, modifiers);
    //     }
    //     else if (event.button.state_id == os::state::released)
    //     {
    //         handled = context->ProcessMouseButtonUp(rml_button, modifiers);
    //     }
    //     break;
    // }
    
    // case os::events::mouse_motion:
    // {
    //     auto modifiers = get_key_modifier_state();
    //     handled = context->ProcessMouseMove(event.motion.x, event.motion.y, modifiers);
    //     break;
    // }
    
    // case os::events::mouse_wheel:
    // {
    //     auto modifiers = get_key_modifier_state();
    //     // RmlUi expects wheel delta as integer, ospp provides float
    //     float wheel_delta = static_cast<float>(event.wheel.y);
    //     handled = context->ProcessMouseWheel(wheel_delta, modifiers);
    //     break;
    // }
    
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

        
        if(ui_comp.version != ui_comp.asset.version())
        {
            if(ui_comp.document)
            {
                ui_comp.document->Close();
                ui_comp.document = nullptr;
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

        // Handle visibility based on auto_show flag
        if(ui_comp.is_loaded() && ui_comp.auto_show && !ui_comp.is_visible())
        {
            ui_comp.document->Show();
            
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

    raw_document->SetId("hud");

    if(component.document)
    {
        component.document->Close();
    }
    // Create shared_ptr with custom deleter that properly closes the document
    component.document = raw_document;
    component.version = component.asset.version();

    APPLOG_INFO("Successfully loaded UI document: {}", component.asset.id());
    return true;
}

} // namespace unravel
