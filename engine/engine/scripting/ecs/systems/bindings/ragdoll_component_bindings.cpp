#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/physics/ecs/components/ragdoll_component.h>
#include <engine/physics/ragdoll_builder.h>

#include <algorithm>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace ragdoll_bindings
{

/**
 * @brief Managed RagdollBody: the settings of a ragdoll_body without its bone name, which C# reads on its own, and its
 *        shape, which stays C++ only. Vectors are C#'s 12-byte Vector3, not the padded math::vec3.
 */
struct managed_ragdoll_body
{
    float mass;
    ragdoll_joint_type joint;
    dotnetpp_backend::managed_interface::vector3 joint_rotation;
    dotnetpp_backend::managed_interface::vector3 swing_offset;
    float swing_limit;
    float twist_lower;
    float twist_upper;
    float friction_scale;
    float drive_scale;
};

// RagdollBody and RagdollDrive in C# mirror these structs field for field and cross the boundary as they are. A
// field added on one side must be added on the other.
static_assert(sizeof(managed_ragdoll_body) == 52, "RagdollBody in C# is 52 bytes.");
static_assert(sizeof(ragdoll_drive) == 24, "RagdollDrive in C# is 24 bytes.");

/// Whether @p body indexes the ragdoll's definition (0 to bodyCount - 1): the one rule every per-body call checks.
auto is_body(const ragdoll_component& ragdoll, int body) -> bool
{
    return body >= 0 && size_t(body) < ragdoll.get_bodies().size();
}

/// The play state of a body, or null until play has prepared the ragdoll for its model.
auto find_running_body(const ragdoll_component& ragdoll, int body) -> const ragdoll_body_runtime*
{
    const auto& runtime = ragdoll.get_runtime();
    const bool is_running = runtime.is_prepared && is_body(ragdoll, body) && size_t(body) < runtime.bodies.size();
    return is_running ? &runtime.bodies[size_t(body)] : nullptr;
}

auto to_managed_body(const ragdoll_body& body) -> managed_ragdoll_body
{
    using converter = dotnet::managed_interface::converter;
    return {body.mass,
            body.joint,
            converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(body.joint_rotation),
            converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(body.swing_offset),
            body.swing_limit,
            body.twist_lower,
            body.twist_upper,
            body.friction_scale,
            body.drive_scale};
}

void apply_managed_body(const managed_ragdoll_body& from, ragdoll_body& to)
{
    using converter = dotnet::managed_interface::converter;
    to.mass = from.mass;
    to.joint = from.joint;
    to.joint_rotation = converter::convert<dotnetpp_backend::managed_interface::vector3, math::vec3>(from.joint_rotation);
    to.swing_offset = converter::convert<dotnetpp_backend::managed_interface::vector3, math::vec3>(from.swing_offset);
    to.swing_limit = from.swing_limit;
    to.twist_lower = from.twist_lower;
    to.twist_upper = from.twist_upper;
    to.friction_scale = from.friction_scale;
    to.drive_scale = from.drive_scale;
}

/// Scripts hand the joint type over as a number; one this build has no joint for is refused.
auto is_ragdoll_joint_type(ragdoll_joint_type type) -> bool
{
    return type <= ragdoll_joint_type::fixed;
}

/// Whether @p pair keeps the two bones apart, named in either order.
auto is_pair_of(const ragdoll_bone_pair& pair, const std::string& bone_a, const std::string& bone_b) -> bool
{
    return (pair.bone_a == bone_a && pair.bone_b == bone_b) || (pair.bone_a == bone_b && pair.bone_b == bone_a);
}

auto is_collision_disabled(const ragdoll_component& ragdoll, const std::string& bone_a, const std::string& bone_b)
    -> bool
{
    const auto& pairs = ragdoll.get_disabled_collisions();
    return std::any_of(pairs.begin(),
                       pairs.end(),
                       [&](const ragdoll_bone_pair& pair)
                       {
                           return is_pair_of(pair, bone_a, bone_b);
                       });
}

auto internal_m2n_ragdoll_get_state(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return uint8_t(comp->get_state());
    }
    return 0;
}

void internal_m2n_ragdoll_set_state(entt::entity id, uint8_t state, float blend_time)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_state(ragdoll_state(state), blend_time);
    }
}

auto internal_m2n_ragdoll_get_blend_time(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_blend_time();
    }
    return 0.0f;
}

void internal_m2n_ragdoll_set_blend_time(entt::entity id, float value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_blend_time(value);
    }
}

auto internal_m2n_ragdoll_get_joint_friction(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_joint_friction();
    }
    return 0.0f;
}

void internal_m2n_ragdoll_set_joint_friction(entt::entity id, float value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_joint_friction(value);
    }
}

auto internal_m2n_ragdoll_get_drive_strength(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_drive_strength();
    }
    return 0.0f;
}

void internal_m2n_ragdoll_set_drive_strength(entt::entity id, float value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_drive_strength(value);
    }
}

void internal_m2n_ragdoll_get_drive(entt::entity id, ragdoll_drive* value)
{
    auto comp = safe_get_component<ragdoll_component>(id);
    *value = comp ? comp->get_drive() : ragdoll_drive{};
}

void internal_m2n_ragdoll_set_drive(entt::entity id, const ragdoll_drive& value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_drive(value);
    }
}

auto internal_m2n_ragdoll_get_start_state(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return uint8_t(comp->get_start_state());
    }
    return 0;
}

void internal_m2n_ragdoll_set_start_state(entt::entity id, uint8_t state)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_start_state(ragdoll_state(state));
    }
}

auto internal_m2n_ragdoll_get_linear_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_linear_damping();
    }
    return 0.0f;
}

void internal_m2n_ragdoll_set_linear_damping(entt::entity id, float value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_linear_damping(value);
    }
}

auto internal_m2n_ragdoll_get_angular_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_angular_damping();
    }
    return 0.0f;
}

void internal_m2n_ragdoll_set_angular_damping(entt::entity id, float value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_angular_damping(value);
    }
}

auto internal_m2n_ragdoll_get_self_collision(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_self_collision();
    }
    return false;
}

void internal_m2n_ragdoll_set_self_collision(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_self_collision(value);
    }
}

auto internal_m2n_ragdoll_get_is_continuous(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->is_continuous();
    }
    return false;
}

void internal_m2n_ragdoll_set_is_continuous(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_is_continuous(value);
    }
}

auto internal_m2n_ragdoll_get_include_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_include_layers();
    }
    return {};
}

void internal_m2n_ragdoll_set_include_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_include_layers(mask);
    }
}

auto internal_m2n_ragdoll_get_exclude_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_exclude_layers();
    }
    return {};
}

void internal_m2n_ragdoll_set_exclude_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->set_exclude_layers(mask);
    }
}

auto internal_m2n_ragdoll_get_body_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return int(comp->get_bodies().size());
    }
    return 0;
}

auto internal_m2n_ragdoll_get_average_speed(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_average_speed();
    }
    return 0.0f;
}

auto internal_m2n_ragdoll_get_rest_time(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_rest_time();
    }
    return 0.0f;
}

auto internal_m2n_ragdoll_is_face_up(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->is_face_up();
    }
    return false;
}

auto internal_m2n_ragdoll_get_root_position(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_root_pose().get_position();
    }
    return {};
}

auto internal_m2n_ragdoll_get_root_rotation(entt::entity id) -> math::quat
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_root_pose().get_rotation();
    }
    return math::identity<math::quat>();
}

auto internal_m2n_ragdoll_set_bone_simulated(entt::entity id,
                                             const std::string& bone,
                                             bool simulated,
                                             bool include_children,
                                             float blend_time) -> bool
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->set_bone_simulated(bone, simulated, include_children, blend_time);
    }
    return false;
}

auto internal_m2n_ragdoll_is_body_simulated(entt::entity id, int body) -> bool
{
    if(auto comp = safe_get_component<ragdoll_component>(id); comp && is_body(*comp, body))
    {
        return comp->is_body_simulated(body);
    }
    return false;
}

auto internal_m2n_ragdoll_find_body_by_name(entt::entity id, const std::string& bone) -> int
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->find_body(bone);
    }
    return -1;
}

auto internal_m2n_ragdoll_find_body(entt::entity id, entt::entity bone) -> int
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->find_body(get_entity_from_id(bone));
    }
    return -1;
}

auto internal_m2n_ragdoll_get_body_bone(entt::entity id, int body) -> std::string
{
    if(auto comp = safe_get_component<ragdoll_component>(id); comp && is_body(*comp, body))
    {
        return comp->get_bodies()[size_t(body)].bone;
    }
    return {};
}

void internal_m2n_ragdoll_get_body(entt::entity id, int body, managed_ragdoll_body* value)
{
    static const ragdoll_body defaults{};
    auto comp = safe_get_component<ragdoll_component>(id);
    const bool is_valid = comp && is_body(*comp, body);
    *value = to_managed_body(is_valid ? comp->get_bodies()[size_t(body)] : defaults);
}

auto internal_m2n_ragdoll_set_body(entt::entity id, int body, const managed_ragdoll_body& value) -> bool
{
    auto comp = safe_get_component<ragdoll_component>(id);
    if(!comp || !is_body(*comp, body) || !is_ragdoll_joint_type(value.joint))
    {
        return false;
    }
    auto bodies = comp->get_bodies();
    apply_managed_body(value, bodies[size_t(body)]);
    comp->set_bodies(bodies);
    return true;
}

auto internal_m2n_ragdoll_get_body_position(entt::entity id, int body) -> math::vec3
{
    auto comp = safe_get_component<ragdoll_component>(id);
    if(const auto* running = comp ? find_running_body(*comp, body) : nullptr)
    {
        return running->has_physics_pose || !running->has_anim_pose ? running->world.get_position()
                                                                    : running->anim_world.get_position();
    }
    return {};
}

auto internal_m2n_ragdoll_get_body_velocity(entt::entity id, int body) -> math::vec3
{
    auto comp = safe_get_component<ragdoll_component>(id);
    if(const auto* running = comp ? find_running_body(*comp, body) : nullptr)
    {
        return running->linear_velocity;
    }
    return {};
}

auto internal_m2n_ragdoll_get_disabled_collision_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return int(comp->get_disabled_collisions().size());
    }
    return 0;
}

/// One bone of a pair that never collides; @p first picks bone_a over bone_b. Empty for a pair out of range.
auto get_disabled_collision_bone(entt::entity id, int pair, bool first) -> std::string
{
    auto comp = safe_get_component<ragdoll_component>(id);
    if(!comp || pair < 0 || size_t(pair) >= comp->get_disabled_collisions().size())
    {
        return {};
    }
    const auto& bones = comp->get_disabled_collisions()[size_t(pair)];
    return first ? bones.bone_a : bones.bone_b;
}

auto internal_m2n_ragdoll_get_disabled_collision_bone_a(entt::entity id, int pair) -> std::string
{
    return get_disabled_collision_bone(id, pair, true);
}

auto internal_m2n_ragdoll_get_disabled_collision_bone_b(entt::entity id, int pair) -> std::string
{
    return get_disabled_collision_bone(id, pair, false);
}

auto internal_m2n_ragdoll_is_collision_disabled(entt::entity id, const std::string& bone_a, const std::string& bone_b)
    -> bool
{
    auto comp = safe_get_component<ragdoll_component>(id);
    return comp && is_collision_disabled(*comp, bone_a, bone_b);
}

void internal_m2n_ragdoll_set_collision_disabled(entt::entity id,
                                                 const std::string& bone_a,
                                                 const std::string& bone_b,
                                                 bool disabled)
{
    auto comp = safe_get_component<ragdoll_component>(id);
    if(!comp || is_collision_disabled(*comp, bone_a, bone_b) == disabled)
    {
        return;
    }
    auto pairs = comp->get_disabled_collisions();
    if(disabled)
    {
        pairs.push_back({bone_a, bone_b});
    }
    else
    {
        const auto removed = std::remove_if(pairs.begin(),
                                            pairs.end(),
                                            [&](const ragdoll_bone_pair& pair)
                                            {
                                                return is_pair_of(pair, bone_a, bone_b);
                                            });
        pairs.erase(removed, pairs.end());
    }
    comp->set_disabled_collisions(pairs);
}

void internal_m2n_ragdoll_add_impulse_at(entt::entity id, int body, const math::vec3& impulse, const math::vec3& point)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->add_impulse(body, impulse, point);
    }
}

void internal_m2n_ragdoll_add_impulse(entt::entity id, int body, const math::vec3& impulse)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->add_impulse(body, impulse);
    }
}

void internal_m2n_ragdoll_add_velocity(entt::entity id, const math::vec3& velocity_change)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->add_velocity(velocity_change);
    }
}

void internal_m2n_ragdoll_add_angular_velocity(entt::entity id, const math::vec3& angular_velocity, const math::vec3& pivot)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->add_angular_velocity(angular_velocity, pivot);
    }
}

void internal_m2n_ragdoll_add_explosion(entt::entity id, const math::vec3& center, float radius, float impulse)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->add_explosion(center, radius, impulse);
    }
}

void internal_m2n_ragdoll_reset_to_animation(entt::entity id)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        comp->reset_to_animation();
    }
}

void internal_m2n_ragdoll_get_recovery_pose(entt::entity id,
                                            const math::vec3& head_direction,
                                            dotnetpp_backend::managed_interface::vector3* position,
                                            dotnetpp_backend::managed_interface::quaternion* rotation)
{
    using converter = dotnet::managed_interface::converter;
    auto comp = safe_get_component<ragdoll_component>(id);
    const math::transform pose = comp ? comp->get_recovery_pose(head_direction) : math::transform{};
    *position = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(pose.get_position());
    *rotation = converter::convert<math::quat, dotnetpp_backend::managed_interface::quaternion>(pose.get_rotation());
}

auto internal_m2n_ragdoll_auto_setup(entt::entity id, float total_mass) -> bool
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        // Written so a NaN mass is refused too.
        if(!(total_mass > 0.0f))
        {
            APPLOG_WARNING("RagdollComponent.AutoSetup: the total mass must be more than 0 kg, not {}.", total_mass);
            return false;
        }
        ragdoll_build_options options;
        options.total_mass = total_mass;
        const auto result = setup_ragdoll(*comp, options);
        if(!result.ok)
        {
            APPLOG_WARNING("RagdollComponent.AutoSetup: {}", result.error);
        }
        return result.ok;
    }
    return false;
}

} // namespace ragdoll_bindings

void register_ragdoll_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    using namespace ragdoll_bindings;
    auto reg = dotnet::internal_call_registry("Unravel.Core.RagdollComponent");
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_state);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_state);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_blend_time);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_blend_time);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_joint_friction);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_joint_friction);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_drive_strength);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_drive_strength);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_drive);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_drive);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_start_state);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_start_state);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_linear_damping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_linear_damping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_angular_damping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_angular_damping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_self_collision);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_self_collision);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_is_continuous);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_is_continuous);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_include_layers);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_include_layers);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_exclude_layers);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_exclude_layers);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_body_count);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_average_speed);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_rest_time);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_is_face_up);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_root_position);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_root_rotation);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_bone_simulated);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_is_body_simulated);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_find_body_by_name);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_find_body);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_body_bone);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_body);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_body);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_body_position);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_body_velocity);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_disabled_collision_count);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_disabled_collision_bone_a);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_disabled_collision_bone_b);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_is_collision_disabled);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_set_collision_disabled);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_add_impulse_at);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_add_impulse);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_add_velocity);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_add_angular_velocity);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_add_explosion);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_reset_to_animation);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_get_recovery_pose);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_ragdoll_auto_setup);
}

} // namespace unravel
