#include "ui_system.h"
#include "../../rmlui/RmlUi_SystemInterface.h"
#include "../components/ui_document_component.h"

#include <engine/ecs/ecs.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/input/input.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/ui/ui_element_queries.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <RmlUi/Debugger/Debugger.h>

#include <algorithm>

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

    auto is_touch_event(const os::event& event) -> bool
    {
        switch(event.type)
        {
        case os::events::finger_down:
        case os::events::finger_motion:
        case os::events::finger_up:
        case os::events::finger_canceled:
            return true;
        default:
            return false;
        }
    }

    /// The platform also makes mouse events from a finger; the documents take the finger itself.
    auto is_mouse_from_touch(const os::event& event) -> bool
    {
        switch(event.type)
        {
        case os::events::mouse_button:
            return event.button.is_from_touch;
        case os::events::mouse_motion:
            return event.motion.is_from_touch;
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
    else if(input_allowed && is_touch_event(event))
    {
        dispatch_touch_event(ctx, scene, event);
    }
    else if(input_allowed && is_mouse_from_touch(event))
    {
        consume_touch_mouse_press(ctx, scene, event);
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

    std::vector<Rml::Context*> content_contexts;
    if(is_pointer_tracking_event(event))
    {
        content_contexts = refresh_mouse_state(ctx, scn);
    }
    const auto is_on_content = [&](const Rml::Context* context)
    {
        return std::find(content_contexts.begin(), content_contexts.end(), context) != content_contexts.end();
    };

    if(Rml::Debugger::IsVisible() && debug_context_)
    {
        if(process_event(debug_context_, event, is_on_content(debug_context_)))
        {
            result.target_context = debug_context_;
        }
    }

    scn.registry->view<ui_document_component, active_component>().each(
        [&](entt::entity, ui_document_component& ui_comp, active_component&)
        {
            if(ui_comp.context && ui_comp.is_enabled())
            {
                if(process_event(ui_comp.context, event, is_on_content(ui_comp.context)))
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

auto ui_system::process_mouse_move(Rml::Context* context, int x, int y) -> bool
{
    if(!context->ProcessMouseMove(x, y, 0))
    {
        const Rml::Vector2f point(static_cast<float>(x), static_cast<float>(y));
        if(is_ui_content_at(context->GetHoverElement(), point))
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

auto ui_system::process_event(Rml::Context* context, os::event& event, bool is_pointer_on_content) -> bool
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
            return is_pointer_on_content;
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

auto ui_system::refresh_mouse_state(rtti::context& ctx, scene& scn) -> std::vector<Rml::Context*>
{
    std::vector<Rml::Context*> content_contexts;
    auto& input = ctx.get_cached<input_system>();
    if(!input.manager.is_input_allowed())
    {
        return content_contexts;
    }

    visit_pointer_documents(scn,
                            input.manager.get_mouse().get_position(),
                            [&](Rml::Context* context, bool is_over, Rml::Vector2f position)
                            {
                                if(!is_over)
                                {
                                    context->ProcessMouseLeave();
                                }
                                else if(process_mouse_move(context,
                                                           static_cast<int>(position.x),
                                                           static_cast<int>(position.y)))
                                {
                                    content_contexts.push_back(context);
                                }
                            });
    return content_contexts;
}

void ui_system::visit_pointer_documents(scene& scn, input::coord position, const pointer_visitor& visit)
{
    const Rml::Vector2f screen_position(static_cast<float>(position.x), static_cast<float>(position.y));

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
        visit(debug_context_, true, screen_position);
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
                visit(ui_comp.context, true, screen_position);
                return;
            }

            if(!scene_camera)
            {
                visit(ui_comp.context, false, {});
                return;
            }

            auto handle = scn.create_handle(entity);
            auto& transform = handle.get<transform_component>();
            const auto quad_transform = ui_comp.get_world_space_quad(transform.get_transform_global());
            math::vec2 pixel;
            const bool is_over = scene_camera->project_to_quad(math::vec2(screen_position.x, screen_position.y),
                                                               quad_transform,
                                                               ui_comp.size.width,
                                                               ui_comp.size.height,
                                                               pixel);
            visit(ui_comp.context, is_over, Rml::Vector2f(pixel.x, pixel.y));
        });
}

void ui_system::dispatch_touch_event(rtti::context& ctx, scene& scn, os::event& event)
{
    auto& input = ctx.get_cached<input_system>();
    const input::coord position = input.manager.map_finger_position(event.finger);
    const auto identifier = static_cast<Rml::TouchId>(event.finger.finger_id);
    // Touches carry no key modifiers.
    constexpr int key_modifiers = 0;
    Rml::Context* target_context = nullptr;

    visit_pointer_documents(scn,
                            position,
                            [&](Rml::Context* context, bool is_over, Rml::Vector2f document_position)
                            {
                                const Rml::TouchList touches{Rml::Touch{identifier, document_position}};
                                switch(event.type)
                                {
                                    case os::events::finger_down:
                                        if(is_over)
                                        {
                                            context->ProcessTouchStart(touches, key_modifiers);
                                            if(is_ui_content_at(context->GetHoverElement(), document_position))
                                            {
                                                target_context = context;
                                            }
                                        }
                                        break;
                                    case os::events::finger_motion:
                                        if(is_over)
                                        {
                                            context->ProcessTouchMove(touches, key_modifiers);
                                        }
                                        break;
                                    case os::events::finger_up:
                                        // A finger that leaves a world-space document lets go without a click.
                                        if(is_over)
                                        {
                                            context->ProcessTouchEnd(touches, key_modifiers);
                                        }
                                        else
                                        {
                                            context->ProcessTouchCancel(touches);
                                        }
                                        break;
                                    case os::events::finger_canceled:
                                        context->ProcessTouchCancel(touches);
                                        break;
                                    default:
                                        break;
                                }
                            });

    if(event.type != os::events::finger_down)
    {
        return;
    }
    // As a mouse press: a finger off the UI takes the focus from every document, a finger on it from
    // the other documents, and the game does not see it.
    if(target_context == nullptr)
    {
        blur_ui_context(debug_context_);
        scn.registry->view<ui_document_component>().each(
            [&](entt::entity, ui_document_component& ui_comp)
            {
                blur_ui_context(ui_comp.context);
            });
        return;
    }
    blur_all_document_contexts(scn, target_context, *this);
    event = {};
}

void ui_system::consume_touch_mouse_press(rtti::context& ctx, scene& scn, os::event& event)
{
    if(event.type != os::events::mouse_button || event.button.state_id != os::state::pressed)
    {
        return;
    }
    auto& input = ctx.get_cached<input_system>();
    const input::coord position = input.manager.map_window_position({event.button.x, event.button.y});
    bool is_on_element = false;
    visit_pointer_documents(scn,
                            position,
                            [&](Rml::Context* context, bool is_over, Rml::Vector2f document_position)
                            {
                                if(is_over &&
                                   is_ui_content_at(context->GetElementAtPoint(document_position), document_position))
                                {
                                    is_on_element = true;
                                }
                            });
    if(is_on_element)
    {
        event = {};
    }
}

} // namespace unravel
