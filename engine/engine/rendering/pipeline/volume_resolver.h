#pragma once

#include <engine/ecs/scene.h>
#include <engine/rendering/pipeline/passes/assao_pass.h>
#include <engine/rendering/pipeline/passes/bloom_pass.h>
#include <engine/rendering/pipeline/passes/fxaa_pass.h>
#include <engine/rendering/pipeline/passes/ssr_pass.h>
#include <engine/rendering/pipeline/passes/tonemapping_pass.h>
#include <entt/entt.hpp>
#include <math/math.h>
#include <optional>

namespace unravel
{

/**
 * @struct resolved_post_process_settings
 * @brief Merged post-processing settings from all contributing volumes.
 */
struct resolved_post_process_settings
{
    bool has_bloom = false;
    bloom_pass::settings bloom{};
    bool has_tonemapping = false;
    tonemapping_pass::settings tonemapping{};
    bool has_fxaa = false;
    bool has_ssr = false;
    ssr_pass::ssr_settings ssr{};
    bool has_assao = false;
    assao_pass::settings assao{};
};

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
