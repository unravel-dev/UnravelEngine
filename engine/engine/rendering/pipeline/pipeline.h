#pragma once

#include <engine/ecs/ecs.h>
#include <engine/rendering/camera.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>

#include "passes/assao_pass.h"
#include "passes/atmospheric_pass.h"
#include "passes/atmospheric_pass_perez.h"
#include "passes/atmospheric_pass_skybox.h"
#include "passes/fxaa_pass.h"
#include "passes/tonemapping_pass.h"
#include "passes/blit_pass.h"
#include "passes/prefilter_pass.h"
#include "passes/ssr_pass.h"
#include "passes/hiz_pass.h"

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <map>
#include <memory>
#include <vector>
#include <hpp/small_vector.hpp>

namespace unravel
{
namespace rendering
{
/**
 * @struct lod_data
 * @brief Contains level of detail (LOD) data for an entity.
 */
struct lod_data
{
    std::uint32_t current_lod_index = 0; ///< Current LOD index.
    std::uint32_t target_lod_index = 0;  ///< Target LOD index.
    float current_time = 0.0f;           ///< Current time for LOD transition.
};

using lod_data_container = std::map<entt::handle, lod_data>;
using visibility_set_models_t = hpp::small_vector<entt::handle>;

/**
 * @struct per_camera_data
 * @brief Contains data specific to a camera, including LOD information.
 */
struct per_camera_data
{
    lod_data_container entity_lods; ///< Container for entity LOD data.
};

/**
 * @class rendering_pipeline
 * @brief Base class for different rendering paths in the ACE framework.
 */
class pipeline
{
public:
    using uptr = std::unique_ptr<pipeline>;
    using sptr = std::shared_ptr<pipeline>;
    using wptr = std::weak_ptr<pipeline>;

    /**
     * @enum visibility_query
     * @brief Flags for visibility queries.
     */
    enum visibility_query : uint32_t
    {
        not_specified = 1 << 0,        ///< No specific visibility query.
        is_dirty = 1 << 1,             ///< Query for dirty entities.
        is_static = 1 << 2,            ///< Query for static entities.
        is_shadow_caster = 1 << 3,     ///< Query for shadow casting entities.
        is_reflection_caster = 1 << 4, ///< Query for reflection casting entities.
    };

    using visibility_flags = uint32_t; ///< Type alias for visibility flags.

    struct run_params
    {
        visibility_flags vflags = visibility_query::not_specified;

        std::function<void(assao_pass::run_params& params)> fill_assao_params;
        std::function<void(tonemapping_pass::run_params& params)> fill_hdr_params;
        std::function<void(fxaa_pass::run_params& params)> fill_fxaa_params;
        std::function<void(ssr_pass::run_params& params)> fill_ssr_params;
    };

    pipeline() = default;
    virtual ~pipeline() = default;

    virtual auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Gathers visible models from the scene based on the given query.
     * @param scn The scene to gather models from.
     * @param camera The camera used for visibility determination.
     * @param query The visibility query flags.
     * @return A vector of handles to the visible models.
     */
    virtual auto gather_visible_models(scene& scn,
                                       const math::frustum* frustum,
                                       visibility_flags query = visibility_query::is_static) -> visibility_set_models_t;

    /**
     * @brief Renders the entire scene from the camera's perspective.
     * @param scn The scene to render.
     * @param camera The camera to render from.
     * @param storage The camera storage.
     * @param render_view The render view.
     * @param dt The delta time.
     * @param query The visibility query flags.
     * @return A shared pointer to the frame buffer containing the rendered scene.
     */
    virtual auto run_pipeline(scene& scn,
                              const camera& camera,
                              gfx::render_view& rview,
                              delta_t dt,
                              const run_params& params) -> gfx::frame_buffer::ptr = 0;

    /**
     * @brief Renders the entire scene from the camera's perspective to the specified output.
     * @param output The output frame buffer.
     * @param scn The scene to render.
     * @param camera The camera to render from.
     * @param storage The camera storage.
     * @param render_view The render view.
     * @param dt The delta time.
     * @param query The visibility query flags.
     */
    virtual void run_pipeline(const gfx::frame_buffer::ptr& output,
                              scene& scn,
                              const camera& camera,
                              gfx::render_view& rview,
                              delta_t dt,
                              const run_params& params) = 0;

    virtual void set_debug_pass(int pass) = 0;

    virtual void ui_pass(scene& scn, const camera& camera, gfx::render_view& rview, const gfx::frame_buffer::ptr& output);

    virtual auto create_run_params(entt::handle camera_ent) const -> rendering::pipeline::run_params;

protected:
    prefilter_pass prefilter_pass_{};
    blit_pass blit_pass_{};
    atmospheric_pass atmospheric_pass_{};
    atmospheric_pass_perez atmospheric_pass_perez_{};
    atmospheric_pass_skybox atmospheric_pass_skybox_{};
    assao_pass assao_pass_{};
    fxaa_pass fxaa_pass_{};
    tonemapping_pass tonemapping_pass_{};
    ssr_pass ssr_pass_{};
    hiz_pass hiz_pass_{};  ///< Hi-Z buffer generation pass
};
} // namespace rendering
} // namespace unravel
