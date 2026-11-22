#pragma once
#include "inspector.h"

#include <engine/rendering/ecs/components/model_component.h>

#include <string>
#include <vector>

namespace unravel
{

/// One model's morph target weights before and after an edit, for record_morph_weights_change.
struct morph_weights_change
{
    entt::handle entity;
    std::vector<morph_weight> before;
    std::vector<morph_weight> after;
};

/**
 * @brief Applies @p changes as one undo step named @p action_name: each model gets its weights after the edit, undo
 *        puts the ones before back, and every weight that differs is marked as a prefab override of its instance by
 *        its target's name. A change that alters no weight is left out; with none left nothing is recorded. The Model
 *        inspector's sliders and the MCP weights tool both edit through it.
 */
void record_morph_weights_change(rtti::context& ctx,
                                 const std::vector<morph_weights_change>& changes,
                                 const std::string& action_name);

/**
 * @brief The Model component's rows, then a Morph Targets row that opens to a weight row for each morph target (blend
 *        shape) of its mesh, drawn as the inspector draws every property: a label column, the ranged slider, the
 *        inspector's search, mixed values across a selection, prefab overrides, and a gutter mark with a revert button
 *        on each target whose weight differs from the mesh's default.
 */
struct inspector_model_component : public crtp_meta_type<inspector_model_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;

private:
    void draw_morph_targets_section(rtti::context& ctx, model_component& data);
    /// One target's row; a finished drag records its undo step.
    void draw_morph_weight_row(rtti::context& ctx,
                               model_component& data,
                               const morph_target& target,
                               const std::string& label,
                               const std::vector<entt::handle>& edited);

    /// Lists only the targets whose weight moves the mesh.
    bool show_active_only_{};
    /// Each edited model's weights when a slider was grabbed: its release records one undo step from them.
    std::vector<morph_weights_change> edit_changes_;
};

REFLECT_INSPECTOR_INLINE(inspector_model_component, model_component)

} // namespace unravel
