#include "gizmo_rig_components.h"

#include <engine/animation/ecs/systems/rig_system.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/play_mode.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace unravel
{
namespace
{
// Colours are ABGR.
constexpr uint32_t REACHED_COLOR = 0xff40e040;
constexpr uint32_t UNREACHED_COLOR = 0xff0090ff;
constexpr uint32_t IDLE_COLOR = 0xff909090;
constexpr uint32_t TARGET_COLOR = 0xff00ffff;
constexpr uint32_t POLE_COLOR = 0xffff9040;
constexpr uint32_t PROBE_COLOR = 0xff707070;
constexpr uint32_t SPRING_COLOR = 0xffffc040;
constexpr uint32_t COLLIDER_COLOR = 0xff4060ff;
/// Joint spheres as a fraction of the average bone length, within these bounds (meters).
constexpr float JOINT_RADIUS_FRACTION = 0.08f;
constexpr float MIN_JOINT_RADIUS = 0.004f;
constexpr float MAX_JOINT_RADIUS = 0.03f;
/// Half size of the target, goal and pole crosses, in meters.
constexpr float CROSS_HALF_SIZE = 0.05f;
/// Length of the look-at turn limit cone, in meters.
constexpr float LOOK_CONE_LENGTH = 0.25f;
/// Wider limits no longer read as a cone and are not drawn, in degrees.
constexpr float MAX_DRAWN_CONE_ANGLE = 80.0f;
/// Weights below this count as off.
constexpr float MIN_WEIGHT = 1e-3f;
/// A facing axis shorter than this has no direction to draw the turn limit around.
constexpr float MIN_AXIS_LENGTH = 1e-3f;
/// Pelvis drops smaller than this are not applied by the rig system, so not drawn.
constexpr float MIN_PELVIS_OFFSET = 1e-4f;
/// Coarse tessellation: the shapes only have to read as spheres and cones.
constexpr uint8_t GIZMO_LOD = 2;

auto to_gizmo_point(const math::vec3& data) -> bx::Vec3
{
    return {data.x, data.y, data.z};
}

auto get_transform(entt::handle entity) -> transform_component*
{
    return entity && entity.valid() ? entity.try_get<transform_component>() : nullptr;
}

auto get_joint_radius(const std::vector<math::vec3>& joints) -> float
{
    if(joints.size() < 2)
    {
        return MIN_JOINT_RADIUS;
    }
    float total = 0.0f;
    for(size_t i = 0; i + 1 < joints.size(); ++i)
    {
        total += math::length(joints[i + 1] - joints[i]);
    }
    const float average = total / float(joints.size() - 1);
    return math::clamp(average * JOINT_RADIUS_FRACTION, MIN_JOINT_RADIUS, MAX_JOINT_RADIUS);
}

void draw_line(gfx::dd_raii& dd, const math::vec3& from, const math::vec3& to, uint32_t color)
{
    dd.encoder.setColor(color);
    dd.encoder.moveTo(from.x, from.y, from.z);
    dd.encoder.lineTo(to.x, to.y, to.z);
}

void draw_cross(gfx::dd_raii& dd, const math::vec3& position, uint32_t color)
{
    draw_line(dd, position - math::vec3(CROSS_HALF_SIZE, 0, 0), position + math::vec3(CROSS_HALF_SIZE, 0, 0), color);
    draw_line(dd, position - math::vec3(0, CROSS_HALF_SIZE, 0), position + math::vec3(0, CROSS_HALF_SIZE, 0), color);
    draw_line(dd, position - math::vec3(0, 0, CROSS_HALF_SIZE), position + math::vec3(0, 0, CROSS_HALF_SIZE), color);
}

void draw_sphere(gfx::dd_raii& dd, const math::vec3& center, float radius, uint32_t color)
{
    dd.encoder.setColor(color);
    dd.encoder.draw(bx::Sphere{to_gizmo_point(center), radius});
}

/// Bones as lines, joints as spheres of `radius` (or a size derived from the bone lengths).
void draw_chain(gfx::dd_raii& dd, const std::vector<math::vec3>& joints, uint32_t color, float radius = 0.0f)
{
    if(joints.empty())
    {
        return;
    }
    const float joint_radius = radius > 0.0f ? radius : get_joint_radius(joints);
    for(size_t i = 0; i + 1 < joints.size(); ++i)
    {
        draw_line(dd, joints[i], joints[i + 1], color);
    }
    for(const auto& joint : joints)
    {
        draw_sphere(dd, joint, joint_radius, color);
    }
}

auto get_status_color(const ik_debug_state& debug) -> uint32_t
{
    if(!debug.solved || debug.effective_weight < MIN_WEIGHT)
    {
        return IDLE_COLOR;
    }
    return debug.reached ? REACHED_COLOR : UNREACHED_COLOR;
}

void begin_gizmo(gfx::dd_raii& dd)
{
    dd.encoder.setWireframe(true);
    dd.encoder.setLod(GIZMO_LOD);
}

struct reach_gizmo
{
    const ik_debug_state& debug;
    entt::handle tip;
    /// Entities in the chain, the tip included.
    int joints{};
    entt::handle target;
    entt::handle pole;
};

/// Shared by the two-bone and chain IK: the solved chain (or the animated one while idle), the
/// target, a line to it while out of reach, and the pole the chain bends toward.
void draw_reach(gfx::dd_raii& dd, const reach_gizmo& gizmo)
{
    const auto joints = gizmo.debug.joints.empty() ? collect_rig_chain_positions(gizmo.tip, gizmo.joints) : gizmo.debug.joints;
    draw_chain(dd, joints, get_status_color(gizmo.debug));
    if(auto* target = get_transform(gizmo.target))
    {
        draw_cross(dd, target->get_position_global(), TARGET_COLOR);
        if(gizmo.debug.solved && !gizmo.debug.reached && !joints.empty())
        {
            draw_line(dd, joints.back(), gizmo.debug.goal, UNREACHED_COLOR);
        }
    }
    if(auto* pole = get_transform(gizmo.pole); pole != nullptr && joints.size() >= RIG_MIN_POLE_JOINTS)
    {
        const math::vec3 pole_position = pole->get_position_global();
        draw_line(dd, joints[joints.size() / 2], pole_position, POLE_COLOR);
        draw_cross(dd, pole_position, POLE_COLOR);
    }
}
} // namespace

void gizmo_two_bone_ik_component::draw(rtti::context& ctx,
                                       entt::meta_any& var,
                                       const camera& cam,
                                       gfx::dd_raii& dd,
                                       dd_2d_raii& dd_2d)
{
    entt::as_derived(var);
    auto& data = var.cast<two_bone_ik_component&>();
    DebugDrawEncoderScopePush scope(dd.encoder);
    begin_gizmo(dd);
    draw_reach(dd, {data.debug, data.get_owner(), RIG_TWO_BONE_JOINTS, data.target, data.pole});
}

void gizmo_two_bone_ik_component::draw_billboard(rtti::context& ctx,
                                                 entt::meta_any& var,
                                                 const camera& cam,
                                                 gfx::dd_raii& dd)
{
}

void gizmo_chain_ik_component::draw(rtti::context& ctx,
                                    entt::meta_any& var,
                                    const camera& cam,
                                    gfx::dd_raii& dd,
                                    dd_2d_raii& dd_2d)
{
    entt::as_derived(var);
    auto& data = var.cast<chain_ik_component&>();
    DebugDrawEncoderScopePush scope(dd.encoder);
    begin_gizmo(dd);
    draw_reach(dd, {data.debug, data.get_owner(), data.chain_length + 1, data.target, data.pole});
}

void gizmo_chain_ik_component::draw_billboard(rtti::context& ctx,
                                              entt::meta_any& var,
                                              const camera& cam,
                                              gfx::dd_raii& dd)
{
}

void gizmo_look_at_ik_component::draw(rtti::context& ctx,
                                      entt::meta_any& var,
                                      const camera& cam,
                                      gfx::dd_raii& dd,
                                      dd_2d_raii& dd_2d)
{
    entt::as_derived(var);
    auto& data = var.cast<look_at_ik_component&>();
    auto tip = data.get_owner();
    auto* tip_transform = get_transform(tip);
    if(tip_transform == nullptr)
    {
        return;
    }
    DebugDrawEncoderScopePush scope(dd.encoder);
    begin_gizmo(dd);
    const auto joints = data.debug.joints.empty() ? collect_rig_chain_positions(tip, data.chain_length) : data.debug.joints;
    draw_chain(dd, joints, data.debug.solved ? REACHED_COLOR : IDLE_COLOR);
    const math::vec3 eye = tip_transform->get_position_global();
    if(data.debug.effective_weight >= MIN_WEIGHT)
    {
        draw_line(dd, eye, data.debug.goal, TARGET_COLOR);
        draw_cross(dd, data.debug.goal, TARGET_COLOR);
    }
    // The cone each bone may turn within, around the head's animated facing. The scene being played
    // holds the solved pose through the render, so there the head shows that facing only while the
    // look does not solve.
    const bool shows_animated_facing = !ctx.get_cached<play_mode>().is_active() || !data.debug.solved;
    const math::vec3 local_axis = !data.resolved_axes.empty() ? data.resolved_axes.back() : data.forward_axis;
    if(!shows_animated_facing || math::length(local_axis) < MIN_AXIS_LENGTH || data.max_bone_angle <= 0.0f ||
       data.max_bone_angle > MAX_DRAWN_CONE_ANGLE)
    {
        return;
    }
    const math::vec3 facing = math::normalize(tip_transform->get_rotation_global() * math::normalize(local_axis));
    const float radius = LOOK_CONE_LENGTH * std::tan(math::radians(data.max_bone_angle));
    dd.encoder.setColor(IDLE_COLOR);
    dd.encoder.drawCone(to_gizmo_point(eye + facing * LOOK_CONE_LENGTH), to_gizmo_point(eye), radius);
}

void gizmo_look_at_ik_component::draw_billboard(rtti::context& ctx,
                                                entt::meta_any& var,
                                                const camera& cam,
                                                gfx::dd_raii& dd)
{
}

void gizmo_foot_ik_component::draw(rtti::context& ctx,
                                   entt::meta_any& var,
                                   const camera& cam,
                                   gfx::dd_raii& dd,
                                   dd_2d_raii& dd_2d)
{
    entt::as_derived(var);
    auto& data = var.cast<foot_ik_component&>();
    DebugDrawEncoderScopePush scope(dd.encoder);
    begin_gizmo(dd);
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    // The foot IK runs in play mode, where the scene being played holds the solved pose through the
    // render: the pelvis and every foot below it already carry the pelvis drop.
    const auto draw_foot = [&](entt::handle foot, const ik_debug_state& debug)
    {
        auto* transform = get_transform(foot);
        if(transform == nullptr)
        {
            return;
        }
        // Probes start above the animated foot: a leg the IK left alone moved only with the pelvis,
        // a solved one is drawn from its planted foot, on the same vertical.
        const math::vec3 solved_position = transform->get_position_global();
        const math::vec3 position = debug.solved ? solved_position : solved_position - up * data.pelvis_offset;
        draw_line(dd, position + up * data.probe_up, position - up * data.probe_down, PROBE_COLOR);
        const auto joints = debug.joints.empty() ? collect_rig_chain_positions(foot, RIG_TWO_BONE_JOINTS) : debug.joints;
        draw_chain(dd, joints, get_status_color(debug));
        if(debug.solved)
        {
            draw_cross(dd, debug.goal, TARGET_COLOR);
        }
    };
    draw_foot(data.left_foot, data.left_debug);
    draw_foot(data.right_foot, data.right_debug);
    if(auto* pelvis = get_transform(data.pelvis); pelvis != nullptr && std::fabs(data.pelvis_offset) > MIN_PELVIS_OFFSET)
    {
        // From the animated pelvis down to where the drop put it.
        const math::vec3 dropped = pelvis->get_position_global();
        draw_line(dd, dropped - up * data.pelvis_offset, dropped, POLE_COLOR);
        draw_cross(dd, dropped, POLE_COLOR);
    }
}

void gizmo_foot_ik_component::draw_billboard(rtti::context& ctx,
                                             entt::meta_any& var,
                                             const camera& cam,
                                             gfx::dd_raii& dd)
{
}

void gizmo_spring_bone_component::draw(rtti::context& ctx,
                                       entt::meta_any& var,
                                       const camera& cam,
                                       gfx::dd_raii& dd,
                                       dd_2d_raii& dd_2d)
{
    entt::as_derived(var);
    auto& data = var.cast<spring_bone_component&>();
    DebugDrawEncoderScopePush scope(dd.encoder);
    begin_gizmo(dd);
    const auto joints = data.debug.joints.empty() ? collect_rig_spring_chain_positions(data.get_owner(), data.chain_length) : data.debug.joints;
    draw_chain(dd, joints, data.debug.solved ? SPRING_COLOR : IDLE_COLOR, data.radius);
    for(const auto& collider : data.colliders)
    {
        if(auto* transform = get_transform(collider.entity))
        {
            const math::vec3 center = transform->get_position_global() + transform->get_rotation_global() * collider.offset;
            draw_sphere(dd, center, collider.radius, COLLIDER_COLOR);
        }
    }
}

void gizmo_spring_bone_component::draw_billboard(rtti::context& ctx,
                                                 entt::meta_any& var,
                                                 const camera& cam,
                                                 gfx::dd_raii& dd)
{
}

} // namespace unravel
