#pragma once

#include <editor/hub/panels/inspector_panel/inspectors/inspector.h>
#include <engine/animation/animation_controller.h>

#include <functional>

namespace unravel
{

/**
 * @brief The controller the Animation Controller window inspects the definitions of.
 *
 * While one is open, the inspectors below draw a state, transition, parameter or layer of it like
 * any reflected type - the generic rows, groups, tooltips and visibility rules - and the fields
 * that need the controller their own way: names (a rename keeps every reference), parameter and
 * state pickers, conditions by their parameter's type, the blend plot and the mask's bones.
 * Without one they draw like the generic inspector.
 */
class animation_controller_inspect_scope
{
public:
    struct context
    {
        animation_controller* controller{};
        /// The layer states and transitions are inspected in.
        size_t layer{};
        /// Draws the plot of a blend state's points: live weights and the parameter marker.
        std::function<inspect_result(animation_state_def& state)> draw_blend_plot;
        /// Draws the bones of the skeleton a layer mask picks from.
        std::function<inspect_result(animation_layer_def& layer)> draw_mask_bones;
    };

    explicit animation_controller_inspect_scope(context scope_context);
    ~animation_controller_inspect_scope();
    animation_controller_inspect_scope(const animation_controller_inspect_scope&) = delete;
    auto operator=(const animation_controller_inspect_scope&) -> animation_controller_inspect_scope& = delete;

    /// The open scope's context, or nullptr.
    static auto get_current() -> const context*;
    /// Puts the keyboard into the next name field drawn (F2, double-click, a new item).
    static void request_name_focus();
    /// Drops a focus request no name field took (the selection has none).
    static void cancel_name_focus();

private:
    context context_;
    const context* previous_{};
};

/// Parameter: a rename keeps conditions and blend axes; a type change keeps conditions valid.
struct inspector_animation_parameter_def : public crtp_meta_type<inspector_animation_parameter_def, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

/// Condition: one row - the parameter, a comparison its type has, a threshold for numbers.
struct inspector_animation_condition_def : public crtp_meta_type<inspector_animation_condition_def, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

/// State: a rename keeps transitions; parameter pickers; the blend plot above the points.
struct inspector_animation_state_def : public crtp_meta_type<inspector_animation_state_def, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

/// Transition: the source as text, the destination from the layer's states.
struct inspector_animation_transition_def : public crtp_meta_type<inspector_animation_transition_def, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

/// Layer: unique names, the default state from its states, the mask with the skeleton's bones.
struct inspector_animation_layer_def : public crtp_meta_type<inspector_animation_layer_def, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_animation_parameter_def, animation_parameter_def)
REFLECT_INSPECTOR_INLINE(inspector_animation_condition_def, animation_condition_def)
REFLECT_INSPECTOR_INLINE(inspector_animation_state_def, animation_state_def)
REFLECT_INSPECTOR_INLINE(inspector_animation_transition_def, animation_transition_def)
REFLECT_INSPECTOR_INLINE(inspector_animation_layer_def, animation_layer_def)

} // namespace unravel
