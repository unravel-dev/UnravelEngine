#pragma once

#include <engine/ecs/scene.h>
#include <engine/rendering/pipeline/passes/assao_pass.h>
#include <engine/rendering/pipeline/passes/auto_exposure_pass.h>
#include <engine/rendering/pipeline/passes/bloom_pass.h>
#include <engine/rendering/pipeline/passes/fog_pass.h>
#include <engine/rendering/ecs/components/gi_component.h>
#include <engine/rendering/pipeline/passes/fxaa_pass.h>
#include <engine/rendering/pipeline/passes/ssr_pass.h>
#include <engine/rendering/pipeline/passes/ssil_pass.h>
#include <engine/rendering/pipeline/passes/gtao_pass.h>
#include <engine/rendering/pipeline/passes/tonemapping_pass.h>
#include <engine/rendering/pipeline/passes/taa_pass.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <entt/entt.hpp>
#include <math/math.h>
#include <hpp/optional.hpp>

namespace unravel
{

/**
 * @struct resolved_post_process_settings
 * @brief Merged post-processing settings from all contributing volumes.
 */
struct resolved_post_process_settings
{
    bool has_auto_exposure = false;
    auto_exposure_pass::settings auto_exposure{};
    bool has_bloom = false;
    bloom_pass::settings bloom{};
    bool has_tonemapping = false;
    tonemapping_pass::settings tonemapping{};
    bool has_fxaa = false;
    bool has_taa = false;
    taa_pass::settings taa{};
    bool has_ssr = false;
    ssr_pass::ssr_settings ssr{};
    bool has_assao = false;
    assao_pass::settings assao{};
    bool has_ssil = false;
    ssil_pass::ssil_settings ssil{};
    bool has_gtao = false;
    gtao_pass::settings gtao{};
    bool has_gi = false;
    gi_settings gi{};
    bool has_fog = false;
    fog_pass::settings fog{};
};

/**
 * @brief One post-process material a camera run draws: the material whose program and textures it binds, its
 * parameter registers blended across the volumes that run it, and the weight it shows with.
 */
struct post_process_material_draw
{
    /// The key of the material asset the entries name: entries of one material blend into one draw.
    std::string material_key;
    std::shared_ptr<graph_material> material;
    std::vector<math::vec4> registers;
    float weight{};
    material_post_process_location location{material_post_process_location::after_tonemapping};
    int priority{};
    /// Where it came in the resolve (volumes by priority, each volume's list, then the camera's): the order among
    /// materials of one location and priority.
    uint32_t order{};
};

/**
 * @brief The post-process materials a camera at @p camera_pos runs: every contributing volume's entries, weighted by
 * its contribution, then @p camera_ent's own. Entries of one material blend into one draw: its weight moves toward each
 * later entry's weight by that entry's contribution, and its parameters with it, so a volume fades its materials in.
 * Sorted by Location, Priority, then that order; draws of no weight are left out.
 */
auto resolve_post_process_materials(scene& scn, const math::vec3& camera_pos, entt::handle camera_ent)
    -> std::vector<post_process_material_draw>;

/**
 * @brief Moves @p draw toward a later entry of its material, of @p weight and @p registers, by @p contribution: the
 * over-model the volumes' other settings blend by. Its weight and registers blend over; at a contribution of a half or
 * more the entry's @p material (its overrides resolved) becomes the draw's.
 */
void blend_post_process_material_draw(post_process_material_draw& draw,
                                      const std::shared_ptr<graph_material>& material,
                                      const std::vector<math::vec4>& registers,
                                      float weight,
                                      float contribution);

/// Drops the draws of no weight and sorts the others by Location, Priority, then the order they came in.
void finish_post_process_material_draws(std::vector<post_process_material_draw>& draws);

/**
 * @brief Resolves post-process volumes for a camera position.
 * @param scn The scene containing volume entities.
 * @param camera_pos The camera position in world space.
 * @param camera_ent The camera entity (used as fallback when no volumes contribute).
 * @return Merged settings from contributing volumes. If no volumes contribute,
 *         returns empty (caller should use camera entity components).
 */
auto resolve_post_process_volumes(scene& scn,
                                 const math::vec3& camera_pos,
                                 entt::handle camera_ent) -> resolved_post_process_settings;

} // namespace unravel
