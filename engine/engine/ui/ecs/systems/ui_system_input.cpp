#include "ui_system.h"
#include "../../rmlui/RmlUi_SystemInterface.h"
#include "../components/ui_document_component.h"

#include <engine/ecs/ecs.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/input/input.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/ecs/components/camera_component.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <RmlUi/Debugger/Debugger.h>

namespace unravel
{
namespace
{
    auto is_keyboard_event(const os::event& event) -> bool
    {
        switch(event.type)
        {
        case os::events::key_down:
        case os::events::key_up:
        case os::events::text_input:
            return true;
        default:
            return false;
        }
    }

    auto is_release_event(const os::event& event) -> bool
    {
        switch(event.type)
        {
        case os::events::key_up:
            return true;
        case os::events::mouse_button:
            return event.button.state_id == os::state::released;
        default:
            return false;
        }
    }

    auto is_pointer_tracking_event(const os::event& event) -> bool
    {
        switch(event.type)
        {
        case os::events::mouse_button:
        case os::events::mouse_motion:
        case os::events::mouse_wheel:
            return true;
        default:
            return false;
        }
    }

    auto context_wants_keyboard_input(Rml::Context* context) -> bool
    {
        if(!context)
        {
            return false;
        }

        Rml::Element* focus = context->GetFocusElement();
        if(!focus || focus == context->GetRootElement())
        {
            return false;
        }

        Rml::Element* leaf = focus->GetFocusLeafNode();
        if(!leaf || leaf == context->GetRootElement())
        {
            return false;
        }

        const Rml::String& tag = leaf->GetTagName();
        return tag == "input" || tag == "textarea" || tag == "select";
    }

    void blur_all_document_contexts(scene& scn, const Rml::Context* except, const ui_system& system)
    {
        scn.registry->view<ui_document_component>().each(
            [&](entt::entity, ui_document_component& ui_comp)
            {
                if(ui_comp.context && ui_comp.context != except)
                {
                    system.blur_ui_context(ui_comp.context);
                }
            });
    }
} // namespace

void ui_system::on_os_event(rtti::context& ctx, os::event& event)
{
    auto& scene = ctx.get_cached<ecs>().get_scene();
    const bool input_allowed = ctx.get_cached<input_system>().manager.is_input_allowed();

    if(input_allowed && is_keyboard_event(event))
    {
        if(try_consume_keyboard_event(scene, event))
        {
            event = {};
            return;
        }
    }
    else if(input_allowed)
    {
        const ui_pointer_dispatch_result pointer = dispatch_pointer_event(ctx, scene, event);
        apply_mouse_press_focus(scene, event, pointer.target_context, input_allowed);
    }
}

auto ui_system::find_keyboard_context(scene& scn) const -> Rml::Context*
{
    if(Rml::Debugger::IsVisible() && debug_context_ && context_wants_keyboard_input(debug_context_))
    {
        return debug_context_;
    }

    Rml::Context* keyboard_context = nullptr;
    scn.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity, ui_document_component& ui_comp, active_component&)
        {
            if(ui_comp.context && ui_comp.is_enabled() && context_wants_keyboard_input(ui_comp.context))
            {
                keyboard_context = ui_comp.context;
            }
        });
    return keyboard_context;
}

auto ui_system::try_consume_keyboard_event(scene& scn, os::event& event) -> bool
{
    Rml::Context* keyboard_context = find_keyboard_context(scn);
    if(!keyboard_context)
    {
        return false;
    }

    // Block game keyboard while a text field is focused, but always let releases through
    // so keys/buttons pressed before UI focus cannot get stuck in the engine.
    (void)RmlEngine::input_event_handler(keyboard_context, event);
    return !is_release_event(event);
}

auto ui_system::dispatch_pointer_event(rtti::context& ctx, scene& scn, os::event& event)
    -> ui_pointer_dispatch_result
{
    ui_pointer_dispatch_result result;

    if(is_pointer_tracking_event(event))
    {
        refresh_mouse_state(ctx, scn);
    }

    if(Rml::Debugger::IsVisible() && debug_context_)
    {
        if(process_event(scn, debug_context_, event))
        {
            result.target_context = debug_context_;
        }
    }

    scn.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity, ui_document_component& ui_comp, active_component&)
        {
            if(ui_comp.context && ui_comp.is_enabled())
            {
                if(process_event(scn, ui_comp.context, event))
                {
                    result.target_context = ui_comp.context;
                }
            }
        });

    return result;
}

void ui_system::apply_mouse_press_focus(scene& scn,
                                        os::event& event,
                                        Rml::Context* target_context,
                                        bool input_allowed)
{
    if(event.type != os::events::mouse_button)
    {
        return;
    }

    if(event.button.state_id == os::state::released)
    {
        // Always propagate mouse releases to the engine to clear pressed states.
        return;
    }

    if(event.button.state_id != os::state::pressed)
    {
        return;
    }

    if(!target_context && input_allowed)
    {
        blur_ui_context(debug_context_);
        scn.registry->view<ui_document_component>().each(
            [&](entt::entity, ui_document_component& ui_comp)
            {
                blur_ui_context(ui_comp.context);
            });
        return;
    }

    if(target_context || !input_allowed)
    {
        const Rml::Context* except = (input_allowed && target_context) ? target_context : nullptr;
        blur_all_document_contexts(scn, except, *this);

        if(target_context)
        {
            event = {};
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
        if(static_cast<Rml::Element*>(ui_comp.document) == element)
        {
            return false;
        }
    }
    return true;
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
    const bool propagate = RmlEngine::input_event_handler(context, event);
    if(!propagate)
    {
        if(is_keyboard_event(event))
        {
            return true;
        }
    }
    if(propagate)
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

void ui_system::blur_ui_context(Rml::Context* context) const
{
    if(!context)
    {
        return;
    }

    context->ProcessMouseLeave();

    Rml::Element* focus = context->GetFocusElement();
    if(focus && focus != context->GetRootElement())
    {
        focus->Blur();
    }
}

void ui_system::refresh_mouse_state(rtti::context& ctx, scene& scn)
{
    auto& input = ctx.get_cached<input_system>();
    if(!input.manager.is_input_allowed())
    {
        return;
    }

    const int mouse_x = static_cast<int>(input.manager.get_mouse().get_position().x);
    const int mouse_y = static_cast<int>(input.manager.get_mouse().get_position().y);

    const camera* scene_camera = nullptr;
    entt::handle camera_entity;
    scn.registry->view<camera_component, active_component>().each(
        [&](entt::entity entity, camera_component&, active_component&)
        {
            if(!scene_camera)
            {
                camera_entity = scn.create_handle(entity);
                scene_camera = &camera_entity.get<camera_component>().get_camera();
            }
        });

    if(debug_context_)
    {
        process_mouse_move(scn, debug_context_, mouse_x, mouse_y);
    }

    scn.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity entity, ui_document_component& ui_comp, active_component&)
        {
            if(!ui_comp.context || !ui_comp.is_enabled())
            {
                return;
            }

            if(ui_comp.render_mode == ui_render_mode::screen_space_overlay)
            {
                process_mouse_move(scn, ui_comp.context, mouse_x, mouse_y);
                return;
            }

            if(!scene_camera)
            {
                ui_comp.context->ProcessMouseLeave();
                return;
            }

            auto handle = scn.create_handle(entity);
            auto& transform = handle.get<transform_component>();
            const auto scale = ui_comp.get_world_space_scale();
            const auto quad_transform = transform.get_transform_global() * math::transform::scaling(scale);
            math::vec2 pixel;
            if(scene_camera->project_to_quad(math::vec2(static_cast<float>(mouse_x), static_cast<float>(mouse_y)),
                                             quad_transform, ui_comp.size.width, ui_comp.size.height, pixel))
            {
                process_mouse_move(scn, ui_comp.context, static_cast<int>(pixel.x), static_cast<int>(pixel.y));
            }
            else
            {
                ui_comp.context->ProcessMouseLeave();
            }
        });
}

} // namespace unravel
