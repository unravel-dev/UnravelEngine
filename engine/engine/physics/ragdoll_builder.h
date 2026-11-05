#pragma once
#include <engine/engine_export.h>

#include <engine/physics/ecs/components/ragdoll_component.h>

#include <string>
#include <vector>

namespace unravel
{
class model_component;

/**
 * @struct ragdoll_build_options
 * @brief What an automatic ragdoll setup makes.
 */
struct ragdoll_build_options
{
    /// Kilograms shared out over the bodies by the mass of each part of a human body. Must be above zero.
    float total_mass{70.0f};
    /// Fits the shapes to the skinned vertices; without it, sizes come from the bone lengths.
    bool fit_to_skin{true};
    bool include_hands{true};
    bool include_feet{true};
    /// A body for the neck when the skeleton has a neck bone of some length.
    bool include_neck{true};
};

/**
 * @struct ragdoll_build_result
 * @brief A ragdoll setup and how it came about.
 */
struct ragdoll_build_result
{
    bool ok{false};
    std::string error;
    std::vector<ragdoll_body> bodies;
    std::vector<ragdoll_bone_pair> disabled_collisions;
    /// Bodies whose shape was fitted to skinned vertices; the rest were sized from bone lengths.
    int skin_fitted_bodies{0};
};

/**
 * @brief Builds a ragdoll for a humanoid skinned model.
 *
 * The bones come from the model's humanoid roles (find_humanoid_bones and the mesh's role overrides), so any naming
 * works. Limbs get capsules from joint to joint whose radius fits the vertices that weigh mostly on the limb's bones,
 * as Unreal's physics asset tool fits them; the pelvis and the spine get capsules across the body, the head a capsule
 * standing up, the feet boxes on the soles. Joints take human ranges: hinges at the elbows and knees, cones of at most
 * 90 degrees elsewhere, and the hips flex through their twist so they can bend far forward and little back. Each
 * shoulder cone centres on the arm halfway between hanging and held out level (a swing offset from the bind
 * direction), so T-pose and A-pose rigs get the same cone and hanging arms lie inside it. Masses share @p total_mass
 * by body segment (Winter's anthropometric table). Bodies that overlap in the rest pose, and the two legs, never
 * collide with each other.
 *
 * Refuses, with ok false and the reason in error, rather than make bodies with no size or undefined numbers: a total
 * mass not above zero, a model without a humanoid skeleton (telling a mesh still loading from one without a
 * skeleton), a limb whose next bone sits at its start, a shape or joint frame that comes out degenerate.
 */
auto build_humanoid_ragdoll(const model_component& model, entt::handle model_entity, const ragdoll_build_options& options)
    -> ragdoll_build_result;

/**
 * @brief Gives @p ragdoll the result of build_humanoid_ragdoll for the model on its own entity.
 */
auto setup_ragdoll(ragdoll_component& ragdoll, const ragdoll_build_options& options = {}) -> ragdoll_build_result;

} // namespace unravel
