#include "ragdoll_component.h"

#include <engine/ecs/components/basic_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/physics/ecs/components/joint_component.h>
#include <engine/physics/ecs/systems/ragdoll_poses.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace ragdoll_component_detail
{
/// Default blend back to the animation when a caller passes a negative time.
auto resolve_blend_time(float requested, float fallback) -> float
{
    return requested < 0.0f ? fallback : requested;
}

/// Directions shorter than this are taken as no direction.
constexpr float min_direction_length = 1e-4f;
/// The lightest a body may be, in kilograms: Box3D needs mass to move a body.
constexpr float min_body_mass = 0.01f;

auto is_valid_state(ragdoll_state state) -> bool
{
    return state <= ragdoll_state::powered;
}

/// One rule for every caller (inspector, MCP, scripts, files): a joint type the build has, a cone Box3D takes and a
/// body with mass.
auto make_valid_body(ragdoll_body body) -> ragdoll_body
{
    if(body.joint > ragdoll_joint_type::fixed)
    {
        body.joint = ragdoll_joint_type::ball;
    }
    body.swing_limit = math::clamp(body.swing_limit, 0.0f, MAX_CONE_ANGLE);
    body.mass = std::isfinite(body.mass) ? math::max(body.mass, min_body_mass) : min_body_mass;
    body.friction_scale = math::max(body.friction_scale, 0.0f);
    body.drive_scale = math::max(body.drive_scale, 0.0f);
    return body;
}

auto make_valid_drive(ragdoll_drive drive) -> ragdoll_drive
{
    drive.local_frequency = math::max(drive.local_frequency, 0.0f);
    drive.local_damping_ratio = math::max(drive.local_damping_ratio, 0.0f);
    drive.world_frequency = math::max(drive.world_frequency, 0.0f);
    drive.world_damping_ratio = math::max(drive.world_damping_ratio, 0.0f);
    drive.world_max_force = math::max(drive.world_max_force, 0.0f);
    drive.world_max_torque = math::max(drive.world_max_torque, 0.0f);
    return drive;
}

auto make_rotation_from_to(const math::vec3& from, const math::vec3& to) -> math::quat
{
    const float from_length = math::length(from);
    const float to_length = math::length(to);
    if(from_length <= 0.0f || to_length <= 0.0f)
    {
        return math::identity<math::quat>();
    }
    return math::rotation(from / from_length, to / to_length);
}
} // namespace ragdoll_component_detail

void ragdoll_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    auto& component = entity.get<ragdoll_component>();
    component.set_owner(entity);
    component.dirty_.set();
    component.needs_rebuild_ = true;
    component.settings_dirty_ = true;
    component.reset_runtime_state();
}

void ragdoll_component::on_destroy_component(entt::registry& r, entt::entity e)
{
}

void ragdoll_component::mark_definition_changed()
{
    dirty_.set();
    needs_rebuild_ = true;
    fit_runtime_to_definition();
}

void ragdoll_component::fit_runtime_to_definition()
{
    const size_t kept = std::min(runtime_.bodies.size(), bodies_.size());
    runtime_.bodies.resize(bodies_.size());
    const bool simulate = state_ != ragdoll_state::animated;
    for(size_t i = kept; i < runtime_.bodies.size(); ++i)
    {
        runtime_.bodies[i].simulated = simulate;
        runtime_.bodies[i].weight = simulate ? 1.0f : 0.0f;
    }
}

void ragdoll_component::mark_settings_changed()
{
    dirty_.set();
    settings_dirty_ = true;
}

void ragdoll_component::set_bodies(const std::vector<ragdoll_body>& bodies)
{
    std::vector<ragdoll_body> valid;
    valid.reserve(bodies.size());
    std::transform(bodies.begin(), bodies.end(), std::back_inserter(valid), ragdoll_component_detail::make_valid_body);
    if(bodies_ == valid)
    {
        return;
    }
    bodies_ = std::move(valid);
    mark_definition_changed();
}

auto ragdoll_component::get_bodies() const noexcept -> const std::vector<ragdoll_body>&
{
    return bodies_;
}

void ragdoll_component::set_disabled_collisions(const std::vector<ragdoll_bone_pair>& pairs)
{
    if(disabled_collisions_ == pairs)
    {
        return;
    }
    disabled_collisions_ = pairs;
    mark_definition_changed();
}

auto ragdoll_component::get_disabled_collisions() const noexcept -> const std::vector<ragdoll_bone_pair>&
{
    return disabled_collisions_;
}

void ragdoll_component::set_start_state(ragdoll_state state)
{
    if(ragdoll_component_detail::is_valid_state(state))
    {
        start_state_ = state;
    }
}

auto ragdoll_component::get_start_state() const noexcept -> ragdoll_state
{
    return start_state_;
}

void ragdoll_component::set_joint_friction(float friction)
{
    friction = math::max(friction, 0.0f);
    if(joint_friction_ == friction)
    {
        return;
    }
    joint_friction_ = friction;
    mark_settings_changed();
}

auto ragdoll_component::get_joint_friction() const noexcept -> float
{
    return joint_friction_;
}

void ragdoll_component::set_linear_damping(float damping)
{
    damping = math::max(damping, 0.0f);
    if(linear_damping_ == damping)
    {
        return;
    }
    linear_damping_ = damping;
    mark_settings_changed();
}

auto ragdoll_component::get_linear_damping() const noexcept -> float
{
    return linear_damping_;
}

void ragdoll_component::set_angular_damping(float damping)
{
    damping = math::max(damping, 0.0f);
    if(angular_damping_ == damping)
    {
        return;
    }
    angular_damping_ = damping;
    mark_settings_changed();
}

auto ragdoll_component::get_angular_damping() const noexcept -> float
{
    return angular_damping_;
}

void ragdoll_component::set_self_collision(bool enabled)
{
    if(self_collision_ == enabled)
    {
        return;
    }
    self_collision_ = enabled;
    mark_definition_changed();
}

auto ragdoll_component::get_self_collision() const noexcept -> bool
{
    return self_collision_;
}

void ragdoll_component::set_is_continuous(bool continuous)
{
    if(is_continuous_ == continuous)
    {
        return;
    }
    is_continuous_ = continuous;
    mark_settings_changed();
}

auto ragdoll_component::is_continuous() const noexcept -> bool
{
    return is_continuous_;
}

void ragdoll_component::set_include_layers(layer_mask mask)
{
    if(include_layers_.mask == mask.mask)
    {
        return;
    }
    include_layers_ = mask;
    mark_settings_changed();
}

auto ragdoll_component::get_include_layers() const noexcept -> layer_mask
{
    return include_layers_;
}

void ragdoll_component::set_exclude_layers(layer_mask mask)
{
    if(exclude_layers_.mask == mask.mask)
    {
        return;
    }
    exclude_layers_ = mask;
    mark_settings_changed();
}

auto ragdoll_component::get_exclude_layers() const noexcept -> layer_mask
{
    return exclude_layers_;
}

auto ragdoll_component::get_collision_mask() const noexcept -> layer_mask
{
    return layer_mask{include_layers_.mask & ~exclude_layers_.mask};
}

void ragdoll_component::set_drive(const ragdoll_drive& drive)
{
    const ragdoll_drive valid = ragdoll_component_detail::make_valid_drive(drive);
    if(drive_ == valid)
    {
        return;
    }
    drive_ = valid;
    mark_settings_changed();
    runtime_.bodies_changed = true;
}

auto ragdoll_component::get_drive() const noexcept -> const ragdoll_drive&
{
    return drive_;
}

void ragdoll_component::set_blend_time(float seconds)
{
    blend_time_ = math::max(seconds, 0.0f);
}

auto ragdoll_component::get_blend_time() const noexcept -> float
{
    return blend_time_;
}

void ragdoll_component::set_body_simulated(int index, bool simulated, float blend_time)
{
    if(index < 0 || size_t(index) >= runtime_.bodies.size())
    {
        return;
    }
    auto& body = runtime_.bodies[size_t(index)];
    if(body.simulated == simulated)
    {
        return;
    }
    body.simulated = simulated;
    if(simulated)
    {
        // A body starts simulating where the animation put it, so there is nothing to blend from.
        body.weight = 1.0f;
    }
    else
    {
        body.snapshot_world = body.has_physics_pose ? body.world : body.anim_world;
        body.blend_time = blend_time;
        // A body that never had a pose (before the first physics frame) has nowhere to blend back from.
        if(blend_time <= 0.0f || !(body.has_physics_pose || body.has_anim_pose))
        {
            body.weight = 0.0f;
        }
    }
    runtime_.bodies_changed = true;
    dirty_.set();
}

void ragdoll_component::set_state(ragdoll_state state, float blend_time)
{
    const float blend = ragdoll_component_detail::resolve_blend_time(blend_time, blend_time_);
    if(!ragdoll_component_detail::is_valid_state(state) || (state_ == state && state != ragdoll_state::animated))
    {
        return;
    }
    state_ = state;
    const bool simulate = state != ragdoll_state::animated;
    for(size_t i = 0; i < runtime_.bodies.size(); ++i)
    {
        set_body_simulated(int(i), simulate, blend);
    }
    runtime_.rest_time = 0.0f;
    runtime_.bodies_changed = true;
    dirty_.set();
}

auto ragdoll_component::get_state() const noexcept -> ragdoll_state
{
    return state_;
}

void ragdoll_component::collect_bodies_below(int index, std::vector<int>& out) const
{
    out.push_back(index);
    for(size_t i = 0; i < runtime_.bodies.size(); ++i)
    {
        if(runtime_.bodies[i].parent == index)
        {
            collect_bodies_below(int(i), out);
        }
    }
}

auto ragdoll_component::set_bone_simulated(hpp::string_view bone, bool simulated, bool include_children, float blend_time)
    -> bool
{
    const int index = find_body(bone);
    if(index < 0)
    {
        return false;
    }
    std::vector<int> targets;
    if(include_children)
    {
        // The bodies below are found through the bones, which a call before the first physics frame has not met yet.
        prepare_ragdoll_runtime(*this);
        collect_bodies_below(index, targets);
    }
    else
    {
        targets.push_back(index);
    }
    const float blend = ragdoll_component_detail::resolve_blend_time(blend_time, blend_time_);
    for(const int target : targets)
    {
        set_body_simulated(target, simulated, blend);
    }
    return true;
}

auto ragdoll_component::is_body_simulated(int index) const -> bool
{
    return index >= 0 && size_t(index) < runtime_.bodies.size() && runtime_.bodies[size_t(index)].simulated;
}

auto ragdoll_component::find_body(hpp::string_view bone) const -> int
{
    const auto it = std::find_if(bodies_.begin(),
                                 bodies_.end(),
                                 [&](const ragdoll_body& body)
                                 {
                                     return body.bone == bone;
                                 });
    return it != bodies_.end() ? int(std::distance(bodies_.begin(), it)) : -1;
}

auto ragdoll_component::find_body(entt::handle bone) const -> int
{
    const entt::entity owner = get_owner().entity();
    for(entt::handle current = bone; current && current.entity() != owner;)
    {
        for(size_t i = 0; i < runtime_.bodies.size(); ++i)
        {
            if(runtime_.bodies[i].bone == current)
            {
                return int(i);
            }
        }
        if(const auto* tag = current.try_get<tag_component>())
        {
            const int index = find_body(tag->name);
            if(index >= 0)
            {
                return index;
            }
        }
        const auto* transform = current.try_get<transform_component>();
        current = transform ? transform->get_parent() : entt::handle{};
    }
    return -1;
}

auto ragdoll_component::can_take_impulses() const -> bool
{
    // An inactive ragdoll is not stepped, so what it queued would all land at once when it comes back.
    const auto owner = get_owner();
    return owner && owner.all_of<active_component>();
}

void ragdoll_component::add_impulse(int body, const math::vec3& impulse, const math::vec3& point)
{
    if(body < 0 || size_t(body) >= bodies_.size() || !can_take_impulses())
    {
        return;
    }
    runtime_.impulses.push_back({body, impulse, point, true, false});
    dirty_.set();
}

void ragdoll_component::add_impulse(int body, const math::vec3& impulse)
{
    if(body < 0 || size_t(body) >= bodies_.size() || !can_take_impulses())
    {
        return;
    }
    runtime_.impulses.push_back({body, impulse, math::vec3{0.0f}, false, false});
    dirty_.set();
}

void ragdoll_component::add_velocity(const math::vec3& velocity_change)
{
    if(!can_take_impulses())
    {
        return;
    }
    for(size_t i = 0; i < bodies_.size(); ++i)
    {
        runtime_.impulses.push_back({int(i), velocity_change, math::vec3{0.0f}, false, true});
    }
    dirty_.set();
}

void ragdoll_component::add_angular_velocity(const math::vec3& angular_velocity, const math::vec3& pivot)
{
    if(!can_take_impulses())
    {
        return;
    }
    for(size_t i = 0; i < bodies_.size() && i < runtime_.bodies.size(); ++i)
    {
        const math::vec3 offset = runtime_.bodies[i].world.get_position() - pivot;
        runtime_.impulses.push_back(
            {int(i), math::cross(angular_velocity, offset), math::vec3{0.0f}, false, true, angular_velocity});
    }
    dirty_.set();
}

void ragdoll_component::add_explosion(const math::vec3& center, float radius, float impulse)
{
    if(radius <= 0.0f || !can_take_impulses())
    {
        return;
    }
    float total_mass = 0.0f;
    for(const auto& body : bodies_)
    {
        total_mass += math::max(body.mass, 0.0f);
    }
    for(size_t i = 0; i < runtime_.bodies.size() && i < bodies_.size(); ++i)
    {
        const auto& body = runtime_.bodies[i];
        const math::vec3 position = body.has_physics_pose && body.simulated ? body.world.get_position()
                                                                            : body.anim_world.get_position();
        math::vec3 direction = position - center;
        const float distance = math::length(direction);
        if(distance > radius)
        {
            continue;
        }
        direction = distance > ragdoll_component_detail::min_direction_length ? direction / distance : math::vec3{0.0f, 1.0f, 0.0f};
        const float falloff = 1.0f - distance / radius;
        // The impulse is shared by mass, so every body gets the same velocity change, as Unreal's radial impulse.
        const float share = total_mass > 0.0f ? bodies_[i].mass / total_mass : 1.0f / float(bodies_.size());
        runtime_.impulses.push_back({int(i), direction * (impulse * falloff * share), math::vec3{0.0f}, false, false});
    }
    dirty_.set();
}

void ragdoll_component::set_drive_strength(float strength)
{
    strength = math::clamp(strength, 0.0f, 1.0f);
    if(runtime_.drive_strength == strength)
    {
        return;
    }
    runtime_.drive_strength = strength;
    runtime_.bodies_changed = true;
    dirty_.set();
}

auto ragdoll_component::get_drive_strength() const noexcept -> float
{
    return runtime_.drive_strength;
}

void ragdoll_component::reset_to_animation()
{
    runtime_.teleport = true;
    dirty_.set();
}

auto ragdoll_component::get_root_pose() const -> math::transform
{
    if(runtime_.write_order.empty())
    {
        return {};
    }
    const auto& root = runtime_.bodies[size_t(runtime_.write_order.front())];
    if(root.simulated && root.has_physics_pose)
    {
        return root.world;
    }
    if(root.weight > 0.0f)
    {
        return root.snapshot_world;
    }
    return root.anim_world;
}

auto ragdoll_component::is_face_up() const -> bool
{
    const math::transform root = get_root_pose();
    const math::vec3 forward = root.get_rotation() * runtime_.root_forward_local;
    return forward.y > 0.0f;
}

auto ragdoll_component::get_average_speed() const -> float
{
    if(runtime_.bodies.empty())
    {
        return 0.0f;
    }
    float sum = 0.0f;
    for(const auto& body : runtime_.bodies)
    {
        sum += math::length(body.linear_velocity);
    }
    return sum / float(runtime_.bodies.size());
}

auto ragdoll_component::get_rest_time() const noexcept -> float
{
    return runtime_.rest_time;
}

auto ragdoll_component::get_recovery_pose(const math::vec3& head_direction) const -> math::transform
{
    math::transform result;
    const math::transform root = get_root_pose();
    result.set_position(root.get_position());
    // Where the head lies from the hips, flat on the ground.
    math::vec3 lying = root.get_rotation() * runtime_.root_up_local;
    lying.y = 0.0f;
    math::vec3 wanted = head_direction;
    wanted.y = 0.0f;
    if(math::length(lying) < ragdoll_component_detail::min_direction_length ||
       math::length(wanted) < ragdoll_component_detail::min_direction_length)
    {
        math::vec3 forward = root.get_rotation() * runtime_.root_forward_local;
        forward.y = 0.0f;
        const float yaw = math::length(forward) > ragdoll_component_detail::min_direction_length ? std::atan2(forward.x, forward.z) : 0.0f;
        result.set_rotation(math::angleAxis(yaw, math::vec3{0.0f, 1.0f, 0.0f}));
        return result;
    }
    // The character turns so that its own head direction lies along the body's.
    const math::quat turn = ragdoll_component_detail::make_rotation_from_to(wanted, lying);
    const math::vec3 forward = turn * math::vec3{0.0f, 0.0f, 1.0f};
    const float yaw = std::atan2(forward.x, forward.z);
    result.set_rotation(math::angleAxis(yaw, math::vec3{0.0f, 1.0f, 0.0f}));
    return result;
}

auto ragdoll_component::is_dirty(uint8_t id) const noexcept -> bool
{
    return dirty_[id];
}

void ragdoll_component::set_dirty(uint8_t id, bool dirty) noexcept
{
    dirty_.set(id, dirty);
}

auto ragdoll_component::needs_rebuild() const noexcept -> bool
{
    return needs_rebuild_;
}

void ragdoll_component::set_needs_rebuild(bool rebuild) noexcept
{
    needs_rebuild_ = rebuild;
}

auto ragdoll_component::get_runtime() noexcept -> ragdoll_runtime&
{
    return runtime_;
}

auto ragdoll_component::get_runtime() const noexcept -> const ragdoll_runtime&
{
    return runtime_;
}

auto ragdoll_component::are_settings_dirty() const noexcept -> bool
{
    return settings_dirty_;
}

void ragdoll_component::set_settings_dirty(bool dirty) noexcept
{
    settings_dirty_ = dirty;
}

void ragdoll_component::reset_runtime_state()
{
    state_ = start_state_;
    runtime_ = {};
    needs_rebuild_ = true;
    settings_dirty_ = true;
    fit_runtime_to_definition();
}

} // namespace unravel
