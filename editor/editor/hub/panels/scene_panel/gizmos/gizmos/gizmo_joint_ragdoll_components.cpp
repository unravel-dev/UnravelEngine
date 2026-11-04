#include "gizmo_joint_ragdoll_components.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/physics/gizmos/gizmos.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <algorithm>
#include <cmath>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace joint_gizmo_detail
{
// Colours are ABGR.
constexpr uint32_t AXIS_COLOR = 0xff2080ff;
constexpr uint32_t SECONDARY_AXIS_COLOR = 0xff40c040;
constexpr uint32_t RANGE_COLOR = 0xffffd040;
constexpr uint32_t BROKEN_COLOR = 0xff3030ff;
constexpr uint32_t BODY_LINK_COLOR = 0xff909090;
constexpr uint32_t ANIMATED_COLOR = 0xc040d040;
constexpr uint32_t SIMULATED_COLOR = 0xc00090ff;
constexpr uint32_t POWERED_COLOR = 0xc0ff40c0;
constexpr uint32_t BLENDING_COLOR = 0xc0e0e0e0;
/// Sizes of the joint frame drawing, in meters.
constexpr float AXIS_LENGTH = 0.35f;
constexpr float RANGE_RADIUS = 0.25f;
constexpr float RAGDOLL_AXIS_LENGTH = 0.08f;
/// Segments of the drawn arcs and circles.
constexpr int ARC_SEGMENTS = 24;
/// Cones wider than this do not read as cones and are drawn as their rim only.
constexpr float MAX_CONE_LINES_ANGLE = 80.0f;

auto to_point(const math::vec3& v) -> bx::Vec3
{
    return {v.x, v.y, v.z};
}

void draw_line(gfx::dd_raii& dd, const math::vec3& from, const math::vec3& to, uint32_t color)
{
    dd.encoder.setColor(color);
    dd.encoder.moveTo(from.x, from.y, from.z);
    dd.encoder.lineTo(to.x, to.y, to.z);
}

/// An arc about @p axis from @p from (a direction across the axis) turned by @p lower to @p upper degrees.
void draw_arc(gfx::dd_raii& dd,
              const math::vec3& center,
              const math::vec3& axis,
              const math::vec3& from,
              float lower,
              float upper,
              float radius,
              uint32_t color)
{
    dd.encoder.setColor(color);
    for(int i = 0; i <= ARC_SEGMENTS; ++i)
    {
        const float angle = math::radians(math::mix(lower, upper, float(i) / float(ARC_SEGMENTS)));
        const math::vec3 point = center + math::angleAxis(angle, axis) * from * radius;
        if(i == 0)
        {
            dd.encoder.moveTo(point.x, point.y, point.z);
        }
        else
        {
            dd.encoder.lineTo(point.x, point.y, point.z);
        }
    }
    const math::vec3 first = center + math::angleAxis(math::radians(lower), axis) * from * radius;
    const math::vec3 last = center + math::angleAxis(math::radians(upper), axis) * from * radius;
    draw_line(dd, center, first, color);
    draw_line(dd, center, last, color);
}

void draw_cone(gfx::dd_raii& dd,
               const math::vec3& apex,
               const math::vec3& axis,
               const math::vec3& side,
               float half_angle,
               float length,
               uint32_t color)
{
    const float clamped = math::clamp(half_angle, 0.0f, 179.0f);
    const math::vec3 tip = axis * std::cos(math::radians(clamped)) * length;
    const float rim = std::sin(math::radians(clamped)) * length;
    dd.encoder.setColor(color);
    dd.encoder.drawCircle(to_point(axis), to_point(apex + tip), rim);
    if(clamped <= MAX_CONE_LINES_ANGLE)
    {
        const math::vec3 other = math::cross(axis, side);
        draw_line(dd, apex, apex + tip + side * rim, color);
        draw_line(dd, apex, apex + tip - side * rim, color);
        draw_line(dd, apex, apex + tip + other * rim, color);
        draw_line(dd, apex, apex + tip - other * rim, color);
    }
}

auto get_world_position(entt::handle entity) -> hpp::optional<math::vec3>
{
    if(!entity || !entity.valid())
    {
        return {};
    }
    if(const auto* transform = entity.try_get<transform_component>())
    {
        return transform->get_position_global();
    }
    return {};
}

/// The direction across @p axis towards @p target, else @p fallback.
auto get_across(const math::vec3& axis, const math::vec3& center, const hpp::optional<math::vec3>& target, const math::vec3& fallback)
    -> math::vec3
{
    if(target)
    {
        math::vec3 across = *target - center;
        across -= axis * math::dot(across, axis);
        if(math::length(across) > 1e-3f)
        {
            return math::normalize(across);
        }
    }
    return fallback;
}

void draw_joint_range(gfx::dd_raii& dd,
                      const joint_component& joint,
                      const math::vec3& center,
                      const math::vec3& x_axis,
                      const math::vec3& y_axis,
                      const hpp::optional<math::vec3>& body_b)
{
    const auto& limits = joint.get_limits();
    switch(joint.get_type())
    {
        case joint_type::hinge:
        {
            const math::vec3 from = get_across(x_axis, center, body_b, y_axis);
            const float lower = limits.enabled ? std::min(limits.lower, limits.upper) : -180.0f;
            const float upper = limits.enabled ? std::max(limits.lower, limits.upper) : 180.0f;
            draw_arc(dd, center, x_axis, from, lower, upper, RANGE_RADIUS, RANGE_COLOR);
            break;
        }
        case joint_type::ball:
            if(limits.enabled)
            {
                draw_cone(dd, center, x_axis, y_axis, limits.cone_angle, AXIS_LENGTH, RANGE_COLOR);
                const math::vec3 z_axis = math::cross(x_axis, y_axis);
                draw_arc(dd, center + x_axis * AXIS_LENGTH, x_axis, z_axis, limits.lower, limits.upper,
                         RANGE_RADIUS * 0.4f, RANGE_COLOR);
            }
            break;
        case joint_type::slider:
        {
            const float lower = limits.enabled ? std::min(limits.lower, limits.upper) : -AXIS_LENGTH * 3.0f;
            const float upper = limits.enabled ? std::max(limits.lower, limits.upper) : AXIS_LENGTH * 3.0f;
            const math::vec3 tick = y_axis * 0.05f;
            draw_line(dd, center + x_axis * lower, center + x_axis * upper, RANGE_COLOR);
            draw_line(dd, center + x_axis * lower - tick, center + x_axis * lower + tick, RANGE_COLOR);
            draw_line(dd, center + x_axis * upper - tick, center + x_axis * upper + tick, RANGE_COLOR);
            break;
        }
        case joint_type::wheel:
        {
            dd.encoder.setColor(RANGE_COLOR);
            dd.encoder.drawCircle(to_point(x_axis), to_point(center), RANGE_RADIUS);
            if(limits.enabled)
            {
                draw_line(dd, center + y_axis * limits.lower, center + y_axis * limits.upper, RANGE_COLOR);
            }
            break;
        }
        case joint_type::distance:
            break;
        case joint_type::fixed:
        {
            dd.encoder.setColor(RANGE_COLOR);
            dd.encoder.draw(bx::Aabb{to_point(center - math::vec3(0.04f)), to_point(center + math::vec3(0.04f))});
            break;
        }
    }
}

auto get_ragdoll_color(const ragdoll_component& ragdoll, int index) -> uint32_t
{
    const auto& runtime = ragdoll.get_runtime();
    if(!runtime.is_built || index < 0 || size_t(index) >= runtime.bodies.size())
    {
        return ANIMATED_COLOR;
    }
    const auto& body = runtime.bodies[size_t(index)];
    if(body.simulated)
    {
        return ragdoll.get_state() == ragdoll_state::simulated ? SIMULATED_COLOR : POWERED_COLOR;
    }
    return body.weight > 0.0f ? BLENDING_COLOR : ANIMATED_COLOR;
}

auto find_bone(const model_component& model, const std::string& name) -> entt::handle
{
    int index = model.get_armature_index_by_name_cached(name);
    index = index >= 0 ? index : model.get_armature_index_by_base_name_cached(name);
    return index >= 0 ? model.get_armature_by_index(size_t(index)) : entt::handle{};
}
} // namespace joint_gizmo_detail

void gizmo_joint_component::draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d)
{
    namespace detail = joint_gizmo_detail;
    entt::as_derived(var);
    auto& joint = var.cast<joint_component&>();
    const math::transform frame = joint.get_frame_global();
    const math::vec3 center = frame.get_position();
    const math::quat rotation = frame.get_rotation();
    const math::vec3 x_axis = math::normalize(rotation * math::vec3{1.0f, 0.0f, 0.0f});
    const math::vec3 y_axis = math::normalize(rotation * math::vec3{0.0f, 1.0f, 0.0f});
    const bool broken = joint.get_state().is_broken;

    DebugDrawEncoderScopePush scope(dd.encoder);
    dd.encoder.setWireframe(true);
    const auto body_a = detail::get_world_position(joint.get_body_a());
    const auto body_b = detail::get_world_position(joint.get_resolved_body_b());
    if(body_a)
    {
        detail::draw_line(dd, center, *body_a, detail::BODY_LINK_COLOR);
    }
    if(body_b && joint.get_type() != joint_type::distance)
    {
        detail::draw_line(dd, center, *body_b, detail::BODY_LINK_COLOR);
    }
    if(joint.get_type() == joint_type::distance && body_b)
    {
        const auto& owner_b = joint.get_resolved_body_b();
        const math::quat rotation_b = owner_b.get<transform_component>().get_rotation_global();
        detail::draw_line(dd, center, *body_b + rotation_b * joint.get_connected_anchor(),
                          broken ? detail::BROKEN_COLOR : detail::RANGE_COLOR);
    }
    dd.encoder.setColor(broken ? detail::BROKEN_COLOR : detail::AXIS_COLOR);
    dd.encoder.drawCone(detail::to_point(center + x_axis * (detail::AXIS_LENGTH * 0.8f)),
                        detail::to_point(center + x_axis * detail::AXIS_LENGTH),
                        detail::AXIS_LENGTH * 0.06f);
    detail::draw_line(dd, center - x_axis * detail::AXIS_LENGTH * 0.3f, center + x_axis * detail::AXIS_LENGTH,
                      broken ? detail::BROKEN_COLOR : detail::AXIS_COLOR);
    detail::draw_line(dd, center, center + y_axis * detail::AXIS_LENGTH * 0.4f, detail::SECONDARY_AXIS_COLOR);
    if(!broken)
    {
        detail::draw_joint_range(dd, joint, center, x_axis, y_axis, body_b);
    }
}

void gizmo_joint_component::draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd)
{
}

void gizmo_ragdoll_component::draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d)
{
    namespace detail = joint_gizmo_detail;
    entt::as_derived(var);
    auto& ragdoll = var.cast<ragdoll_component&>();
    auto owner = ragdoll.get_owner();
    const auto* model = owner ? owner.try_get<model_component>() : nullptr;
    if(model == nullptr)
    {
        return;
    }
    DebugDrawEncoderScopePush scope(dd.encoder);
    dd.encoder.setWireframe(true);
    const auto& bodies = ragdoll.get_bodies();
    for(size_t i = 0; i < bodies.size(); ++i)
    {
        const auto bone = detail::find_bone(*model, bodies[i].bone);
        if(!bone)
        {
            continue;
        }
        const auto& world = bone.get<transform_component>().get_transform_global();
        dd.encoder.pushTransform(static_cast<const float*>(world));
        dd.encoder.setColor(detail::get_ragdoll_color(ragdoll, int(i)));
        ::unravel::draw(dd.encoder, bodies[i].shape);
        dd.encoder.popTransform();

        // The joint axis at the bone's origin: the twist or hinge axis.
        const math::quat joint_rotation = math::normalize(world.get_rotation() *
                                                          math::quat(math::radians(bodies[i].joint_rotation)));
        const math::vec3 origin = world.get_position();
        const math::vec3 axis = joint_rotation * math::vec3{1.0f, 0.0f, 0.0f};
        detail::draw_line(dd, origin, origin + axis * detail::RAGDOLL_AXIS_LENGTH, detail::AXIS_COLOR);
    }
}

void gizmo_ragdoll_component::draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd)
{
}

} // namespace unravel
