#include "ik_components.hpp"

#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/layers/layer_mask.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

namespace unravel
{
namespace
{
/**
 * @brief Saves an entity reference as a link, the way script Entity fields are saved: it is
 * remapped to the copy when the hierarchy is duplicated or instantiated, and an external target in
 * a prefab is dropped rather than turned into a stub entity.
 */
template<typename Archive>
void save_entity_reference(Archive& ar, const char* name, const entt::handle& handle)
{
    // Always written, empty when the save names no registry: binary archives are positional.
    ser20::const_entity_handle_link link;
    if(const auto* registry = get_save_context().get_registry())
    {
        link.handle = entt::const_handle(*registry, handle ? handle.entity() : entt::entity(entt::null));
    }
    try_save(ar, ser20::make_nvp(name, link));
}

template<typename Archive>
void load_entity_reference(Archive& ar, const char* name, entt::handle& handle)
{
    ser20::entity_handle_link link;
    if(try_load(ar, ser20::make_nvp(name, link)))
    {
        handle = link.handle;
    }
}
} // namespace

REFLECT(chain_ik_solver)
{
    entt::meta_factory<chain_ik_solver>{}
        .type("chain_ik_solver"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "chain_ik_solver"},
            entt::attribute{"pretty_name", "Chain IK Solver"},
        })
        .data<chain_ik_solver::fabrik>("fabrik"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fabrik"},
            entt::attribute{"pretty_name", "FABRIK"},
        })
        .data<chain_ik_solver::ccd>("ccd"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ccd"},
            entt::attribute{"pretty_name", "CCD"},
        });
}

REFLECT(spring_bone_collider)
{
    entt::meta_factory<spring_bone_collider>{}
        .type("spring_bone_collider"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spring_bone_collider"},
            entt::attribute{"pretty_name", "Spring Bone Collider"},
        })
        .data<&spring_bone_collider::entity>("entity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "entity"},
            entt::attribute{"pretty_name", "Entity"},
            entt::attribute{"tooltip", "Entity (usually a bone) the sphere follows."},
        })
        .data<&spring_bone_collider::radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radius"},
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"min", 0.0f},
        })
        .data<&spring_bone_collider::offset>("offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "offset"},
            entt::attribute{"pretty_name", "Offset"},
            entt::attribute{"tooltip", "The sphere's center from the entity, along the entity's axes, in world units."},
        });
}

SAVE(spring_bone_collider)
{
    save_entity_reference(ar, "entity", obj.entity);
    try_save(ar, ser20::make_nvp("radius", obj.radius));
    try_save(ar, ser20::make_nvp("offset", obj.offset));
}
SAVE_INSTANTIATE(spring_bone_collider, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(spring_bone_collider, ser20::oarchive_binary_t);

LOAD(spring_bone_collider)
{
    load_entity_reference(ar, "entity", obj.entity);
    try_load(ar, ser20::make_nvp("radius", obj.radius));
    try_load(ar, ser20::make_nvp("offset", obj.offset));
}
LOAD_INSTANTIATE(spring_bone_collider, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(spring_bone_collider, ser20::iarchive_binary_t);

REFLECT(two_bone_ik_component)
{
    entt::meta_factory<two_bone_ik_component>{}
        .type("two_bone_ik_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "two_bone_ik_component"},
            entt::attribute{"category", "Animation/IK"},
            entt::attribute{"pretty_name", "Two Bone IK"},
            entt::attribute{"tooltip", "Analytical two-bone IK on this bone (a hand or a foot): it reaches the target, bending its parent toward the pole. Runs after scripts, before skinning, and is undone before the next frame animates."},
        })
        .func<&component_meta<two_bone_ik_component>::exists>("component_exists"_hs)
        .func<&component_meta<two_bone_ik_component>::add>("component_add"_hs)
        .func<&component_meta<two_bone_ik_component>::save>("component_save"_hs)
        .func<&component_meta<two_bone_ik_component>::load>("component_load"_hs)
        .func<&component_meta<two_bone_ik_component>::remove>("component_remove"_hs)
        .data<&two_bone_ik_component::target>("target"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target"},
            entt::attribute{"pretty_name", "Target"},
            entt::attribute{"tooltip", "Entity this bone reaches for. Its rotation is the goal of Rotation Weight."},
        })
        .data<&two_bone_ik_component::pole>("pole"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pole"},
            entt::attribute{"pretty_name", "Pole"},
            entt::attribute{"tooltip", "Entity the middle joint (elbow, knee) bends toward. Empty keeps the animated bend."},
        })
        .data<&two_bone_ik_component::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"tooltip", "Blend against the animated pose."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&two_bone_ik_component::rotation_weight>("rotation_weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rotation_weight"},
            entt::attribute{"pretty_name", "Rotation Weight"},
            entt::attribute{"tooltip", "How much this bone also takes the target's rotation."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&two_bone_ik_component::soften>("soften"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "soften"},
            entt::attribute{"pretty_name", "Soften"},
            entt::attribute{"tooltip", "Fraction of limb length over which reach eases out before the joint locks straight. Costs reach."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&two_bone_ik_component::smoothing>("smoothing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "smoothing"},
            entt::attribute{"pretty_name", "Smoothing"},
            entt::attribute{"tooltip", "Time constant the goal and the weight ease with, in seconds. 0 follows the target exactly. Above it a moving, swapped or lost target does not snap the limb."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
        })
        .data<&two_bone_ik_component::weight_curve>("weight_curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight_curve"},
            entt::attribute{"pretty_name", "Weight Curve"},
            entt::attribute{"tooltip", "Optional animation curve the weight is multiplied by (0 while no playing clip has it)."},
        });
}

SAVE(two_bone_ik_component)
{
    save_entity_reference(ar, "target", obj.target);
    save_entity_reference(ar, "pole", obj.pole);
    try_save(ar, ser20::make_nvp("weight", obj.weight));
    try_save(ar, ser20::make_nvp("rotation_weight", obj.rotation_weight));
    try_save(ar, ser20::make_nvp("soften", obj.soften));
    try_save(ar, ser20::make_nvp("weight_curve", obj.weight_curve));
    try_save(ar, ser20::make_nvp("smoothing", obj.smoothing));
}
SAVE_INSTANTIATE(two_bone_ik_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(two_bone_ik_component, ser20::oarchive_binary_t);

LOAD(two_bone_ik_component)
{
    load_entity_reference(ar, "target", obj.target);
    load_entity_reference(ar, "pole", obj.pole);
    try_load(ar, ser20::make_nvp("weight", obj.weight));
    try_load(ar, ser20::make_nvp("rotation_weight", obj.rotation_weight));
    try_load(ar, ser20::make_nvp("soften", obj.soften));
    try_load(ar, ser20::make_nvp("weight_curve", obj.weight_curve));
    try_load(ar, ser20::make_nvp("smoothing", obj.smoothing));
}
LOAD_INSTANTIATE(two_bone_ik_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(two_bone_ik_component, ser20::iarchive_binary_t);

REFLECT(chain_ik_component)
{
    entt::meta_factory<chain_ik_component>{}
        .type("chain_ik_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "chain_ik_component"},
            entt::attribute{"category", "Animation/IK"},
            entt::attribute{"pretty_name", "Chain IK"},
            entt::attribute{"tooltip", "Iterative IK over a chain ending at this bone (tails, tentacles, spines, robot arms)."},
        })
        .func<&component_meta<chain_ik_component>::exists>("component_exists"_hs)
        .func<&component_meta<chain_ik_component>::add>("component_add"_hs)
        .func<&component_meta<chain_ik_component>::save>("component_save"_hs)
        .func<&component_meta<chain_ik_component>::load>("component_load"_hs)
        .func<&component_meta<chain_ik_component>::remove>("component_remove"_hs)
        .data<&chain_ik_component::target>("target"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target"},
            entt::attribute{"pretty_name", "Target"},
            entt::attribute{"tooltip", "Entity the chain tip reaches for."},
        })
        .data<&chain_ik_component::pole>("pole"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pole"},
            entt::attribute{"pretty_name", "Pole"},
            entt::attribute{"tooltip", "Entity the chain bends toward. Empty keeps the solver's bend."},
        })
        .data<&chain_ik_component::chain_length>("chain_length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "chain_length"},
            entt::attribute{"pretty_name", "Chain Length"},
            entt::attribute{"tooltip", "Bones in the chain, counted up from this bone."},
            entt::attribute{"min", 1},
            entt::attribute{"max", 32},
        })
        .data<&chain_ik_component::solver>("solver"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "solver"},
            entt::attribute{"pretty_name", "Solver"},
            entt::attribute{"tooltip", "FABRIK: smooth, fast to converge. CCD: bends the joints nearest the tip first."},
        })
        .data<&chain_ik_component::iterations>("iterations"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "iterations"},
            entt::attribute{"pretty_name", "Iterations"},
            entt::attribute{"tooltip", "Upper bound on solver iterations."},
            entt::attribute{"min", 1},
            entt::attribute{"max", 100},
        })
        .data<&chain_ik_component::tolerance>("tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tolerance"},
            entt::attribute{"pretty_name", "Tolerance"},
            entt::attribute{"tooltip", "Distance at which the solve stops early."},
            entt::attribute{"min", 0.0f},
        })
        .data<&chain_ik_component::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"tooltip", "Blend against the animated pose."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&chain_ik_component::rotation_weight>("rotation_weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rotation_weight"},
            entt::attribute{"pretty_name", "Rotation Weight"},
            entt::attribute{"tooltip", "How much the tip also takes the target's rotation."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&chain_ik_component::smoothing>("smoothing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "smoothing"},
            entt::attribute{"pretty_name", "Smoothing"},
            entt::attribute{"tooltip", "Time constant the goal and the weight ease with, in seconds. 0 follows the target exactly. Above it a moving, swapped or lost target does not snap the limb."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
        })
        .data<&chain_ik_component::weight_curve>("weight_curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight_curve"},
            entt::attribute{"pretty_name", "Weight Curve"},
            entt::attribute{"tooltip", "Optional animation curve the weight is multiplied by."},
        });
}

SAVE(chain_ik_component)
{
    save_entity_reference(ar, "target", obj.target);
    save_entity_reference(ar, "pole", obj.pole);
    try_save(ar, ser20::make_nvp("chain_length", obj.chain_length));
    try_save(ar, ser20::make_nvp("solver", obj.solver));
    try_save(ar, ser20::make_nvp("iterations", obj.iterations));
    try_save(ar, ser20::make_nvp("tolerance", obj.tolerance));
    try_save(ar, ser20::make_nvp("weight", obj.weight));
    try_save(ar, ser20::make_nvp("rotation_weight", obj.rotation_weight));
    try_save(ar, ser20::make_nvp("weight_curve", obj.weight_curve));
    try_save(ar, ser20::make_nvp("smoothing", obj.smoothing));
}
SAVE_INSTANTIATE(chain_ik_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(chain_ik_component, ser20::oarchive_binary_t);

LOAD(chain_ik_component)
{
    load_entity_reference(ar, "target", obj.target);
    load_entity_reference(ar, "pole", obj.pole);
    try_load(ar, ser20::make_nvp("chain_length", obj.chain_length));
    try_load(ar, ser20::make_nvp("solver", obj.solver));
    try_load(ar, ser20::make_nvp("iterations", obj.iterations));
    try_load(ar, ser20::make_nvp("tolerance", obj.tolerance));
    try_load(ar, ser20::make_nvp("weight", obj.weight));
    try_load(ar, ser20::make_nvp("rotation_weight", obj.rotation_weight));
    try_load(ar, ser20::make_nvp("weight_curve", obj.weight_curve));
    try_load(ar, ser20::make_nvp("smoothing", obj.smoothing));
}
LOAD_INSTANTIATE(chain_ik_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(chain_ik_component, ser20::iarchive_binary_t);

REFLECT(look_at_ik_component)
{
    entt::meta_factory<look_at_ik_component>{}
        .type("look_at_ik_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "look_at_ik_component"},
            entt::attribute{"category", "Animation/IK"},
            entt::attribute{"pretty_name", "Look At IK"},
            entt::attribute{"tooltip", "Turns this bone and its parents (head, neck, chest) toward a target, the lower bones taking a smaller share, each limited to a cone. The turn is smoothed, and fades out when the target goes behind."},
        })
        .func<&component_meta<look_at_ik_component>::exists>("component_exists"_hs)
        .func<&component_meta<look_at_ik_component>::add>("component_add"_hs)
        .func<&component_meta<look_at_ik_component>::save>("component_save"_hs)
        .func<&component_meta<look_at_ik_component>::load>("component_load"_hs)
        .func<&component_meta<look_at_ik_component>::remove>("component_remove"_hs)
        .data<&look_at_ik_component::target>("target"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target"},
            entt::attribute{"pretty_name", "Target"},
            entt::attribute{"tooltip", "Entity to look at."},
        })
        .data<&look_at_ik_component::target_offset>("target_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target_offset"},
            entt::attribute{"pretty_name", "Target Offset"},
            entt::attribute{"tooltip", "World-space offset added to the target position."},
        })
        .data<&look_at_ik_component::chain_length>("chain_length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "chain_length"},
            entt::attribute{"pretty_name", "Chain Length"},
            entt::attribute{"tooltip", "Bones turned, counted up from this bone."},
            entt::attribute{"min", 1},
            entt::attribute{"max", 8},
        })
        .data<&look_at_ik_component::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"tooltip", "Blend against the animated pose."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&look_at_ik_component::root_share>("root_share"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "root_share"},
            entt::attribute{"pretty_name", "Root Share"},
            entt::attribute{"tooltip", "Share of the turn the lowest bone takes. The tip always completes the rest."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&look_at_ik_component::max_bone_angle>("max_bone_angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_bone_angle"},
            entt::attribute{"pretty_name", "Max Bone Angle"},
            entt::attribute{"tooltip", "Largest turn of each bone away from its animated facing, in degrees. 0 holds the bones."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 180.0f},
        })
        .data<&look_at_ik_component::max_turn_angle>("max_turn_angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_turn_angle"},
            entt::attribute{"pretty_name", "Max Turn Angle"},
            entt::attribute{"tooltip", "Targets further than this from the character's forward fade the look out, in degrees."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 180.0f},
        })
        .data<&look_at_ik_component::smoothing>("smoothing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "smoothing"},
            entt::attribute{"pretty_name", "Smoothing"},
            entt::attribute{"tooltip", "Time constant of the target and weight smoothing, in seconds."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
        })
        .data<&look_at_ik_component::forward_axis>("forward_axis"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "forward_axis"},
            entt::attribute{"pretty_name", "Forward Axis"},
            entt::attribute{"tooltip", "Bone-local axis the bones look along. Zero resolves it from the character's forward."},
        })
        .data<&look_at_ik_component::weight_curve>("weight_curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight_curve"},
            entt::attribute{"pretty_name", "Weight Curve"},
            entt::attribute{"tooltip", "Optional animation curve the weight is multiplied by."},
        });
}

SAVE(look_at_ik_component)
{
    save_entity_reference(ar, "target", obj.target);
    try_save(ar, ser20::make_nvp("target_offset", obj.target_offset));
    try_save(ar, ser20::make_nvp("chain_length", obj.chain_length));
    try_save(ar, ser20::make_nvp("weight", obj.weight));
    try_save(ar, ser20::make_nvp("root_share", obj.root_share));
    try_save(ar, ser20::make_nvp("max_bone_angle", obj.max_bone_angle));
    try_save(ar, ser20::make_nvp("max_turn_angle", obj.max_turn_angle));
    try_save(ar, ser20::make_nvp("smoothing", obj.smoothing));
    try_save(ar, ser20::make_nvp("forward_axis", obj.forward_axis));
    try_save(ar, ser20::make_nvp("weight_curve", obj.weight_curve));
}
SAVE_INSTANTIATE(look_at_ik_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(look_at_ik_component, ser20::oarchive_binary_t);

LOAD(look_at_ik_component)
{
    load_entity_reference(ar, "target", obj.target);
    try_load(ar, ser20::make_nvp("target_offset", obj.target_offset));
    try_load(ar, ser20::make_nvp("chain_length", obj.chain_length));
    try_load(ar, ser20::make_nvp("weight", obj.weight));
    try_load(ar, ser20::make_nvp("root_share", obj.root_share));
    try_load(ar, ser20::make_nvp("max_bone_angle", obj.max_bone_angle));
    try_load(ar, ser20::make_nvp("max_turn_angle", obj.max_turn_angle));
    try_load(ar, ser20::make_nvp("smoothing", obj.smoothing));
    try_load(ar, ser20::make_nvp("forward_axis", obj.forward_axis));
    try_load(ar, ser20::make_nvp("weight_curve", obj.weight_curve));
}
LOAD_INSTANTIATE(look_at_ik_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(look_at_ik_component, ser20::iarchive_binary_t);

REFLECT(foot_ik_component)
{
    entt::meta_factory<foot_ik_component>{}
        .type("foot_ik_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "foot_ik_component"},
            entt::attribute{"category", "Animation/IK"},
            entt::attribute{"pretty_name", "Foot IK"},
            entt::attribute{"tooltip", "Plants the feet on uneven ground: probes under each foot, lowers the pelvis to the lowest planted foot, solves each leg and tilts the feet onto the slope. Sits on the character root (feet level). It needs physics, so it runs in play mode."},
        })
        .func<&component_meta<foot_ik_component>::exists>("component_exists"_hs)
        .func<&component_meta<foot_ik_component>::add>("component_add"_hs)
        .func<&component_meta<foot_ik_component>::save>("component_save"_hs)
        .func<&component_meta<foot_ik_component>::load>("component_load"_hs)
        .func<&component_meta<foot_ik_component>::remove>("component_remove"_hs)
        .data<&foot_ik_component::left_foot>("left_foot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_foot"},
            entt::attribute{"pretty_name", "Left Foot"},
            entt::attribute{"tooltip", "Left foot (ankle) bone."},
        })
        .data<&foot_ik_component::right_foot>("right_foot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_foot"},
            entt::attribute{"pretty_name", "Right Foot"},
            entt::attribute{"tooltip", "Right foot (ankle) bone."},
        })
        .data<&foot_ik_component::pelvis>("pelvis"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pelvis"},
            entt::attribute{"pretty_name", "Pelvis"},
            entt::attribute{"tooltip", "Pelvis / hips bone that drops to reach the lower foot."},
        })
        .data<&foot_ik_component::ground_layers>("ground_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ground_layers"},
            entt::attribute{"pretty_name", "Ground Layers"},
            entt::attribute{"tooltip", "Physics layers the ground probes hit."},
        })
        .data<&foot_ik_component::probe_up>("probe_up"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_up"},
            entt::attribute{"pretty_name", "Probe Up"},
            entt::attribute{"tooltip", "Probe start above the animated foot, in meters."},
            entt::attribute{"min", 0.0f},
        })
        .data<&foot_ik_component::probe_down>("probe_down"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_down"},
            entt::attribute{"pretty_name", "Probe Down"},
            entt::attribute{"tooltip", "Probe reach below the animated foot, in meters."},
            entt::attribute{"min", 0.0f},
        })
        .data<&foot_ik_component::lift_start>("lift_start"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lift_start"},
            entt::attribute{"pretty_name", "Lift Start"},
            entt::attribute{"tooltip", "Foot height above the root where the IK starts fading (swing)."},
            entt::attribute{"min", 0.0f},
        })
        .data<&foot_ik_component::lift_end>("lift_end"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lift_end"},
            entt::attribute{"pretty_name", "Lift End"},
            entt::attribute{"tooltip", "Foot height above the root where the IK is gone."},
            entt::attribute{"min", 0.0f},
        })
        .data<&foot_ik_component::drop_start>("drop_start"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "drop_start"},
            entt::attribute{"pretty_name", "Drop Start"},
            entt::attribute{"tooltip", "Ground this far below a foot starts fading its IK (ledges)."},
            entt::attribute{"min", 0.0f},
        })
        .data<&foot_ik_component::drop_end>("drop_end"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "drop_end"},
            entt::attribute{"pretty_name", "Drop End"},
            entt::attribute{"tooltip", "Ground this far below a foot removes its IK."},
            entt::attribute{"min", 0.0f},
        })
        .data<&foot_ik_component::max_pelvis_drop>("max_pelvis_drop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_pelvis_drop"},
            entt::attribute{"pretty_name", "Max Pelvis Drop"},
            entt::attribute{"tooltip", "The pelvis drops at most this far."},
            entt::attribute{"min", 0.0f},
        })
        .data<&foot_ik_component::smoothing>("smoothing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "smoothing"},
            entt::attribute{"pretty_name", "Smoothing"},
            entt::attribute{"tooltip", "Time constant of the pelvis and foot weight smoothing, in seconds."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&foot_ik_component::align_to_ground>("align_to_ground"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "align_to_ground"},
            entt::attribute{"pretty_name", "Align To Ground"},
            entt::attribute{"tooltip", "How much the feet tilt onto the slope."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&foot_ik_component::knee_forward>("knee_forward"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "knee_forward"},
            entt::attribute{"pretty_name", "Knee Forward"},
            entt::attribute{"tooltip", "For legs the animation holds straight: knee pole distance in front of the pelvis, in meters. Bent legs keep their animated bend."},
            entt::attribute{"min", 0.0f},
        })
        .data<&foot_ik_component::knee_outward>("knee_outward"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "knee_outward"},
            entt::attribute{"pretty_name", "Knee Outward"},
            entt::attribute{"tooltip", "For legs the animation holds straight: knee pole offset out to each side, in meters."},
        })
        .data<&foot_ik_component::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"tooltip", "Blend against the animated pose."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&foot_ik_component::weight_curve>("weight_curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight_curve"},
            entt::attribute{"pretty_name", "Weight Curve"},
            entt::attribute{"tooltip", "Optional animation curve the weight is multiplied by."},
        });
}

SAVE(foot_ik_component)
{
    save_entity_reference(ar, "left_foot", obj.left_foot);
    save_entity_reference(ar, "right_foot", obj.right_foot);
    save_entity_reference(ar, "pelvis", obj.pelvis);
    try_save(ar, ser20::make_nvp("ground_layers", obj.ground_layers));
    try_save(ar, ser20::make_nvp("probe_up", obj.probe_up));
    try_save(ar, ser20::make_nvp("probe_down", obj.probe_down));
    try_save(ar, ser20::make_nvp("lift_start", obj.lift_start));
    try_save(ar, ser20::make_nvp("lift_end", obj.lift_end));
    try_save(ar, ser20::make_nvp("drop_start", obj.drop_start));
    try_save(ar, ser20::make_nvp("drop_end", obj.drop_end));
    try_save(ar, ser20::make_nvp("max_pelvis_drop", obj.max_pelvis_drop));
    try_save(ar, ser20::make_nvp("smoothing", obj.smoothing));
    try_save(ar, ser20::make_nvp("align_to_ground", obj.align_to_ground));
    try_save(ar, ser20::make_nvp("knee_forward", obj.knee_forward));
    try_save(ar, ser20::make_nvp("knee_outward", obj.knee_outward));
    try_save(ar, ser20::make_nvp("weight", obj.weight));
    try_save(ar, ser20::make_nvp("weight_curve", obj.weight_curve));
}
SAVE_INSTANTIATE(foot_ik_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(foot_ik_component, ser20::oarchive_binary_t);

LOAD(foot_ik_component)
{
    load_entity_reference(ar, "left_foot", obj.left_foot);
    load_entity_reference(ar, "right_foot", obj.right_foot);
    load_entity_reference(ar, "pelvis", obj.pelvis);
    try_load(ar, ser20::make_nvp("ground_layers", obj.ground_layers));
    try_load(ar, ser20::make_nvp("probe_up", obj.probe_up));
    try_load(ar, ser20::make_nvp("probe_down", obj.probe_down));
    try_load(ar, ser20::make_nvp("lift_start", obj.lift_start));
    try_load(ar, ser20::make_nvp("lift_end", obj.lift_end));
    try_load(ar, ser20::make_nvp("drop_start", obj.drop_start));
    try_load(ar, ser20::make_nvp("drop_end", obj.drop_end));
    try_load(ar, ser20::make_nvp("max_pelvis_drop", obj.max_pelvis_drop));
    try_load(ar, ser20::make_nvp("smoothing", obj.smoothing));
    try_load(ar, ser20::make_nvp("align_to_ground", obj.align_to_ground));
    try_load(ar, ser20::make_nvp("knee_forward", obj.knee_forward));
    try_load(ar, ser20::make_nvp("knee_outward", obj.knee_outward));
    try_load(ar, ser20::make_nvp("weight", obj.weight));
    try_load(ar, ser20::make_nvp("weight_curve", obj.weight_curve));
}
LOAD_INSTANTIATE(foot_ik_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(foot_ik_component, ser20::iarchive_binary_t);

REFLECT(spring_bone_component)
{
    entt::meta_factory<spring_bone_component>{}
        .type("spring_bone_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spring_bone_component"},
            entt::attribute{"category", "Animation/IK"},
            entt::attribute{"pretty_name", "Spring Bone"},
            entt::attribute{"tooltip", "Secondary motion for the chain hanging from this bone (tails, ears, ponytails, straps): damped joints pulled back to the animated pose, pushed by gravity, kept out of colliders."},
        })
        .func<&component_meta<spring_bone_component>::exists>("component_exists"_hs)
        .func<&component_meta<spring_bone_component>::add>("component_add"_hs)
        .func<&component_meta<spring_bone_component>::save>("component_save"_hs)
        .func<&component_meta<spring_bone_component>::load>("component_load"_hs)
        .func<&component_meta<spring_bone_component>::remove>("component_remove"_hs)
        .data<&spring_bone_component::stiffness>("stiffness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stiffness"},
            entt::attribute{"pretty_name", "Stiffness"},
            entt::attribute{"tooltip", "Pull back toward the animated direction, per second."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 20.0f},
        })
        .data<&spring_bone_component::damping>("damping"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "damping"},
            entt::attribute{"pretty_name", "Damping"},
            entt::attribute{"tooltip", "Fraction of velocity lost per simulation step."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&spring_bone_component::gravity>("gravity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gravity"},
            entt::attribute{"pretty_name", "Gravity"},
            entt::attribute{"tooltip", "World-space acceleration applied to the joints."},
        })
        .data<&spring_bone_component::radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radius"},
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"tooltip", "Joint radius used against colliders."},
            entt::attribute{"min", 0.0f},
        })
        .data<&spring_bone_component::chain_length>("chain_length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "chain_length"},
            entt::attribute{"pretty_name", "Chain Length"},
            entt::attribute{"tooltip", "Bones the spring turns: this one and those after it (the first child at each level)."},
            entt::attribute{"min", 1},
            entt::attribute{"max", 64},
        })
        .data<&spring_bone_component::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"tooltip", "Blend against the animated pose."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&spring_bone_component::colliders>("colliders"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "colliders"},
            entt::attribute{"pretty_name", "Colliders"},
            entt::attribute{"tooltip", "Spheres the chain stays out of, attached to entities (usually bones)."},
        });
}

SAVE(spring_bone_component)
{
    try_save(ar, ser20::make_nvp("stiffness", obj.stiffness));
    try_save(ar, ser20::make_nvp("damping", obj.damping));
    try_save(ar, ser20::make_nvp("gravity", obj.gravity));
    try_save(ar, ser20::make_nvp("radius", obj.radius));
    try_save(ar, ser20::make_nvp("chain_length", obj.chain_length));
    try_save(ar, ser20::make_nvp("weight", obj.weight));
    try_save(ar, ser20::make_nvp("colliders", obj.colliders));
}
SAVE_INSTANTIATE(spring_bone_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(spring_bone_component, ser20::oarchive_binary_t);

LOAD(spring_bone_component)
{
    try_load(ar, ser20::make_nvp("stiffness", obj.stiffness));
    try_load(ar, ser20::make_nvp("damping", obj.damping));
    try_load(ar, ser20::make_nvp("gravity", obj.gravity));
    try_load(ar, ser20::make_nvp("radius", obj.radius));
    try_load(ar, ser20::make_nvp("chain_length", obj.chain_length));
    try_load(ar, ser20::make_nvp("weight", obj.weight));
    try_load(ar, ser20::make_nvp("colliders", obj.colliders));
}
LOAD_INSTANTIATE(spring_bone_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(spring_bone_component, ser20::iarchive_binary_t);

} // namespace unravel
