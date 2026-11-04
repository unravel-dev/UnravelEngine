#include "inspector_physics_components.h"
#include "inspectors.h"
#include <editor/editing/editing_manager.h>
#include <engine/play_mode.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <imgui/imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace physics_inspector_detail
{
constexpr ImU32 COL_ACTIVE = IM_COL32(96, 200, 110, 255);
constexpr ImU32 COL_IDLE = IM_COL32(150, 150, 150, 255);
constexpr ImU32 COL_BROKEN = IM_COL32(230, 90, 80, 255);
constexpr float MIN_TOTAL_MASS = 1.0f;
constexpr float MAX_TOTAL_MASS = 500.0f;

void draw_hint(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

auto get_state_name(ragdoll_state state) -> const char*
{
    switch(state)
    {
        case ragdoll_state::simulated:
            return "Simulated";
        case ragdoll_state::powered:
            return "Powered";
        case ragdoll_state::animated:
        default:
            return "Animated";
    }
}

auto describe_setup(const ragdoll_build_result& result) -> std::string
{
    if(!result.ok)
    {
        return result.error;
    }
    return std::to_string(result.bodies.size()) + " bodies, " + std::to_string(result.skin_fitted_bodies) +
           " fitted to the skin, " + std::to_string(result.disabled_collisions.size()) + " pairs that never collide.";
}

void draw_ragdoll_status(rtti::context& ctx, const ragdoll_component& data)
{
    const auto& bodies = data.get_bodies();
    float mass = 0.0f;
    for(const auto& body : bodies)
    {
        mass += body.mass;
    }
    if(bodies.empty())
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_INFORMATION_OUTLINE " No bodies yet: use Auto Setup on a humanoid.");
        return;
    }
    const auto& runtime = data.get_runtime();
    if(!ctx.get_cached<play_mode>().is_active())
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_HUMAN " %d bodies, %.1f kg. Starts %s.",
                           int(bodies.size()),
                           mass,
                           get_state_name(data.get_start_state()));
        return;
    }
    int simulated = 0;
    for(const auto& body : runtime.bodies)
    {
        simulated += body.simulated ? 1 : 0;
    }
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(runtime.is_built ? COL_ACTIVE : COL_IDLE),
                       ICON_MDI_HUMAN " %s: %d of %d bodies simulate, %.2f m/s, resting %.1f s",
                       get_state_name(data.get_state()),
                       simulated,
                       int(bodies.size()),
                       data.get_average_speed(),
                       data.get_rest_time());
}
} // namespace physics_inspector_detail

auto auto_setup_ragdoll(rtti::context& ctx, entt::handle owner, const ragdoll_build_options& options) -> ragdoll_build_result
{
    ragdoll_build_result result;
    auto* ragdoll = owner ? owner.try_get<ragdoll_component>() : nullptr;
    const auto* model = owner ? owner.try_get<model_component>() : nullptr;
    if(ragdoll == nullptr || model == nullptr)
    {
        result.error = "The entity needs a ragdoll and a skinned model component.";
        return result;
    }
    result = build_humanoid_ragdoll(*model, owner, options);
    if(!result.ok)
    {
        return result;
    }
    const auto before_bodies = ragdoll->get_bodies();
    const auto before_pairs = ragdoll->get_disabled_collisions();
    const auto owner_uh = entt::make_uhandle(owner);
    auto overrides = std::make_shared<prefab_override_record>();
    auto& em = ctx.get_cached<editing_manager>();
    em.do_action(
        "Ragdoll Auto Setup",
        [owner_uh, overrides, bodies = result.bodies, pairs = result.disabled_collisions]()
        {
            if(auto entity = owner_uh.resolve(); entity && entity.all_of<ragdoll_component>())
            {
                const auto type = entt::resolve<ragdoll_component>();
                const auto state_before = prefab_override_context::save_component_state(entity, type);
                auto& component = entity.get<ragdoll_component>();
                component.set_bodies(bodies);
                component.set_disabled_collisions(pairs);
                *overrides = prefab_override_context::mark_changed_properties(entity, type, state_before);
            }
        },
        [owner_uh, overrides, bodies = before_bodies, pairs = before_pairs]()
        {
            if(auto entity = owner_uh.resolve(); entity && entity.all_of<ragdoll_component>())
            {
                auto& component = entity.get<ragdoll_component>();
                component.set_bodies(bodies);
                component.set_disabled_collisions(pairs);
                prefab_override_context::restore_overrides(*overrides);
            }
        });
    return result;
}

auto inspector_ragdoll_component::inspect(rtti::context& ctx,
                                          entt::meta_any& var,
                                          const meta_any_proxy& var_proxy,
                                          const var_info& info,
                                          const entt::meta_custom& custom) -> inspect_result
{
    namespace detail = physics_inspector_detail;
    auto& data = var.cast<ragdoll_component&>();
    inspect_result result;
    ImGui::PushID("ragdoll_tools");
    detail::draw_ragdoll_status(ctx, data);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    ImGui::DragFloat("##total_mass", &total_mass_, 0.5f, detail::MIN_TOTAL_MASS, detail::MAX_TOTAL_MASS, "%.1f kg");
    ImGui::SetItemTooltipEx("The mass Auto Setup shares out over the bodies, by the mass of each part of a human body.");
    ImGui::SameLine();
    const bool is_playing = ctx.get_cached<play_mode>().is_active();
    ImGui::BeginDisabled(is_playing);
    if(ImGui::Button(ICON_MDI_AUTO_FIX " Auto Setup"))
    {
        ragdoll_build_options options;
        options.total_mass = total_mass_;
        auto_setup_report_ = detail::describe_setup(auto_setup_ragdoll(ctx, data.get_owner(), options));
        auto_setup_owner_ = data.get_owner();
        result.changed = true;
        result.edit_finished = true;
        result.change_recorded = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Makes the bodies of a humanoid from its skeleton: capsules fitted to the skin, human joint "
                            "ranges, masses by body segment, and the pairs that overlap at rest left apart. Replaces "
                            "the bodies there are.");
    if(!auto_setup_report_.empty() && auto_setup_owner_ == data.get_owner())
    {
        detail::draw_hint(auto_setup_report_.c_str());
    }
    ImGui::PopID();
    ImGui::Spacing();
    const auto properties = inspect_var_properties(ctx, var, var_proxy, info, custom);
    result.changed |= properties.changed;
    result.edit_finished |= properties.edit_finished;
    result.change_recorded |= properties.change_recorded;
    return result;
}

auto inspector_joint_component::inspect(rtti::context& ctx,
                                        entt::meta_any& var,
                                        const meta_any_proxy& var_proxy,
                                        const var_info& info,
                                        const entt::meta_custom& custom) -> inspect_result
{
    namespace detail = physics_inspector_detail;
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<joint_component&>();
    ImGui::PushID("joint_tools");
    ImGui::Spacing();
    const auto& state = data.get_state();
    const auto body_b = data.get_resolved_body_b();
    if(!body_b || !body_b.all_of<physics_component>())
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(detail::COL_IDLE),
                           ICON_MDI_INFORMATION_OUTLINE " Body B needs a physics component: set Body B, or add one here.");
    }
    else if(!ctx.get_cached<play_mode>().is_active())
    {
        detail::draw_hint("The joint is made when play starts, from where this entity and both bodies stand.");
    }
    else if(state.is_broken)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(detail::COL_BROKEN), ICON_MDI_LINK_OFF " Broken");
    }
    else
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(state.is_active ? detail::COL_ACTIVE : detail::COL_IDLE),
                           ICON_MDI_LINK " %s  angle %.1f deg  position %.3f m  speed %.1f  force %.0f N",
                           state.is_active ? "Active" : "Waiting for its bodies",
                           state.angle,
                           state.position,
                           state.speed,
                           math::length(state.force));
    }
    ImGui::PopID();
    return result;
}

} // namespace unravel
