#include "ui_system.h"
#include "../../rmlui/RmlUi_Backend_Engine.h"
#include "../../rmlui/RmlUi_SystemInterface.h"
#include "../../rmlui/RmlUi_FileInterface.h"
#include "../../rmlui/RmlUi_RenderInterface.h"
#include "../components/ui_document_component.h"
#include "entt/entity/fwd.hpp"
#include "filesystem/filesystem.h"
#include "glm/ext/scalar_constants.hpp"
#include "glm/gtc/epsilon.hpp"
#include "hpp/small_vector.hpp"

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
#include <math/math.h>

#include <string_utils/utils.h>
#include <engine/assets/impl/asset_extensions.h>

#include <graphics/texture.h>

#include <algorithm>
#include <vector>

namespace unravel
{
namespace
{
    Rml::Context* debug_target_context_ = nullptr;

    auto remove_context(Rml::Context* context) -> void
    {
        if(debug_target_context_ == context)
        {
            debug_target_context_ = nullptr;
            Rml::Debugger::SetContext(nullptr);
        }
        if(context)
        {
            Rml::RemoveContext(context->GetName());
            context = nullptr;
        }
    }
}

auto ui_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    // Connect to engine events (UI update/render is done in rendering_system::render_scene)
    auto& ev = ctx.get_cached<events>();
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

    // Create debug context (empty, used for RmlUi debugger)
    debug_context_ = Rml::CreateContext("debug", Rml::Vector2i(width, height));
    if(!debug_context_)
    {
        APPLOG_ERROR("Failed to create RmlUi debug context");
        RmlUi_Backend_Engine::shutdown();
        return false;
    }
    Rml::Debugger::Initialise(debug_context_);
    debug_render_layer_stack_ = std::make_shared<RmlUi_RenderLayerStack>();
    // Register component callbacks
    register_component_callbacks(ctx);

    // Load test UI document
    load_fonts();

    return true;
}

auto ui_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ecs_system = ctx.get_cached<ecs>();
    auto& scene = ecs_system.get_scene();
    auto& registry = *scene.registry;

    auto view = registry.view<ui_document_component>();
    for(auto entity : view)
    {
        auto& ui_comp = view.get<ui_document_component>(entity);
        if(ui_comp.context)
        {
            if(ui_comp.document)
            {
                ui_comp.document->Close();
                ui_comp.document = nullptr;
            }
            remove_context(ui_comp.context);
            ui_comp.context = nullptr;
        }
    }

    debug_target_context_ = nullptr;
    if(debug_context_)
    {
        remove_context(debug_context_);
        debug_context_ = nullptr;
    }
    Rml::Debugger::Shutdown();
    RmlUi_Backend_Engine::shutdown();
    APPLOG_INFO("UI system deinitialized successfully");
    return true;
}

void ui_system::on_frame_update(rtti::context& ctx, entt::handle camera_entity, scene& scn, delta_t dt)
{
    update_ui_document_components(ctx, scn, camera_entity);
}

void ui_system::on_frame_render(const gfx::frame_buffer::ptr& output, entt::handle camera_entity, scene& scn, delta_t dt)
{
    if(!output)
    {
        return;
    }
    APP_SCOPE_PERF("UI/System Render");


    auto& camera_comp = camera_entity.get<camera_component>();
    const auto& camera = camera_comp.get_camera();

    scn.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity entity, ui_document_component& ui_comp, active_component& active)
        {
            auto handle = scn.create_handle(entity);
            if(!ui_comp.context || !ui_comp.document || !ui_comp.is_enabled())
            {
                return;
            }

            if(!ui_comp.document->IsVisible())
            {
                return;
            }


            gfx::frame_buffer::ptr target = output;

            if(ui_comp.render_mode == ui_render_mode::world_space)
            {
                auto& transform_comp = handle.get<transform_component>();
                const auto& world_transform = transform_comp.get_transform_global();
                auto bbox = ui_comp.get_bounds();
                if(!camera.test_obb(bbox, world_transform))
                {
                    return;
                }

                ensure_document_framebuffer(ui_comp);

                if(!ui_comp.framebuffer || !ui_comp.framebuffer->is_valid())
                {
                    return;
                }
                target = ui_comp.framebuffer;
            }

            if(!ui_comp.render_layer_stack)
            {
                ui_comp.render_layer_stack = std::make_shared<RmlUi_RenderLayerStack>();
            }

            auto sz = target->get_size();
            RmlUi_FrameState frame_state;
            frame_state.viewport_width = static_cast<int>(sz.width);
            frame_state.viewport_height = static_cast<int>(sz.height);
            frame_state.framebuffer = target;
            frame_state.clear_to_transparent = (ui_comp.render_mode == ui_render_mode::world_space);
            frame_state.render_layers = ui_comp.render_layer_stack;

            RmlUi_Backend_Engine::begin_frame(frame_state);
            ui_comp.context->SetDimensions(Rml::Vector2i(frame_state.viewport_width, frame_state.viewport_height));
            ui_comp.context->Update();
            ui_comp.context->Render();
            RmlUi_Backend_Engine::end_frame();
        });
    if(debug_context_)
    {
        auto sz = output->get_size();
        RmlUi_FrameState frame_state;
        frame_state.viewport_width = static_cast<int>(sz.width);
        frame_state.viewport_height = static_cast<int>(sz.height);
        frame_state.framebuffer = output;
        frame_state.clear_to_transparent = false;
        frame_state.render_layers = debug_render_layer_stack_;
        RmlUi_Backend_Engine::begin_frame(frame_state);
        debug_context_->SetDimensions(Rml::Vector2i(frame_state.viewport_width, frame_state.viewport_height));
        debug_context_->Update();
        debug_context_->Render();
        RmlUi_Backend_Engine::end_frame();
    }
}


void ui_system::on_os_event(rtti::context& ctx, os::event& event)
{
    auto& ecs_system = ctx.get_cached<ecs>();
    auto& scene = ecs_system.get_scene();

    if(Rml::Debugger::IsVisible() && debug_context_)
    {
        if(process_event(scene, debug_context_, event))
        {
            event = {};
        }
    }
    scene.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity, ui_document_component& ui_comp, active_component& active)
        {
            if(ui_comp.context && ui_comp.is_enabled())
            {
                if(process_event(scene, ui_comp.context, event))
                {
                    event = {};
                }
            }
        });

}

auto ui_system::is_not_root_element(scene& scn, Rml::Element* element) -> bool
{
    if(!element)
    {
        return false;
    }
    if(element->GetTagName() == "#root")
    {
        return false;
    }

    auto view = scn.registry->view<ui_document_component>();
    for(auto entity : view)
    {
        auto& ui_comp = view.get<ui_document_component>(entity);
        if(ui_comp.document == element)
        {
            return false;
        }
    }
    return true;
}

void ui_system::load_font(const std::string& path)
{
    const Rml::String font_path = fs::resolve_protocol(path).string();
    Rml::LoadFontFace(font_path, false);
    fonts_loaded_.insert(path);
}

void ui_system::load_fonts()
{
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

void ui_system::on_load_component(entt::registry& r, entt::entity e)
{
    auto& component = r.get<ui_document_component>(e);
    if(!component.is_loaded())
    {
        auto& ctx = engine::context();
        auto& system = ctx.get_cached<ui_system>();
        system.load_ui_document(e, component, true, true);
    }
}

void ui_system::on_destroy_component(entt::registry& r, entt::entity e)
{
    auto& component = r.get<ui_document_component>(e);
    if(component.context)
    {
        if(component.document)
        {
            component.document->Close();
            component.document = nullptr;
        }

        remove_context(component.context);
        component.context = nullptr;
    }
    component.framebuffer.reset();
    component.render_layer_stack.reset();
}

void ui_system::register_component_callbacks(rtti::context& ctx)
{
    // This method would register ECS callbacks for ui_document_component
    // creation and destruction. Implementation depends on your ECS system's
    // callback mechanism. For now, this is a placeholder for the architecture.
    APPLOG_INFO("UI component callbacks registered");
}

auto ui_system::is_debugger_enabled() const -> bool
{
    return Rml::Debugger::IsVisible();
}
void ui_system::set_debugger_enabled(bool enabled)
{
    Rml::Debugger::SetVisible(enabled);
}

auto ui_system::process_mouse_move(scene& scn, Rml::Context* context, int x, int y) -> bool
{
    if(!context->ProcessMouseMove(x, y, 0))
    {
        auto hover_element = context->GetHoverElement();
        if(is_not_root_element(scn, hover_element))
        {
            if(debug_target_context_ != context)
            {
                debug_target_context_ = context;
                Rml::Debugger::SetContext(context);
            }

            return true;
        }
    }
    return false;
}

auto ui_system::process_event(scene& scn, Rml::Context* context, os::event& event) -> bool
{
    if(!context)
    {
        return false;
    }
    if(RmlEngine::input_event_handler(context, event))
    {
        if(event.type == os::events::mouse_button || event.type == os::events::mouse_motion)
        {
            auto hover_element = context->GetHoverElement();
            if(is_not_root_element(scn, hover_element))
            {
                return true;
            }
        }
    }
    return false;
}

void ui_system::update_ui_document_components(rtti::context& ctx, scene& scn, entt::handle camera_entity)
{
    auto& ev = ctx.get_cached<events>();
    auto& camera_comp = camera_entity.get<camera_component>();

    const auto& cam = camera_comp.get_camera();
    auto& input = ctx.get_cached<input_system>();

    if(Rml::Debugger::IsVisible() && debug_context_)
    {
        if(input.manager.is_input_allowed())
        {
            auto mouse_x = input.manager.get_mouse().get_position().x;
            auto mouse_y = input.manager.get_mouse().get_position().y;
            debug_context_->ProcessMouseMove(static_cast<int>(mouse_x), static_cast<int>(mouse_y), 0);
        }
    }

    float dp_ratio = 1.0f;
  
    auto viewport = camera_comp.get_viewport_size();
    auto viewport_width = static_cast<int>(viewport.width);
    auto viewport_height = static_cast<int>(viewport.height);
  
    {
        auto work_zone = input.manager.get_work_zone();
        if(work_zone.w > 0 && work_zone.h > 0)
        {
            dp_ratio = (static_cast<float>(viewport_width) / static_cast<float>(work_zone.w) +
                        static_cast<float>(viewport_height) / static_cast<float>(work_zone.h)) *
                       0.5f;
        }
    }


    struct world_space_doc
    {
        entt::handle handle;
        float distance;
    };
    hpp::small_vector<world_space_doc> world_space_docs;

    bool hit_found = false;

    scn.registry->view<ui_document_component>().each(
        [&](entt::entity entity, ui_document_component& ui_comp)
        {
            auto handle = scn.create_handle(entity);
            bool active = handle.all_of<active_component>();

            if(!ev.is_playing && ui_comp.version != ui_comp.asset.version())
            {
                if(ui_comp.document)
                {
                    ui_comp.document->Close();
                    ui_comp.document = nullptr;
                }
                if(ui_comp.context)
                {

                    remove_context(ui_comp.context);
                    ui_comp.context = nullptr;

                }
            }

            if(!ui_comp.is_loaded())
            {
                load_ui_document(entity, ui_comp, true, false);
            }

            if(!ui_comp.document || !ui_comp.context)
            {
                return;
            }

            if(ui_comp.needs_stylesheet_reload)
            {
                ui_comp.document->ReloadStyleSheet();
                ui_comp.needs_stylesheet_reload = false;
            }

            int doc_w = (ui_comp.render_mode == ui_render_mode::screen_space_overlay) ? viewport_width : static_cast<int>(ui_comp.size.width);
            int doc_h = (ui_comp.render_mode == ui_render_mode::screen_space_overlay) ? viewport_height : static_cast<int>(ui_comp.size.height);

            if(ui_comp.render_mode == ui_render_mode::world_space)
            {
                dp_ratio = 1.0f;
            }
            ui_comp.context->SetDimensions(Rml::Vector2i(doc_w, doc_h));
            ui_comp.context->SetDensityIndependentPixelRatio(dp_ratio);
            ui_comp.context->Update();

            if(ctx.has<input_system>())
            {
                auto& input = ctx.get_cached<input_system>();
                if(input.manager.is_input_allowed())
                {
                    if(ui_comp.render_mode == ui_render_mode::screen_space_overlay)
                    {
                        auto mouse_x = input.manager.get_mouse().get_position().x;
                        auto mouse_y = input.manager.get_mouse().get_position().y;


                        if(process_mouse_move(scn, ui_comp.context, static_cast<int>(mouse_x), static_cast<int>(mouse_y)))
                        {
                            hit_found = true;
                        }
                        
                    }
                    else if(ui_comp.render_mode == ui_render_mode::world_space)
                    {
                        auto& transform = handle.get<transform_component>();
                        float dist = math::distance(cam.get_position(), transform.get_position_global());
                        world_space_docs.push_back({handle, dist});
                    }
                }
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
                if(ui_comp.document->IsVisible())
                {
                    ui_comp.document->Hide();
                }
            }
        });

    if(ctx.has<input_system>() && !world_space_docs.empty())
    {
        auto& input = ctx.get_cached<input_system>();
        if(input.manager.is_input_allowed())
        {
            std::sort(world_space_docs.begin(), world_space_docs.end(),
                      [](const world_space_doc& a, const world_space_doc& b) { return a.distance < b.distance; });

            auto mouse_x = input.manager.get_mouse().get_position().x;
            auto mouse_y = input.manager.get_mouse().get_position().y;

            for(const auto& entry : world_space_docs)
            {
                auto& ui_comp = entry.handle.get<ui_document_component>();
                if(!ui_comp.context)
                    continue;

                int px = -1;
                int py = -1;
                if(!hit_found)
                {
                    auto& transform = entry.handle.get<transform_component>();
                    const auto scale = ui_comp.get_world_space_scale();
                    const auto quad_transform = transform.get_transform_global() * math::transform::scaling(scale);
                    math::vec2 pixel;
                    if(cam.project_to_quad(math::vec2(mouse_x, mouse_y), quad_transform, ui_comp.size.width,
                                          ui_comp.size.height, pixel))
                    {
                        px = static_cast<int>(pixel.x);
                        py = static_cast<int>(pixel.y);
                    }
                }

                if(process_mouse_move(scn, ui_comp.context, px, py))
                {
                    hit_found = true;
                }

            }
        }
    }
}

auto ui_system::load_ui_document(entt::entity entity, ui_document_component& component, bool reload_stylesheet, bool log_error) -> bool
{
    if(!component.asset)
    {
        if(log_error)
        {
            APPLOG_WARNING("Cannot load document: asset is empty");
        }
        return false;
    }
    auto asset = component.asset.get();
    if(!asset)
    {
        if(log_error)
        {
            APPLOG_WARNING("Cannot load document: document_path is empty");
        }
        return false;
    }

    if(!component.context)
    {
        Rml::String context_name = "doc_" + std::to_string(entt::to_integral(entity));
        int w = (component.render_mode == ui_render_mode::screen_space_overlay) ? 1024 : static_cast<int>(component.size.width);
        int h = (component.render_mode == ui_render_mode::screen_space_overlay) ? 768 : static_cast<int>(component.size.height);
        component.context = Rml::CreateContext(context_name, Rml::Vector2i(w, h));
        if(!component.context)
        {
            if(log_error)
            {
                APPLOG_ERROR("Failed to create RmlUi context for document");
            }
            return false;
        }
    }

    auto real = fs::resolve_protocol(component.asset.id());
    Rml::String url_safe_path = Rml::StringUtilities::Replace(real.string(), ':', '|');

    auto raw_document = component.context->LoadDocumentFromMemory(asset->content, url_safe_path);
    if(!raw_document)
    {
        if(log_error)
        {
            APPLOG_ERROR("Failed to load UI document: {}", component.asset.id());
        }
        return false;
    }

    raw_document->SetId("body");

    if(component.document)
    {
        component.document->Close();
    }
    component.document = raw_document;
    component.version = component.asset.version();

    if(reload_stylesheet)
    {
        component.needs_stylesheet_reload = true;
    }

    return true;
}

void ui_system::ensure_document_framebuffer(ui_document_component& comp)
{
    if(comp.framebuffer && comp.framebuffer->is_valid())
    {
        auto sz = comp.framebuffer->get_size();
        if(sz.width == comp.size.width && sz.height == comp.size.height)
        {
            return;
        }
    }

    uint64_t texture_flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    auto color_texture = std::make_shared<gfx::texture>(static_cast<uint16_t>(comp.size.width),
                                                        static_cast<uint16_t>(comp.size.height),
                                                        false,
                                                        1,
                                                        gfx::texture_format::RGBA8,
                                                        texture_flags);

    if(!color_texture->is_valid())
    {
        APPLOG_ERROR("Failed to create UI document color texture");
        return;
    }

    std::vector<gfx::texture::ptr> textures;
    textures.push_back(color_texture);
    comp.framebuffer = std::make_shared<gfx::frame_buffer>(textures);
    if(!comp.framebuffer->is_valid())
    {
        APPLOG_ERROR("Failed to create UI document framebuffer");
        comp.framebuffer.reset();
    }
}

} // namespace unravel
