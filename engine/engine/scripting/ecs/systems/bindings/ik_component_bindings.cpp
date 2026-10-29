#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/animation/ecs/components/ik_components.h>

namespace unravel
{
namespace
{

auto internal_m2n_two_bone_ik_get_target(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->target ? comp->target.entity() : entt::entity(0);
    }
    return entt::entity(0);
}

void internal_m2n_two_bone_ik_set_target(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        comp->target = get_entity_from_id(value);
    }
}

auto internal_m2n_two_bone_ik_get_pole(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->pole ? comp->pole.entity() : entt::entity(0);
    }
    return entt::entity(0);
}

void internal_m2n_two_bone_ik_set_pole(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        comp->pole = get_entity_from_id(value);
    }
}

auto internal_m2n_two_bone_ik_get_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->weight;
    }
    return 0.0f;
}

void internal_m2n_two_bone_ik_set_weight(entt::entity id, float value)
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        comp->weight = value;
    }
}

auto internal_m2n_two_bone_ik_get_rotation_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->rotation_weight;
    }
    return 0.0f;
}

void internal_m2n_two_bone_ik_set_rotation_weight(entt::entity id, float value)
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        comp->rotation_weight = value;
    }
}

auto internal_m2n_two_bone_ik_get_soften(entt::entity id) -> float
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->soften;
    }
    return 0.0f;
}

void internal_m2n_two_bone_ik_set_soften(entt::entity id, float value)
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        comp->soften = value;
    }
}

auto internal_m2n_two_bone_ik_get_smoothing(entt::entity id) -> float
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->smoothing;
    }
    return 0.0f;
}

void internal_m2n_two_bone_ik_set_smoothing(entt::entity id, float value)
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        comp->smoothing = value;
    }
}

auto internal_m2n_two_bone_ik_get_weight_curve(entt::entity id) -> std::string
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->weight_curve;
    }
    return {};
}

void internal_m2n_two_bone_ik_set_weight_curve(entt::entity id, const std::string& value)
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        comp->weight_curve = value;
    }
}

auto internal_m2n_two_bone_ik_is_solved(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->debug.solved;
    }
    return false;
}

auto internal_m2n_two_bone_ik_is_reached(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->debug.reached;
    }
    return false;
}

auto internal_m2n_two_bone_ik_get_effective_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<two_bone_ik_component>(id))
    {
        return comp->debug.effective_weight;
    }
    return 0.0f;
}

auto internal_m2n_chain_ik_get_target(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->target ? comp->target.entity() : entt::entity(0);
    }
    return entt::entity(0);
}

void internal_m2n_chain_ik_set_target(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->target = get_entity_from_id(value);
    }
}

auto internal_m2n_chain_ik_get_pole(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->pole ? comp->pole.entity() : entt::entity(0);
    }
    return entt::entity(0);
}

void internal_m2n_chain_ik_set_pole(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->pole = get_entity_from_id(value);
    }
}

auto internal_m2n_chain_ik_get_chain_length(entt::entity id) -> int
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->chain_length;
    }
    return 0;
}

void internal_m2n_chain_ik_set_chain_length(entt::entity id, int value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->chain_length = value;
    }
}

auto internal_m2n_chain_ik_get_solver(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return static_cast<uint8_t>(comp->solver);
    }
    return 0;
}

void internal_m2n_chain_ik_set_solver(entt::entity id, uint8_t value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->solver = static_cast<chain_ik_solver>(value);
    }
}

auto internal_m2n_chain_ik_get_iterations(entt::entity id) -> int
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->iterations;
    }
    return 0;
}

void internal_m2n_chain_ik_set_iterations(entt::entity id, int value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->iterations = value;
    }
}

auto internal_m2n_chain_ik_get_tolerance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->tolerance;
    }
    return 0.0f;
}

void internal_m2n_chain_ik_set_tolerance(entt::entity id, float value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->tolerance = value;
    }
}

auto internal_m2n_chain_ik_get_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->weight;
    }
    return 0.0f;
}

void internal_m2n_chain_ik_set_weight(entt::entity id, float value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->weight = value;
    }
}

auto internal_m2n_chain_ik_get_rotation_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->rotation_weight;
    }
    return 0.0f;
}

void internal_m2n_chain_ik_set_rotation_weight(entt::entity id, float value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->rotation_weight = value;
    }
}

auto internal_m2n_chain_ik_get_smoothing(entt::entity id) -> float
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->smoothing;
    }
    return 0.0f;
}

void internal_m2n_chain_ik_set_smoothing(entt::entity id, float value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->smoothing = value;
    }
}

auto internal_m2n_chain_ik_get_weight_curve(entt::entity id) -> std::string
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->weight_curve;
    }
    return {};
}

void internal_m2n_chain_ik_set_weight_curve(entt::entity id, const std::string& value)
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        comp->weight_curve = value;
    }
}

auto internal_m2n_chain_ik_is_solved(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->debug.solved;
    }
    return false;
}

auto internal_m2n_chain_ik_is_reached(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->debug.reached;
    }
    return false;
}

auto internal_m2n_chain_ik_get_effective_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<chain_ik_component>(id))
    {
        return comp->debug.effective_weight;
    }
    return 0.0f;
}

auto internal_m2n_look_at_ik_get_target(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->target ? comp->target.entity() : entt::entity(0);
    }
    return entt::entity(0);
}

void internal_m2n_look_at_ik_set_target(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->target = get_entity_from_id(value);
    }
}

auto internal_m2n_look_at_ik_get_target_offset(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->target_offset;
    }
    return {};
}

void internal_m2n_look_at_ik_set_target_offset(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->target_offset = value;
    }
}

auto internal_m2n_look_at_ik_get_chain_length(entt::entity id) -> int
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->chain_length;
    }
    return 0;
}

void internal_m2n_look_at_ik_set_chain_length(entt::entity id, int value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->chain_length = value;
    }
}

auto internal_m2n_look_at_ik_get_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->weight;
    }
    return 0.0f;
}

void internal_m2n_look_at_ik_set_weight(entt::entity id, float value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->weight = value;
    }
}

auto internal_m2n_look_at_ik_get_root_share(entt::entity id) -> float
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->root_share;
    }
    return 0.0f;
}

void internal_m2n_look_at_ik_set_root_share(entt::entity id, float value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->root_share = value;
    }
}

auto internal_m2n_look_at_ik_get_max_bone_angle(entt::entity id) -> float
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->max_bone_angle;
    }
    return 0.0f;
}

void internal_m2n_look_at_ik_set_max_bone_angle(entt::entity id, float value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->max_bone_angle = value;
    }
}

auto internal_m2n_look_at_ik_get_max_turn_angle(entt::entity id) -> float
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->max_turn_angle;
    }
    return 0.0f;
}

void internal_m2n_look_at_ik_set_max_turn_angle(entt::entity id, float value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->max_turn_angle = value;
    }
}

auto internal_m2n_look_at_ik_get_smoothing(entt::entity id) -> float
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->smoothing;
    }
    return 0.0f;
}

void internal_m2n_look_at_ik_set_smoothing(entt::entity id, float value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->smoothing = value;
    }
}

auto internal_m2n_look_at_ik_get_forward_axis(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->forward_axis;
    }
    return {};
}

void internal_m2n_look_at_ik_set_forward_axis(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->forward_axis = value;
    }
}

auto internal_m2n_look_at_ik_get_weight_curve(entt::entity id) -> std::string
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->weight_curve;
    }
    return {};
}

void internal_m2n_look_at_ik_set_weight_curve(entt::entity id, const std::string& value)
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        comp->weight_curve = value;
    }
}

auto internal_m2n_look_at_ik_is_solved(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->debug.solved;
    }
    return false;
}

auto internal_m2n_look_at_ik_is_reached(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->debug.reached;
    }
    return false;
}

auto internal_m2n_look_at_ik_get_effective_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<look_at_ik_component>(id))
    {
        return comp->debug.effective_weight;
    }
    return 0.0f;
}

auto internal_m2n_foot_ik_get_left_foot(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->left_foot ? comp->left_foot.entity() : entt::entity(0);
    }
    return entt::entity(0);
}

void internal_m2n_foot_ik_set_left_foot(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->left_foot = get_entity_from_id(value);
    }
}

auto internal_m2n_foot_ik_get_right_foot(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->right_foot ? comp->right_foot.entity() : entt::entity(0);
    }
    return entt::entity(0);
}

void internal_m2n_foot_ik_set_right_foot(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->right_foot = get_entity_from_id(value);
    }
}

auto internal_m2n_foot_ik_get_pelvis(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->pelvis ? comp->pelvis.entity() : entt::entity(0);
    }
    return entt::entity(0);
}

void internal_m2n_foot_ik_set_pelvis(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->pelvis = get_entity_from_id(value);
    }
}

auto internal_m2n_foot_ik_get_ground_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->ground_layers;
    }
    return {};
}

void internal_m2n_foot_ik_set_ground_layers(entt::entity id, layer_mask value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->ground_layers = value;
    }
}

auto internal_m2n_foot_ik_get_probe_up(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->probe_up;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_probe_up(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->probe_up = value;
    }
}

auto internal_m2n_foot_ik_get_probe_down(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->probe_down;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_probe_down(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->probe_down = value;
    }
}

auto internal_m2n_foot_ik_get_lift_start(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->lift_start;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_lift_start(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->lift_start = value;
    }
}

auto internal_m2n_foot_ik_get_lift_end(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->lift_end;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_lift_end(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->lift_end = value;
    }
}

auto internal_m2n_foot_ik_get_drop_start(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->drop_start;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_drop_start(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->drop_start = value;
    }
}

auto internal_m2n_foot_ik_get_drop_end(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->drop_end;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_drop_end(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->drop_end = value;
    }
}

auto internal_m2n_foot_ik_get_max_pelvis_drop(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->max_pelvis_drop;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_max_pelvis_drop(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->max_pelvis_drop = value;
    }
}

auto internal_m2n_foot_ik_get_smoothing(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->smoothing;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_smoothing(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->smoothing = value;
    }
}

auto internal_m2n_foot_ik_get_align_to_ground(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->align_to_ground;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_align_to_ground(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->align_to_ground = value;
    }
}

auto internal_m2n_foot_ik_get_knee_forward(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->knee_forward;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_knee_forward(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->knee_forward = value;
    }
}

auto internal_m2n_foot_ik_get_knee_outward(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->knee_outward;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_knee_outward(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->knee_outward = value;
    }
}

auto internal_m2n_foot_ik_get_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->weight;
    }
    return 0.0f;
}

void internal_m2n_foot_ik_set_weight(entt::entity id, float value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->weight = value;
    }
}

auto internal_m2n_foot_ik_get_weight_curve(entt::entity id) -> std::string
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->weight_curve;
    }
    return {};
}

void internal_m2n_foot_ik_set_weight_curve(entt::entity id, const std::string& value)
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        comp->weight_curve = value;
    }
}

auto internal_m2n_foot_ik_get_pelvis_offset(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->pelvis_offset;
    }
    return 0.0f;
}

auto internal_m2n_foot_ik_get_left_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->left_weight;
    }
    return 0.0f;
}

auto internal_m2n_foot_ik_get_right_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<foot_ik_component>(id))
    {
        return comp->right_weight;
    }
    return 0.0f;
}

auto internal_m2n_spring_bone_get_stiffness(entt::entity id) -> float
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        return comp->stiffness;
    }
    return 0.0f;
}

void internal_m2n_spring_bone_set_stiffness(entt::entity id, float value)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->stiffness = value;
    }
}

auto internal_m2n_spring_bone_get_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        return comp->damping;
    }
    return 0.0f;
}

void internal_m2n_spring_bone_set_damping(entt::entity id, float value)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->damping = value;
    }
}

auto internal_m2n_spring_bone_get_gravity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        return comp->gravity;
    }
    return {};
}

void internal_m2n_spring_bone_set_gravity(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->gravity = value;
    }
}

auto internal_m2n_spring_bone_get_radius(entt::entity id) -> float
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        return comp->radius;
    }
    return 0.0f;
}

void internal_m2n_spring_bone_set_radius(entt::entity id, float value)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->radius = value;
    }
}

auto internal_m2n_spring_bone_get_chain_length(entt::entity id) -> int
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        return comp->chain_length;
    }
    return 0;
}

void internal_m2n_spring_bone_set_chain_length(entt::entity id, int value)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->chain_length = value;
    }
}

auto internal_m2n_spring_bone_get_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        return comp->weight;
    }
    return 0.0f;
}

void internal_m2n_spring_bone_set_weight(entt::entity id, float value)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->weight = value;
    }
}

auto internal_m2n_spring_bone_is_solved(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        return comp->debug.solved;
    }
    return false;
}

auto internal_m2n_spring_bone_get_collider_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        return int(comp->colliders.size());
    }
    return 0;
}

void internal_m2n_spring_bone_add_collider(entt::entity id, entt::entity collider, float radius, const math::vec3& offset)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->colliders.push_back({get_entity_from_id(collider), radius, offset});
    }
}

void internal_m2n_spring_bone_clear_colliders(entt::entity id)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->colliders.clear();
    }
}

void internal_m2n_spring_bone_reset_simulation(entt::entity id)
{
    if(auto comp = safe_get_component<spring_bone_component>(id))
    {
        comp->reset_simulation();
    }
}

} // namespace

void register_ik_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.TwoBoneIKComponent");
        reg.add_internal_call("internal_m2n_two_bone_ik_get_target", script_internal_call(internal_m2n_two_bone_ik_get_target));
        reg.add_internal_call("internal_m2n_two_bone_ik_set_target", script_internal_call(internal_m2n_two_bone_ik_set_target));
        reg.add_internal_call("internal_m2n_two_bone_ik_get_pole", script_internal_call(internal_m2n_two_bone_ik_get_pole));
        reg.add_internal_call("internal_m2n_two_bone_ik_set_pole", script_internal_call(internal_m2n_two_bone_ik_set_pole));
        reg.add_internal_call("internal_m2n_two_bone_ik_get_weight", script_internal_call(internal_m2n_two_bone_ik_get_weight));
        reg.add_internal_call("internal_m2n_two_bone_ik_set_weight", script_internal_call(internal_m2n_two_bone_ik_set_weight));
        reg.add_internal_call("internal_m2n_two_bone_ik_get_rotation_weight", script_internal_call(internal_m2n_two_bone_ik_get_rotation_weight));
        reg.add_internal_call("internal_m2n_two_bone_ik_set_rotation_weight", script_internal_call(internal_m2n_two_bone_ik_set_rotation_weight));
        reg.add_internal_call("internal_m2n_two_bone_ik_get_soften", script_internal_call(internal_m2n_two_bone_ik_get_soften));
        reg.add_internal_call("internal_m2n_two_bone_ik_set_soften", script_internal_call(internal_m2n_two_bone_ik_set_soften));
        reg.add_internal_call("internal_m2n_two_bone_ik_get_smoothing", script_internal_call(internal_m2n_two_bone_ik_get_smoothing));
        reg.add_internal_call("internal_m2n_two_bone_ik_set_smoothing", script_internal_call(internal_m2n_two_bone_ik_set_smoothing));
        reg.add_internal_call("internal_m2n_two_bone_ik_get_weight_curve", script_internal_call(internal_m2n_two_bone_ik_get_weight_curve));
        reg.add_internal_call("internal_m2n_two_bone_ik_set_weight_curve", script_internal_call(internal_m2n_two_bone_ik_set_weight_curve));
        reg.add_internal_call("internal_m2n_two_bone_ik_is_solved", script_internal_call(internal_m2n_two_bone_ik_is_solved));
        reg.add_internal_call("internal_m2n_two_bone_ik_is_reached", script_internal_call(internal_m2n_two_bone_ik_is_reached));
        reg.add_internal_call("internal_m2n_two_bone_ik_get_effective_weight", script_internal_call(internal_m2n_two_bone_ik_get_effective_weight));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.ChainIKComponent");
        reg.add_internal_call("internal_m2n_chain_ik_get_target", script_internal_call(internal_m2n_chain_ik_get_target));
        reg.add_internal_call("internal_m2n_chain_ik_set_target", script_internal_call(internal_m2n_chain_ik_set_target));
        reg.add_internal_call("internal_m2n_chain_ik_get_pole", script_internal_call(internal_m2n_chain_ik_get_pole));
        reg.add_internal_call("internal_m2n_chain_ik_set_pole", script_internal_call(internal_m2n_chain_ik_set_pole));
        reg.add_internal_call("internal_m2n_chain_ik_get_chain_length", script_internal_call(internal_m2n_chain_ik_get_chain_length));
        reg.add_internal_call("internal_m2n_chain_ik_set_chain_length", script_internal_call(internal_m2n_chain_ik_set_chain_length));
        reg.add_internal_call("internal_m2n_chain_ik_get_solver", script_internal_call(internal_m2n_chain_ik_get_solver));
        reg.add_internal_call("internal_m2n_chain_ik_set_solver", script_internal_call(internal_m2n_chain_ik_set_solver));
        reg.add_internal_call("internal_m2n_chain_ik_get_iterations", script_internal_call(internal_m2n_chain_ik_get_iterations));
        reg.add_internal_call("internal_m2n_chain_ik_set_iterations", script_internal_call(internal_m2n_chain_ik_set_iterations));
        reg.add_internal_call("internal_m2n_chain_ik_get_tolerance", script_internal_call(internal_m2n_chain_ik_get_tolerance));
        reg.add_internal_call("internal_m2n_chain_ik_set_tolerance", script_internal_call(internal_m2n_chain_ik_set_tolerance));
        reg.add_internal_call("internal_m2n_chain_ik_get_weight", script_internal_call(internal_m2n_chain_ik_get_weight));
        reg.add_internal_call("internal_m2n_chain_ik_set_weight", script_internal_call(internal_m2n_chain_ik_set_weight));
        reg.add_internal_call("internal_m2n_chain_ik_get_rotation_weight", script_internal_call(internal_m2n_chain_ik_get_rotation_weight));
        reg.add_internal_call("internal_m2n_chain_ik_set_rotation_weight", script_internal_call(internal_m2n_chain_ik_set_rotation_weight));
        reg.add_internal_call("internal_m2n_chain_ik_get_smoothing", script_internal_call(internal_m2n_chain_ik_get_smoothing));
        reg.add_internal_call("internal_m2n_chain_ik_set_smoothing", script_internal_call(internal_m2n_chain_ik_set_smoothing));
        reg.add_internal_call("internal_m2n_chain_ik_get_weight_curve", script_internal_call(internal_m2n_chain_ik_get_weight_curve));
        reg.add_internal_call("internal_m2n_chain_ik_set_weight_curve", script_internal_call(internal_m2n_chain_ik_set_weight_curve));
        reg.add_internal_call("internal_m2n_chain_ik_is_solved", script_internal_call(internal_m2n_chain_ik_is_solved));
        reg.add_internal_call("internal_m2n_chain_ik_is_reached", script_internal_call(internal_m2n_chain_ik_is_reached));
        reg.add_internal_call("internal_m2n_chain_ik_get_effective_weight", script_internal_call(internal_m2n_chain_ik_get_effective_weight));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.LookAtIKComponent");
        reg.add_internal_call("internal_m2n_look_at_ik_get_target", script_internal_call(internal_m2n_look_at_ik_get_target));
        reg.add_internal_call("internal_m2n_look_at_ik_set_target", script_internal_call(internal_m2n_look_at_ik_set_target));
        reg.add_internal_call("internal_m2n_look_at_ik_get_target_offset", script_internal_call(internal_m2n_look_at_ik_get_target_offset));
        reg.add_internal_call("internal_m2n_look_at_ik_set_target_offset", script_internal_call(internal_m2n_look_at_ik_set_target_offset));
        reg.add_internal_call("internal_m2n_look_at_ik_get_chain_length", script_internal_call(internal_m2n_look_at_ik_get_chain_length));
        reg.add_internal_call("internal_m2n_look_at_ik_set_chain_length", script_internal_call(internal_m2n_look_at_ik_set_chain_length));
        reg.add_internal_call("internal_m2n_look_at_ik_get_weight", script_internal_call(internal_m2n_look_at_ik_get_weight));
        reg.add_internal_call("internal_m2n_look_at_ik_set_weight", script_internal_call(internal_m2n_look_at_ik_set_weight));
        reg.add_internal_call("internal_m2n_look_at_ik_get_root_share", script_internal_call(internal_m2n_look_at_ik_get_root_share));
        reg.add_internal_call("internal_m2n_look_at_ik_set_root_share", script_internal_call(internal_m2n_look_at_ik_set_root_share));
        reg.add_internal_call("internal_m2n_look_at_ik_get_max_bone_angle", script_internal_call(internal_m2n_look_at_ik_get_max_bone_angle));
        reg.add_internal_call("internal_m2n_look_at_ik_set_max_bone_angle", script_internal_call(internal_m2n_look_at_ik_set_max_bone_angle));
        reg.add_internal_call("internal_m2n_look_at_ik_get_max_turn_angle", script_internal_call(internal_m2n_look_at_ik_get_max_turn_angle));
        reg.add_internal_call("internal_m2n_look_at_ik_set_max_turn_angle", script_internal_call(internal_m2n_look_at_ik_set_max_turn_angle));
        reg.add_internal_call("internal_m2n_look_at_ik_get_smoothing", script_internal_call(internal_m2n_look_at_ik_get_smoothing));
        reg.add_internal_call("internal_m2n_look_at_ik_set_smoothing", script_internal_call(internal_m2n_look_at_ik_set_smoothing));
        reg.add_internal_call("internal_m2n_look_at_ik_get_forward_axis", script_internal_call(internal_m2n_look_at_ik_get_forward_axis));
        reg.add_internal_call("internal_m2n_look_at_ik_set_forward_axis", script_internal_call(internal_m2n_look_at_ik_set_forward_axis));
        reg.add_internal_call("internal_m2n_look_at_ik_get_weight_curve", script_internal_call(internal_m2n_look_at_ik_get_weight_curve));
        reg.add_internal_call("internal_m2n_look_at_ik_set_weight_curve", script_internal_call(internal_m2n_look_at_ik_set_weight_curve));
        reg.add_internal_call("internal_m2n_look_at_ik_is_solved", script_internal_call(internal_m2n_look_at_ik_is_solved));
        reg.add_internal_call("internal_m2n_look_at_ik_is_reached", script_internal_call(internal_m2n_look_at_ik_is_reached));
        reg.add_internal_call("internal_m2n_look_at_ik_get_effective_weight", script_internal_call(internal_m2n_look_at_ik_get_effective_weight));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.FootIKComponent");
        reg.add_internal_call("internal_m2n_foot_ik_get_left_foot", script_internal_call(internal_m2n_foot_ik_get_left_foot));
        reg.add_internal_call("internal_m2n_foot_ik_set_left_foot", script_internal_call(internal_m2n_foot_ik_set_left_foot));
        reg.add_internal_call("internal_m2n_foot_ik_get_right_foot", script_internal_call(internal_m2n_foot_ik_get_right_foot));
        reg.add_internal_call("internal_m2n_foot_ik_set_right_foot", script_internal_call(internal_m2n_foot_ik_set_right_foot));
        reg.add_internal_call("internal_m2n_foot_ik_get_pelvis", script_internal_call(internal_m2n_foot_ik_get_pelvis));
        reg.add_internal_call("internal_m2n_foot_ik_set_pelvis", script_internal_call(internal_m2n_foot_ik_set_pelvis));
        reg.add_internal_call("internal_m2n_foot_ik_get_ground_layers", script_internal_call(internal_m2n_foot_ik_get_ground_layers));
        reg.add_internal_call("internal_m2n_foot_ik_set_ground_layers", script_internal_call(internal_m2n_foot_ik_set_ground_layers));
        reg.add_internal_call("internal_m2n_foot_ik_get_probe_up", script_internal_call(internal_m2n_foot_ik_get_probe_up));
        reg.add_internal_call("internal_m2n_foot_ik_set_probe_up", script_internal_call(internal_m2n_foot_ik_set_probe_up));
        reg.add_internal_call("internal_m2n_foot_ik_get_probe_down", script_internal_call(internal_m2n_foot_ik_get_probe_down));
        reg.add_internal_call("internal_m2n_foot_ik_set_probe_down", script_internal_call(internal_m2n_foot_ik_set_probe_down));
        reg.add_internal_call("internal_m2n_foot_ik_get_lift_start", script_internal_call(internal_m2n_foot_ik_get_lift_start));
        reg.add_internal_call("internal_m2n_foot_ik_set_lift_start", script_internal_call(internal_m2n_foot_ik_set_lift_start));
        reg.add_internal_call("internal_m2n_foot_ik_get_lift_end", script_internal_call(internal_m2n_foot_ik_get_lift_end));
        reg.add_internal_call("internal_m2n_foot_ik_set_lift_end", script_internal_call(internal_m2n_foot_ik_set_lift_end));
        reg.add_internal_call("internal_m2n_foot_ik_get_drop_start", script_internal_call(internal_m2n_foot_ik_get_drop_start));
        reg.add_internal_call("internal_m2n_foot_ik_set_drop_start", script_internal_call(internal_m2n_foot_ik_set_drop_start));
        reg.add_internal_call("internal_m2n_foot_ik_get_drop_end", script_internal_call(internal_m2n_foot_ik_get_drop_end));
        reg.add_internal_call("internal_m2n_foot_ik_set_drop_end", script_internal_call(internal_m2n_foot_ik_set_drop_end));
        reg.add_internal_call("internal_m2n_foot_ik_get_max_pelvis_drop", script_internal_call(internal_m2n_foot_ik_get_max_pelvis_drop));
        reg.add_internal_call("internal_m2n_foot_ik_set_max_pelvis_drop", script_internal_call(internal_m2n_foot_ik_set_max_pelvis_drop));
        reg.add_internal_call("internal_m2n_foot_ik_get_smoothing", script_internal_call(internal_m2n_foot_ik_get_smoothing));
        reg.add_internal_call("internal_m2n_foot_ik_set_smoothing", script_internal_call(internal_m2n_foot_ik_set_smoothing));
        reg.add_internal_call("internal_m2n_foot_ik_get_align_to_ground", script_internal_call(internal_m2n_foot_ik_get_align_to_ground));
        reg.add_internal_call("internal_m2n_foot_ik_set_align_to_ground", script_internal_call(internal_m2n_foot_ik_set_align_to_ground));
        reg.add_internal_call("internal_m2n_foot_ik_get_knee_forward", script_internal_call(internal_m2n_foot_ik_get_knee_forward));
        reg.add_internal_call("internal_m2n_foot_ik_set_knee_forward", script_internal_call(internal_m2n_foot_ik_set_knee_forward));
        reg.add_internal_call("internal_m2n_foot_ik_get_knee_outward", script_internal_call(internal_m2n_foot_ik_get_knee_outward));
        reg.add_internal_call("internal_m2n_foot_ik_set_knee_outward", script_internal_call(internal_m2n_foot_ik_set_knee_outward));
        reg.add_internal_call("internal_m2n_foot_ik_get_weight", script_internal_call(internal_m2n_foot_ik_get_weight));
        reg.add_internal_call("internal_m2n_foot_ik_set_weight", script_internal_call(internal_m2n_foot_ik_set_weight));
        reg.add_internal_call("internal_m2n_foot_ik_get_weight_curve", script_internal_call(internal_m2n_foot_ik_get_weight_curve));
        reg.add_internal_call("internal_m2n_foot_ik_set_weight_curve", script_internal_call(internal_m2n_foot_ik_set_weight_curve));
        reg.add_internal_call("internal_m2n_foot_ik_get_pelvis_offset", script_internal_call(internal_m2n_foot_ik_get_pelvis_offset));
        reg.add_internal_call("internal_m2n_foot_ik_get_left_weight", script_internal_call(internal_m2n_foot_ik_get_left_weight));
        reg.add_internal_call("internal_m2n_foot_ik_get_right_weight", script_internal_call(internal_m2n_foot_ik_get_right_weight));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.SpringBoneComponent");
        reg.add_internal_call("internal_m2n_spring_bone_get_stiffness", script_internal_call(internal_m2n_spring_bone_get_stiffness));
        reg.add_internal_call("internal_m2n_spring_bone_set_stiffness", script_internal_call(internal_m2n_spring_bone_set_stiffness));
        reg.add_internal_call("internal_m2n_spring_bone_get_damping", script_internal_call(internal_m2n_spring_bone_get_damping));
        reg.add_internal_call("internal_m2n_spring_bone_set_damping", script_internal_call(internal_m2n_spring_bone_set_damping));
        reg.add_internal_call("internal_m2n_spring_bone_get_gravity", script_internal_call(internal_m2n_spring_bone_get_gravity));
        reg.add_internal_call("internal_m2n_spring_bone_set_gravity", script_internal_call(internal_m2n_spring_bone_set_gravity));
        reg.add_internal_call("internal_m2n_spring_bone_get_radius", script_internal_call(internal_m2n_spring_bone_get_radius));
        reg.add_internal_call("internal_m2n_spring_bone_set_radius", script_internal_call(internal_m2n_spring_bone_set_radius));
        reg.add_internal_call("internal_m2n_spring_bone_get_chain_length", script_internal_call(internal_m2n_spring_bone_get_chain_length));
        reg.add_internal_call("internal_m2n_spring_bone_set_chain_length", script_internal_call(internal_m2n_spring_bone_set_chain_length));
        reg.add_internal_call("internal_m2n_spring_bone_get_weight", script_internal_call(internal_m2n_spring_bone_get_weight));
        reg.add_internal_call("internal_m2n_spring_bone_set_weight", script_internal_call(internal_m2n_spring_bone_set_weight));
        reg.add_internal_call("internal_m2n_spring_bone_is_solved", script_internal_call(internal_m2n_spring_bone_is_solved));
        reg.add_internal_call("internal_m2n_spring_bone_get_collider_count", script_internal_call(internal_m2n_spring_bone_get_collider_count));
        reg.add_internal_call("internal_m2n_spring_bone_add_collider", script_internal_call(internal_m2n_spring_bone_add_collider));
        reg.add_internal_call("internal_m2n_spring_bone_clear_colliders", script_internal_call(internal_m2n_spring_bone_clear_colliders));
        reg.add_internal_call("internal_m2n_spring_bone_reset_simulation", script_internal_call(internal_m2n_spring_bone_reset_simulation));
    }
}

} // namespace unravel
