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
#include <engine/rendering/camera.h>
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
#include <graphics/graphics.h>

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
    bool is_deploy_mode = ctx.has<deploy>();

    if(!is_deploy_mode)
    {
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

    }
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

void ui_system::update_ui_document_common(entt::handle handle, ui_document_component& ui_comp, bool is_playing)
{
    bool active = handle.all_of<active_component>();
    if(!is_playing && ui_comp.version != ui_comp.asset.version())
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
        load_ui_document(handle, ui_comp, false);
    }
    if(!ui_comp.document || !ui_comp.context)
    {
        return;
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
}                                 


void ui_system::update_world_space_document(rtti::context& ctx, scene& scn, entt::handle handle, ui_document_component& comp,
                                            const camera& cam, float dp_ratio, bool process_input, bool& hit_found)
{
    APP_SCOPE_PERF("UI/Update World Space Document");
    auto doc_size = Rml::Vector2i(static_cast<int>(comp.size.width), static_cast<int>(comp.size.height));
    comp.context->SetDimensions(doc_size);
    comp.context->SetDensityIndependentPixelRatio(dp_ratio);
    if(!process_input)
    {
        return;
    }
    auto& input = ctx.get_cached<input_system>();
    if(!input.manager.is_input_allowed())
    {
        return;
    }
    int px = -1;
    int py = -1;
    if(!hit_found)
    {
        auto& transform = handle.get<transform_component>();
        const auto scale = comp.get_world_space_scale();
        const auto quad_transform = transform.get_transform_global() * math::transform::scaling(scale);
        math::vec2 pixel;
        auto mouse_x = input.manager.get_mouse().get_position().x;
        auto mouse_y = input.manager.get_mouse().get_position().y;
        if(cam.project_to_quad(math::vec2(mouse_x, mouse_y), quad_transform, comp.size.width, comp.size.height, pixel))
        {
            px = static_cast<int>(pixel.x);
            py = static_cast<int>(pixel.y);
        }
    }
    if(process_mouse_move(scn, comp.context, px, py))
    {
        hit_found = true;
    }
}

void ui_system::update_screen_space_document(rtti::context& ctx, Rml::Context* context, int viewport_width,
                                            int viewport_height, float dp_ratio, scene& scn, bool process_input, bool& hit_found)
{
    APP_SCOPE_PERF("UI/Update Screen Space Document");
    auto doc_size = Rml::Vector2i(viewport_width, viewport_height);
    context->SetDimensions(doc_size);
    context->SetDensityIndependentPixelRatio(dp_ratio);
    if(!process_input)
    {
        return;
    }
    auto& input = ctx.get_cached<input_system>();
    if(input.manager.is_input_allowed())
    {
        auto mouse_x = input.manager.get_mouse().get_position().x;
        auto mouse_y = input.manager.get_mouse().get_position().y;
        if(process_mouse_move(scn, context, static_cast<int>(mouse_x), static_cast<int>(mouse_y)))
        {
            hit_found = true;
        }
    }
}

void ui_system::update_world_space(rtti::context& ctx, entt::handle camera_entity, scene& scn, bool& process_input)
{
    APP_SCOPE_PERF("UI/Update World Space");
    auto& camera_comp = camera_entity.get<camera_component>();
    const auto& cam = camera_comp.get_camera();
    auto& input = ctx.get_cached<input_system>();

    // world space ui does not depend on the viewport size, so we can use a fixed dp_ratio
    float dp_ratio = 1.0f;
   
    auto& ev = ctx.get_cached<events>();
    bool is_playing = ev.is_playing;

    
    /// State between update_world_space and render_world_space
    struct world_space_doc
    {
        entt::handle handle;
        ui_document_component* comp = nullptr;
        float distance = 0.0f;
    };
    std::vector<world_space_doc> world_space_visible;


    scn.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity entity, ui_document_component& ui_comp, active_component& active)
        {
            if(ui_comp.render_mode != ui_render_mode::world_space)
            {
                return;
            }
            auto handle = scn.create_handle(entity);
            update_ui_document_common(handle, ui_comp, is_playing);
            if(!ui_comp.context || !ui_comp.document || !ui_comp.is_enabled())
            {
                return;
            }
            if(!ui_comp.document->IsVisible())
            {
                return;
            }
            auto& transform_comp = handle.get<transform_component>();
            const auto& world_transform = transform_comp.get_transform_global();
            auto bbox = ui_comp.get_bounds();
            if(!cam.test_obb(bbox, world_transform))
            {
                return;
            }
            float dist = math::distance2(cam.get_position(), transform_comp.get_position_global());
            world_space_visible.push_back({handle, &ui_comp, dist});
        });
    std::sort(world_space_visible.begin(), world_space_visible.end(),
              [](const world_space_doc& a, const world_space_doc& b) { return a.distance < b.distance; });
    bool hit_found = false;
    for(const auto& entry : world_space_visible)
    {
        auto& ui_comp = *entry.comp;
        auto handle = entry.handle;
        update_world_space_document(ctx, scn, handle, ui_comp, cam, dp_ratio, process_input, hit_found);
    }

    if(process_input)
    {
        process_input = !hit_found;
    }
}

void ui_system::render_world_space(rtti::context& ctx, entt::handle camera_entity, scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("UI/Render World Space");
    (void)ctx;
    (void)camera_entity;
    (void)dt;
    const auto current_frame = static_cast<uint64_t>(gfx::get_render_frame());


    scn.registry->view<ui_document_component, active_component>().each(
    [&](entt::entity entity, ui_document_component& ui_comp, active_component& active)
    {
        if(ui_comp.render_mode != ui_render_mode::world_space)
        {
            return;
        }
        if(!ui_comp.context || !ui_comp.document || !ui_comp.is_enabled())
        {
            return;
        }
        if(!ui_comp.document->IsVisible())
        {
            return;
        }
        if(ui_comp.last_render_frame == current_frame)
        {
            return;
        }
        ui_comp.last_render_frame = current_frame;
        ensure_document_framebuffer(ui_comp);
        if(!ui_comp.framebuffer || !ui_comp.framebuffer->is_valid())
        {
            return;
        }
        if(!ui_comp.render_layer_stack)
        {
            ui_comp.render_layer_stack = std::make_shared<RmlUi_RenderLayerStack>();
        }
        auto sz = ui_comp.framebuffer->get_size();
        RmlUi_FrameState frame_state;
        frame_state.viewport_width = static_cast<int>(sz.width);
        frame_state.viewport_height = static_cast<int>(sz.height);
        frame_state.framebuffer = ui_comp.framebuffer;
        frame_state.clear_to_transparent = true;
        frame_state.render_layers = ui_comp.render_layer_stack;
        RmlUi_Backend_Engine::begin_frame(frame_state);
        ui_comp.context->Update();
        ui_comp.context->Render();
        RmlUi_Backend_Engine::end_frame();
    });
}

void ui_system::update_screen_space(rtti::context& ctx, entt::handle camera_entity, scene& scn, bool& process_input)
{
    APP_SCOPE_PERF("UI/Update Screen Space");
    auto& camera_comp = camera_entity.get<camera_component>();
    auto& input = ctx.get_cached<input_system>();
    auto& ev = ctx.get_cached<events>();
    bool is_playing = ev.is_playing;
    float screen_space_dp_ratio = 1.0f;
    auto viewport = camera_comp.get_viewport_size();
    float screen_space_viewport_width = static_cast<int>(viewport.width);
    float screen_space_viewport_height = static_cast<int>(viewport.height);
    {
        auto work_zone = input.manager.get_work_zone();
        if(work_zone.w > 0 && work_zone.h > 0)
        {
            screen_space_dp_ratio =
                (static_cast<float>(screen_space_viewport_width) / static_cast<float>(work_zone.w) +
                 static_cast<float>(screen_space_viewport_height) / static_cast<float>(work_zone.h)) *
                0.5f;
        }
    }
    bool hit_found = false;
    scn.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity entity, ui_document_component& ui_comp, active_component& active)
        {
            if(ui_comp.render_mode != ui_render_mode::screen_space_overlay)
            {
                return;
            }
            auto handle = scn.create_handle(entity);
            update_ui_document_common(handle, ui_comp, is_playing);
            if(!ui_comp.context || !ui_comp.document || !ui_comp.is_enabled())
            {
                return;
            }
            if(!ui_comp.document->IsVisible())
            {
                return;
            }
            update_screen_space_document(ctx, ui_comp.context, screen_space_viewport_width,
                                        screen_space_viewport_height, screen_space_dp_ratio, scn, process_input,
                                        hit_found);
        });
    if(debug_context_)
    {
        bool debug_hit_found = false;
        update_screen_space_document(ctx, debug_context_, screen_space_viewport_width,
                                    screen_space_viewport_height, screen_space_dp_ratio, scn, process_input,
                                    debug_hit_found);
    }

    if(process_input)
    {
        process_input = !hit_found;
    }
}

void ui_system::render_screen_space(rtti::context& ctx, const gfx::frame_buffer::ptr& output,
                                   entt::handle camera_entity, scene& scn, delta_t dt)
{
    if(!output)
    {
        return;
    }
    APP_SCOPE_PERF("UI/Render Screen Space");
    (void)ctx;
    (void)camera_entity;
    (void)dt;
    scn.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity entity, ui_document_component& ui_comp, active_component& active)
    {
        if(ui_comp.render_mode != ui_render_mode::screen_space_overlay)
        {
            return;
        }
        if(!ui_comp.context || !ui_comp.document || !ui_comp.is_enabled())
        {
            return;
        }
        if(!ui_comp.document->IsVisible())
        {
            return;
        }

        if(!ui_comp.render_layer_stack)
        {
            ui_comp.render_layer_stack = std::make_shared<RmlUi_RenderLayerStack>();
        }
        auto sz = output->get_size();
        RmlUi_FrameState frame_state;
        frame_state.viewport_width = static_cast<int>(sz.width);
        frame_state.viewport_height = static_cast<int>(sz.height);
        frame_state.framebuffer = output;
        frame_state.clear_to_transparent = false;
        frame_state.render_layers = ui_comp.render_layer_stack;
        RmlUi_Backend_Engine::begin_frame(frame_state);
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
        debug_context_->Update();
        debug_context_->Render();
        RmlUi_Backend_Engine::end_frame();
    }
}

void ui_system::on_os_event(rtti::context& ctx, os::event& event)
{
    auto& ecs_system = ctx.get_cached<ecs>();
    auto& scene = ecs_system.get_scene();
    auto& input = ctx.get_cached<input_system>();
    bool is_input_allowed = input.manager.is_input_allowed();
    Rml::Context* target_context = nullptr;

    if(is_input_allowed)
    {
        if(Rml::Debugger::IsVisible() && debug_context_)
        {
            if(process_event(scene, debug_context_, event))
            {
                target_context = debug_context_;
            }
        }
        
        scene.registry->view<ui_document_component, active_component>().each(
            [&](entt::entity, ui_document_component& ui_comp, active_component& active)
            {
                if(ui_comp.context && ui_comp.is_enabled())
                {
                    if(process_event(scene, ui_comp.context, event))
                    {
                        target_context = ui_comp.context;
                    }
                }
            });
    }
    

    if(target_context || !is_input_allowed)
    {

        if(event.type == os::events::mouse_button)
        {
            scene.registry->view<ui_document_component>().each(
                [&](entt::entity, ui_document_component& ui_comp)
                {
                 
                    if(ui_comp.context && (ui_comp.context != target_context || !is_input_allowed))
                    {
                        ui_comp.context->ProcessMouseLeave();
    
                        auto focus_element = ui_comp.context->GetFocusElement();
                        if(focus_element)
                        {
                            focus_element->Blur();
                        }
                    }
                });

            if(target_context)
            {
                event = {};
            }

        }
        
    }
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
    load_font("engine:/data/fonts/Inter/InterVariable.ttf");
    load_font("engine:/data/fonts/Inter/InterVariable-Italic.ttf");

    load_font("engine:/data/fonts/OpenSans/OpenSans-VariableFont_wdth,wght.ttf");
    load_font("engine:/data/fonts/OpenSans/OpenSans-Italic-VariableFont_wdth,wght.ttf");

    load_font("engine:/data/fonts/RobotoMono/RobotoMono-VariableFont_wght.ttf");
    load_font("engine:/data/fonts/RobotoMono/RobotoMono-Italic-VariableFont_wght.ttf");
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
        system.load_ui_document(e, component, true);
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

void ui_system::release_resources()
{
    Rml::ReleaseFontResources();
    Rml::ReleaseRenderManagers();
    Rml::ReleaseTextures();
    Rml::ReleaseCompiledGeometry();
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

void ui_system::update_ui_documents(rtti::context& ctx, scene& scn)
{
    auto& ev = ctx.get_cached<events>();
    APP_SCOPE_PERF("UI/Update UI Documents");

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
                load_ui_document(entity, ui_comp, false);
            }
            if(!ui_comp.document || !ui_comp.context)
            {
                return;
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
}

auto ui_system::load_ui_document(entt::entity entity, ui_document_component& component, bool log_error) -> bool
{
    APP_SCOPE_PERF("UI/Load UI Document");
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
    raw_document->ReloadStyleSheet();
    if(component.document)
    {
        component.document->Close();
    }
    component.document = raw_document;
    component.version = component.asset.version();

    return true;
}

void ui_system::ensure_document_framebuffer(ui_document_component& comp)
{
    if(gfx::needs_recreate(comp.framebuffer, comp.size))
    {
        comp.framebuffer.reset();

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
        comp.framebuffer.reset();
        comp.framebuffer = std::make_shared<gfx::frame_buffer>(textures);
        if(!comp.framebuffer->is_valid())
        {
            APPLOG_ERROR("Failed to create UI document framebuffer");
            comp.framebuffer.reset();
        }
    }

}

} // namespace unravel
