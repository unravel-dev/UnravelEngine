#include "inspector_rig_components.h"
#include "inspectors.h"

#include <editor/editing/rig_setup.h>
#include <engine/animation/animation_player.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/systems/rig_system.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/play_mode.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace unravel
{
namespace
{
constexpr ImU32 COL_REACHED = INSPECTOR_STATUS_ACTIVE_COLOR;
constexpr ImU32 COL_UNREACHED = IM_COL32(240, 150, 60, 255);
constexpr ImU32 COL_IDLE = INSPECTOR_STATUS_IDLE_COLOR;
/// Weights below this count as off.
constexpr float MIN_ACTIVE_WEIGHT = 1e-3f;
/// A spring chain swings only with a child below its root.
constexpr size_t MIN_SPRING_CHAIN_JOINTS = 2;

auto find_transform(entt::handle entity) -> transform_component*
{
    return entity && entity.valid() ? entity.try_get<transform_component>() : nullptr;
}

/// Rigs built from separate models are drawn from their transforms: the solve shows in play mode.
void draw_plain_rig_hint(rtti::context& ctx, entt::handle owner)
{
    if(!owner || owner.all_of<bone_component>() || ctx.get_cached<play_mode>().is_active())
    {
        return;
    }
    draw_inspector_hint(ICON_MDI_INFORMATION_OUTLINE " Plain hierarchy: the solved pose shows in play mode. The gizmo previews it.");
}

/// One line saying what the modifier did on the last frame.
void draw_solve_status(const ik_debug_state& debug, bool has_target, const char* missing_hint)
{
    ImGui::Spacing();
    if(!has_target)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE), ICON_MDI_INFORMATION_OUTLINE " %s", missing_hint);
        return;
    }
    if(!debug.solved || debug.effective_weight < MIN_ACTIVE_WEIGHT)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_PAUSE_CIRCLE_OUTLINE " Idle (weight %.2f)",
                           debug.effective_weight);
        return;
    }
    const ImU32 color = debug.reached ? COL_REACHED : COL_UNREACHED;
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color),
                       "%s  weight %.2f",
                       debug.reached ? ICON_MDI_CHECK_CIRCLE " Reached" : ICON_MDI_ALERT_CIRCLE_OUTLINE " Out of reach",
                       debug.effective_weight);
}

/// Create Target / Create Pole for the reaching modifiers (two-bone and chain IK).
template<typename Component>
void draw_reach_tools(rtti::context& ctx, Component& data, rig_setup::ik_modifier modifier, inspect_result& result)
{
    auto owner = data.get_owner();
    if(find_transform(owner) == nullptr)
    {
        return;
    }
    const bool shows_target_button = !data.target;
    if(shows_target_button && ImGui::Button(ICON_MDI_TARGET " Create Target"))
    {
        rig_setup::create_ik_target(ctx, owner, modifier);
        result.changed = true;
        result.edit_finished = true;
        result.change_recorded = true;
    }
    if(shows_target_button && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Creates a target at the bone's animated position and assigns it.");
    }
    if(data.pole || !rig_setup::can_create_ik_pole(owner, modifier))
    {
        return;
    }
    if(shows_target_button)
    {
        ImGui::SameLine();
    }
    if(ImGui::Button(ICON_MDI_VECTOR_LINE " Create Pole"))
    {
        rig_setup::create_ik_pole(ctx, owner, modifier);
        result.changed = true;
        result.edit_finished = true;
        result.change_recorded = true;
    }
    if(ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Creates a pole in front of the middle joint, along its current bend, and assigns it.");
    }
}

auto describe_found(const char* slot, entt::handle bone) -> std::string
{
    auto* tag = bone && bone.valid() ? bone.try_get<tag_component>() : nullptr;
    return std::string(slot) + ": " + (tag != nullptr ? tag->name : std::string("not found"));
}

/// What Auto Setup found, a line per slot.
auto describe_found_bones(const rig_setup::foot_ik_bones& bones) -> std::string
{
    return describe_found("Left foot", bones.left_foot) + "\n" + describe_found("Right foot", bones.right_foot) + "\n" +
           describe_found("Pelvis", bones.pelvis);
}
} // namespace

auto inspector_two_bone_ik_component::inspect(rtti::context& ctx,
                                              entt::meta_any& var,
                                              const meta_any_proxy& var_proxy,
                                              const var_info& info,
                                              const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<two_bone_ik_component&>();
    ImGui::PushID("two_bone_ik_tools");
    draw_solve_status(data.debug, bool(data.target), "Assign a target, or create one.");
    draw_plain_rig_hint(ctx, data.get_owner());
    draw_reach_tools(ctx, data, rig_setup::ik_modifier::two_bone, result);
    ImGui::PopID();
    return result;
}

auto inspector_chain_ik_component::inspect(rtti::context& ctx,
                                           entt::meta_any& var,
                                           const meta_any_proxy& var_proxy,
                                           const var_info& info,
                                           const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<chain_ik_component&>();
    ImGui::PushID("chain_ik_tools");
    draw_solve_status(data.debug, bool(data.target), "Assign a target, or create one.");
    draw_plain_rig_hint(ctx, data.get_owner());
    draw_reach_tools(ctx, data, rig_setup::ik_modifier::chain, result);
    ImGui::PopID();
    return result;
}

auto inspector_look_at_ik_component::inspect(rtti::context& ctx,
                                             entt::meta_any& var,
                                             const meta_any_proxy& var_proxy,
                                             const var_info& info,
                                             const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<look_at_ik_component&>();
    ImGui::PushID("look_at_ik_tools");
    ImGui::Spacing();
    if(!data.target)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE), ICON_MDI_INFORMATION_OUTLINE " Assign a target, or create one.");
    }
    else
    {
        const bool looking = data.smoothed_weight >= MIN_ACTIVE_WEIGHT;
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(looking ? COL_REACHED : COL_IDLE),
                           "%s  weight %.2f",
                           looking ? ICON_MDI_EYE " Looking" : ICON_MDI_EYE_OFF " Faded out",
                           data.smoothed_weight);
    }
    auto owner = data.get_owner();
    if(find_transform(owner) != nullptr && !data.target)
    {
        if(ImGui::Button(ICON_MDI_TARGET " Create Target"))
        {
            rig_setup::create_ik_target(ctx, owner, rig_setup::ik_modifier::look_at);
            result.changed = true;
            result.edit_finished = true;
            result.change_recorded = true;
        }
        ImGui::SetItemTooltipEx("Creates a target in front of the eyes and assigns it.");
    }
    ImGui::PopID();
    return result;
}

auto inspector_foot_ik_component::inspect(rtti::context& ctx,
                                          entt::meta_any& var,
                                          const meta_any_proxy& var_proxy,
                                          const var_info& info,
                                          const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<foot_ik_component&>();
    ImGui::PushID("foot_ik_tools");
    ImGui::Spacing();
    const bool assigned = data.left_foot && data.right_foot && data.pelvis;
    if(!assigned)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_INFORMATION_OUTLINE " Assign both feet and the pelvis, or use Auto Setup.");
    }
    else
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(data.left_debug.solved || data.right_debug.solved ? COL_REACHED : COL_IDLE),
                           ICON_MDI_FOOT_PRINT " Left %.2f  Right %.2f  Pelvis %+.3f m",
                           data.left_weight,
                           data.right_weight,
                           data.pelvis_offset);
        if(!ctx.get_cached<play_mode>().is_active())
        {
            draw_inspector_hint("Ground probes use physics: feet plant in play mode.");
        }
    }
    if(ImGui::Button(ICON_MDI_AUTO_FIX " Auto Setup"))
    {
        auto_setup_report_ = describe_found_bones(rig_setup::auto_setup_foot_ik(ctx, data.get_owner()));
        auto_setup_owner_ = data.get_owner();
        result.changed = true;
        result.edit_finished = true;
        result.change_recorded = true;
    }
    ImGui::SetItemTooltipEx("Finds the feet and the hips of this character's skeleton by humanoid role (from the "
                            "skeleton's shape) and assigns the ones it finds.");
    if(!auto_setup_report_.empty() && auto_setup_owner_ == data.get_owner())
    {
        draw_inspector_hint(auto_setup_report_);
    }
    ImGui::PopID();
    return result;
}

auto inspector_spring_bone_component::inspect(rtti::context& ctx,
                                              entt::meta_any& var,
                                              const meta_any_proxy& var_proxy,
                                              const var_info& info,
                                              const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<spring_bone_component&>();
    ImGui::PushID("spring_bone_tools");
    ImGui::Spacing();
    const int joints = int(data.debug.joints.size());
    std::vector<entt::handle> chain;
    collect_rig_spring_chain(data.get_owner(), data.chain_length, chain);
    if(data.debug.solved)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_REACHED),
                           ICON_MDI_WAVES " Simulating %d bones",
                           std::max(joints - 1, 0));
    }
    else if(chain.size() < MIN_SPRING_CHAIN_JOINTS)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_INFORMATION_OUTLINE " Needs at least one child bone below this one.");
    }
    else
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_PAUSE_CIRCLE_OUTLINE " Idle (weight %.2f)",
                           data.weight);
    }
    draw_plain_rig_hint(ctx, data.get_owner());
    if(ImGui::Button(ICON_MDI_RESTART " Reset Simulation"))
    {
        data.reset_simulation();
    }
    ImGui::PopID();
    return result;
}

} // namespace unravel
