#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/physics/ecs/components/ragdoll_component.h>
#include <engine/physics/ragdoll_builder.h>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace ragdoll_bindings
{

auto is_body(const ragdoll_component& ragdoll, int body) -> bool
{
    return body >= 0 && size_t(body) < ragdoll.get_runtime().bodies.size();
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

auto internal_m2n_ragdoll_get_local_drive(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_drive().local_frequency;
    }
    return 0.0f;
}

void internal_m2n_ragdoll_set_local_drive(entt::entity id, float value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        auto drive = comp->get_drive();
        drive.local_frequency = math::max(value, 0.0f);
        comp->set_drive(drive);
    }
}

auto internal_m2n_ragdoll_get_world_drive(entt::entity id) -> float
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_drive().world_frequency;
    }
    return 0.0f;
}

void internal_m2n_ragdoll_set_world_drive(entt::entity id, float value)
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        auto drive = comp->get_drive();
        drive.world_frequency = math::max(value, 0.0f);
        comp->set_drive(drive);
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
    if(auto comp = safe_get_component<ragdoll_component>(id))
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
    auto comp = safe_get_component<ragdoll_component>(id);
    if(!comp)
    {
        return -1;
    }
    auto& registry = *engine::context().get_cached<ecs>().get_scene().registry;
    return registry.valid(bone) ? comp->find_body(entt::handle(registry, bone)) : -1;
}

auto internal_m2n_ragdoll_get_body_bone(entt::entity id, int body) -> std::string
{
    if(auto comp = safe_get_component<ragdoll_component>(id); comp && body >= 0 && size_t(body) < comp->get_bodies().size())
    {
        return comp->get_bodies()[size_t(body)].bone;
    }
    return {};
}

auto internal_m2n_ragdoll_get_body_position(entt::entity id, int body) -> math::vec3
{
    if(auto comp = safe_get_component<ragdoll_component>(id); comp && is_body(*comp, body))
    {
        const auto& runtime = comp->get_runtime().bodies[size_t(body)];
        return runtime.has_physics_pose || !runtime.has_anim_pose ? runtime.world.get_position()
                                                                  : runtime.anim_world.get_position();
    }
    return {};
}

auto internal_m2n_ragdoll_get_body_velocity(entt::entity id, int body) -> math::vec3
{
    if(auto comp = safe_get_component<ragdoll_component>(id); comp && is_body(*comp, body))
    {
        return comp->get_runtime().bodies[size_t(body)].linear_velocity;
    }
    return {};
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

auto internal_m2n_ragdoll_get_recovery_position(entt::entity id, const math::vec3& head_direction) -> math::vec3
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_recovery_pose(head_direction).get_position();
    }
    return {};
}

auto internal_m2n_ragdoll_get_recovery_rotation(entt::entity id, const math::vec3& head_direction) -> math::quat
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        return comp->get_recovery_pose(head_direction).get_rotation();
    }
    return math::identity<math::quat>();
}

auto internal_m2n_ragdoll_auto_setup(entt::entity id, float total_mass) -> bool
{
    if(auto comp = safe_get_component<ragdoll_component>(id))
    {
        ragdoll_build_options options;
        options.total_mass = math::max(total_mass, 1.0f);
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
#define UNRAVEL_RAGDOLL_CALL(name) reg.add_internal_call(#name, script_internal_call(name))
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_state);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_set_state);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_blend_time);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_set_blend_time);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_joint_friction);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_set_joint_friction);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_drive_strength);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_set_drive_strength);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_local_drive);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_set_local_drive);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_world_drive);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_set_world_drive);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_body_count);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_average_speed);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_rest_time);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_is_face_up);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_root_position);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_root_rotation);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_set_bone_simulated);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_is_body_simulated);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_find_body_by_name);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_find_body);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_body_bone);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_body_position);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_body_velocity);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_add_impulse_at);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_add_impulse);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_add_velocity);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_add_angular_velocity);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_add_explosion);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_reset_to_animation);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_recovery_position);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_get_recovery_rotation);
    UNRAVEL_RAGDOLL_CALL(internal_m2n_ragdoll_auto_setup);
#undef UNRAVEL_RAGDOLL_CALL
}

} // namespace unravel
