#include "inspector_physics_components.h"
#include "inspectors.h"
#include <editor/editing/editor_actions.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/physics_types.h>
#include <engine/play_mode.h>
#include <engine/settings/boot_config.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace unravel
{

void draw_inspector_hint(hpp::string_view text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%.*s", int(text.size()), text.data());
    ImGui::PopStyleColor();
}

// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace physics_inspector_detail
{
constexpr float MIN_TOTAL_MASS = 1.0f;
constexpr float MAX_TOTAL_MASS = 500.0f;
/// Kilograms the total mass field changes by per pixel dragged.
constexpr float TOTAL_MASS_DRAG_SPEED = 0.5f;
/// Width of the total mass field, in font heights.
constexpr float TOTAL_MASS_FIELD_WIDTH = 6.0f;
constexpr const char* TOTAL_MASS_TOOLTIP =
    "The mass Auto Setup shares out over the bodies, by the mass of each part of a human body.";
constexpr const char* AUTO_SETUP_TOOLTIP =
    "Makes the bodies of a humanoid from its skeleton: capsules fitted to the skin, boxes on the soles of the feet, "
    "human joint ranges, masses by body segment, and the pairs that overlap at rest left apart. Replaces the bodies "
    "there are.";
constexpr const char* AUTO_SETUP_PLAY_TOOLTIP = "Auto Setup works in edit mode only: stop play to use it.";
constexpr const char* BULLET_JOINT_HINT = "Bullet, the physics backend this editor runs, does not simulate joints: "
                                          "this joint does nothing in play. Box3D simulates them.";
constexpr const char* BULLET_RAGDOLL_HINT = "Bullet, the physics backend this editor runs, does not simulate "
                                            "ragdolls: these bodies do nothing in play. Box3D simulates them.";

/// One status line in @p color, wrapping at the inspector's edge.
void draw_status(ImU32 color, const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

/// Whether this editor runs Bullet, which simulates neither joints nor ragdolls. The backend is picked when the
/// editor starts, so a changed project setting counts only after a restart.
auto is_running_bullet(rtti::context& ctx) -> bool
{
    const physics_backend_type selected =
        ctx.has<boot_config>() ? ctx.get<boot_config>().physics : physics_backend_type::auto_detect;
    return resolve_physics_backend(selected) == physics_backend_type::bullet;
}

auto get_state_name(ragdoll_state state) -> const char*
{
    switch(state)
    {
        case ragdoll_state::simulated:
            return "simulated";
        case ragdoll_state::powered:
            return "powered";
        case ragdoll_state::animated:
        default:
            return "animated";
    }
}

auto get_total_mass(const std::vector<ragdoll_body>& bodies) -> float
{
    return std::accumulate(bodies.begin(),
                           bodies.end(),
                           0.0f,
                           [](float sum, const ragdoll_body& body)
                           {
                               return sum + body.mass;
                           });
}

auto describe_setup(const ragdoll_auto_setup_result& setup) -> std::string
{
    const auto& build = setup.build;
    if(!build.ok)
    {
        return build.error;
    }
    const std::string made =
        fmt::format("Auto Setup made {} bodies, {} of them fitted to the skin, and {} pairs of bodies that never "
                    "collide.",
                    build.bodies.size(),
                    build.skin_fitted_bodies,
                    build.disabled_collisions.size());
    return setup.is_changed ? made : made + " The ragdoll had these already, so nothing changed.";
}

void draw_ragdoll_edit_status(const ragdoll_component& data)
{
    const auto& bodies = data.get_bodies();
    draw_status(INSPECTOR_STATUS_IDLE_COLOR,
                fmt::format(ICON_MDI_HUMAN " The ragdoll has {} bodies weighing {:.1f} kg in all, and it starts {}.",
                            bodies.size(),
                            get_total_mass(bodies),
                            get_state_name(data.get_start_state())));
}

void draw_ragdoll_play_status(const ragdoll_component& data)
{
    const auto& runtime = data.get_runtime();
    const char* state = get_state_name(data.get_state());
    if(!runtime.is_built)
    {
        draw_status(INSPECTOR_STATUS_IDLE_COLOR,
                    fmt::format(ICON_MDI_HUMAN " The ragdoll is {}, and its physics bodies are not built yet.", state));
        return;
    }
    const auto simulated = std::count_if(runtime.bodies.begin(),
                                         runtime.bodies.end(),
                                         [](const ragdoll_body_runtime& body)
                                         {
                                             return body.simulated;
                                         });
    draw_status(INSPECTOR_STATUS_ACTIVE_COLOR,
                fmt::format(ICON_MDI_HUMAN " The ragdoll is built and {}: {} of {} bodies simulate, at {:.2f} m/s on "
                                           "average, and it has been at rest for {:.1f} s.",
                            state,
                            simulated,
                            runtime.bodies.size(),
                            data.get_average_speed(),
                            data.get_rest_time()));
}

void draw_ragdoll_status(rtti::context& ctx, const ragdoll_component& data)
{
    if(data.get_bodies().empty())
    {
        draw_status(INSPECTOR_STATUS_IDLE_COLOR,
                    ICON_MDI_INFORMATION_OUTLINE " The ragdoll has no bodies yet: use Auto Setup on a humanoid.");
        return;
    }
    if(ctx.get_cached<play_mode>().is_active())
    {
        draw_ragdoll_play_status(data);
    }
    else
    {
        draw_ragdoll_edit_status(data);
    }
    if(is_running_bullet(ctx))
    {
        draw_inspector_hint(BULLET_RAGDOLL_HINT);
    }
}

/// The Auto Setup button, disabled while playing; true when it was clicked.
auto draw_auto_setup_button(rtti::context& ctx) -> bool
{
    const bool is_playing = ctx.get_cached<play_mode>().is_active();
    ImGui::BeginDisabled(is_playing);
    const bool is_clicked = ImGui::Button(ICON_MDI_AUTO_FIX " Auto Setup");
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("%s", is_playing ? AUTO_SETUP_PLAY_TOOLTIP : AUTO_SETUP_TOOLTIP);
    return is_clicked;
}

/// Whether a body was set and its entity has been deleted since.
auto is_deleted(entt::handle body) -> bool
{
    return body.entity() != entt::null && !body;
}

/// Why Body A keeps the joint from being made, or empty when it does not.
auto find_body_a_problem(entt::handle body_a) -> hpp::string_view
{
    if(is_deleted(body_a))
    {
        return "Body A was deleted: set another body, or clear Body A to hang the joint on the world.";
    }
    if(body_a && !body_a.all_of<physics_component>())
    {
        return "Body A needs a physics component: add one to it, or clear Body A to hang the joint on the world.";
    }
    return {};
}

/// Why the resolved Body B keeps the joint from being made, or empty when it does not.
auto find_body_b_problem(entt::handle body_b) -> hpp::string_view
{
    if(is_deleted(body_b))
    {
        return "Body B was deleted: set another body, or clear Body B to hold this entity.";
    }
    if(!body_b || !body_b.all_of<physics_component>())
    {
        return "Body B needs a physics component: set Body B, or add one to this entity.";
    }
    return {};
}

/// Why the joint cannot be made when play starts, or empty when it can.
auto find_joint_problem(joint_component& joint) -> hpp::string_view
{
    const auto body_a = joint.get_body_a();
    const auto body_b = joint.get_resolved_body_b();
    if(const auto problem = find_body_a_problem(body_a); !problem.empty())
    {
        return problem;
    }
    if(const auto problem = find_body_b_problem(body_b); !problem.empty())
    {
        return problem;
    }
    if(body_a == body_b)
    {
        return "Body A and Body B are the same body, and a joint connects two: set another Body A, or clear it to "
               "hang the joint on the world.";
    }
    return {};
}

/// What a running joint of @p type reports, in its units; empty for a fixed joint.
auto describe_joint_motion(joint_type type, const joint_state& state) -> std::string
{
    switch(type)
    {
        case joint_type::hinge:
            return fmt::format("at {:.1f} degrees, turning at {:.1f} degrees per second", state.angle, state.speed);
        case joint_type::slider:
            return fmt::format("at {:.3f} m, moving at {:.2f} m/s", state.position, state.speed);
        case joint_type::ball:
            return fmt::format("twisted by {:.1f} degrees", state.angle);
        case joint_type::wheel:
            return fmt::format("steered to {:.1f} degrees, spinning at {:.0f} degrees per second",
                               state.angle,
                               state.speed);
        case joint_type::distance:
            return fmt::format("{:.3f} m long", state.position);
        case joint_type::fixed:
        default:
            return {};
    }
}

/// The running joint's status: what its type reports, and the force and torque that hold its bodies.
auto describe_active_joint(const joint_component& joint) -> std::string
{
    const auto& state = joint.get_state();
    const std::string motion = describe_joint_motion(joint.get_type(), state);
    return fmt::format(ICON_MDI_LINK " The joint is active{} and holds its bodies with {:.0f} N and {:.1f} N m.",
                       motion.empty() ? std::string{} : ", " + motion + ",",
                       math::length(state.force),
                       math::length(state.torque));
}

void draw_joint_play_status(const joint_component& joint)
{
    const auto& state = joint.get_state();
    if(state.is_broken)
    {
        draw_status(INSPECTOR_STATUS_FAILED_COLOR,
                    ICON_MDI_LINK_OFF " The joint is broken, and it stays broken until a script repairs it or play "
                                      "ends.");
        return;
    }
    if(!state.is_active)
    {
        draw_status(INSPECTOR_STATUS_IDLE_COLOR,
                    ICON_MDI_LINK " The joint waits for both of its bodies to be in the simulation.");
        return;
    }
    draw_status(INSPECTOR_STATUS_ACTIVE_COLOR, describe_active_joint(joint));
}

void draw_joint_status(rtti::context& ctx, joint_component& joint)
{
    if(const auto problem = find_joint_problem(joint); !problem.empty())
    {
        draw_status(INSPECTOR_STATUS_IDLE_COLOR, ICON_MDI_INFORMATION_OUTLINE " " + std::string(problem));
        return;
    }
    if(is_running_bullet(ctx))
    {
        draw_inspector_hint(BULLET_JOINT_HINT);
        return;
    }
    if(!ctx.get_cached<play_mode>().is_active())
    {
        draw_inspector_hint("The joint is made when play starts, from where this entity and its bodies stand.");
        return;
    }
    draw_joint_play_status(joint);
}
} // namespace physics_inspector_detail

auto inspector_ragdoll_component::inspect(rtti::context& ctx,
                                          entt::meta_any& var,
                                          const meta_any_proxy& var_proxy,
                                          const var_info& info,
                                          const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<ragdoll_component&>();
    ImGui::PushID("ragdoll_tools");
    physics_inspector_detail::draw_ragdoll_status(ctx, data);
    auto result = draw_auto_setup(ctx, data);
    ImGui::PopID();
    ImGui::Spacing();
    const auto properties = inspect_var_properties(ctx, var, var_proxy, info, custom);
    result.changed |= properties.changed;
    result.edit_finished |= properties.edit_finished;
    result.change_recorded |= properties.change_recorded;
    return result;
}

auto inspector_ragdoll_component::draw_auto_setup(rtti::context& ctx, ragdoll_component& data) -> inspect_result
{
    namespace detail = physics_inspector_detail;
    inspect_result result;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * detail::TOTAL_MASS_FIELD_WIDTH);
    ImGui::DragFloat("##total_mass",
                     &total_mass_,
                     detail::TOTAL_MASS_DRAG_SPEED,
                     detail::MIN_TOTAL_MASS,
                     detail::MAX_TOTAL_MASS,
                     "%.1f kg",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltipEx("%s", detail::TOTAL_MASS_TOOLTIP);
    ImGui::SameLine();
    if(detail::draw_auto_setup_button(ctx))
    {
        ragdoll_build_options options;
        options.total_mass = total_mass_;
        const auto setup = editor_actions::auto_setup_ragdoll(ctx, data.get_owner(), options);
        auto_setup_report_ = detail::describe_setup(setup);
        auto_setup_owner_ = data.get_owner();
        result.changed = setup.is_changed;
        result.edit_finished = setup.is_changed;
        result.change_recorded = setup.is_changed;
    }
    if(!auto_setup_report_.empty() && auto_setup_owner_ == data.get_owner())
    {
        draw_inspector_hint(auto_setup_report_);
    }
    return result;
}

auto inspector_joint_component::inspect(rtti::context& ctx,
                                        entt::meta_any& var,
                                        const meta_any_proxy& var_proxy,
                                        const var_info& info,
                                        const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    ImGui::PushID("joint_tools");
    ImGui::Spacing();
    physics_inspector_detail::draw_joint_status(ctx, var.cast<joint_component&>());
    ImGui::PopID();
    return result;
}

} // namespace unravel
