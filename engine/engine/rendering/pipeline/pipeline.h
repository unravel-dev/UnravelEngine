#pragma once

#include <engine/ecs/ecs.h>
#include <engine/layers/layer_mask.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/model.h>
#include <engine/rendering/batch_collector.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>

#include <cstdint>


#include "passes/assao_pass.h"
#include "passes/atmospheric_pass_perez.h"
#include "passes/atmospheric_pass_skybox.h"
#include "passes/auto_exposure_pass.h"
#include "passes/blit_pass.h"
#include "passes/fxaa_pass.h"
#include "passes/hiz_pass.h"
#include "passes/prefilter_pass.h"
#include "passes/ssr_pass.h"
#include "passes/gi_cache_pass.h"
#include "passes/gi_resolve_pass.h"
#include "passes/ssil_pass.h"
#include "passes/gi_cache_pass.h"
#include "passes/gi_resolve_pass.h"
#include "passes/sdf_debug_pass.h"
#include "passes/bloom_pass.h"
#include "passes/tonemapping_pass.h"
#include "passes/taa_pass.h"
#include "volume_resolver.h"

#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/ecs/components/transform_component.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <hpp/small_vector.hpp>
#include <map>
#include <memory>
#include <vector>


namespace unravel
{
namespace rendering
{
struct visibility_data
{
    entt::handle entity;
    unravel::lod_data lod_data;
};

using visibility_set_models_t = hpp::small_vector<visibility_data>;

struct pipeline_stats
{
    uint32_t drawn_models = 0;
    uint32_t drawn_static_submeshes = 0;
    uint32_t drawn_skinned_models = 0;
    uint32_t drawn_skinned_submeshes = 0;
    uint32_t drawn_models_for_shadows = 0;
    uint32_t drawn_submeshes_for_shadows = 0;
    uint32_t drawn_skinned_models_for_shadows = 0;
    uint32_t drawn_skinned_submeshes_for_shadows = 0;

    uint32_t drawn_lights = 0;
    uint32_t drawn_lights_casting_shadows = 0;
    uint32_t drawn_particles = 0;
    uint32_t drawn_particles_batches = 0;
    
    // Static mesh batching statistics
    batch_stats batching_stats;

    auto anything_drawn() const -> bool
    {
        return drawn_models > 0 || drawn_static_submeshes > 0 || drawn_skinned_models > 0 || drawn_skinned_submeshes > 0 ||
               drawn_models_for_shadows > 0 || drawn_submeshes_for_shadows > 0 || drawn_skinned_models_for_shadows > 0 ||
               drawn_skinned_submeshes_for_shadows > 0 || drawn_lights > 0 || drawn_lights_casting_shadows > 0 || drawn_particles > 0 ||
               drawn_particles_batches > 0;
    }
    
    /**
     * @brief Add batch statistics to the pipeline stats
     * @param stats Batch statistics to accumulate
     */
    void add_batch_stats(const batch_stats& stats);
    void add_stats(const pipeline_stats& stats);
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
    };

    using visibility_flags = uint32_t; ///< Type alias for visibility flags.
    using pipeline_flags = uint32_t;

    /**
     * @brief Describes which high-level pipeline path is executing.
     * Pass enablement is controlled separately via @c run_params::pflags.
     */
    enum class pipeline_run_type : std::uint8_t
    {
        /// Main camera / viewport rendering (post-processing, UI, probe build, etc.).
        camera = 0,
        /// Single cubemap face capture for reflection probe baking.
        reflection_probe_capture = 1,
    };

    struct run_params
    {
        visibility_flags vflags = visibility_query::not_specified;

        pipeline_run_type run_type = pipeline_run_type::camera;

        /// Deferred pipeline only: bitmask of enabled passes (@c deferred::pipeline_steps).
        pipeline_flags pflags = 0xFFFFFFFFu;

        std::function<void(assao_pass::run_params& params)> fill_assao_params;
        std::function<void(auto_exposure_pass::run_params& params)> fill_auto_exposure_params;
        std::function<void(bloom_pass::run_params& params)> fill_bloom_params;
        std::function<void(tonemapping_pass::run_params& params)> fill_hdr_params;
        std::function<void(fxaa_pass::run_params& params)> fill_fxaa_params;
        std::function<void(taa_pass::run_params& params)> fill_taa_params;
        /// When set with @c fill_taa_params, applies jitter / temporal AA state to the camera (typically @c camera::set_aa_data).
        /// When TAA is off, leave empty; the deferred path resets AA on the camera.
        std::function<void(camera&, const usize32_t& viewport_size)> apply_taa_params;
        std::function<void(ssr_pass::run_params& params)> fill_ssr_params;
        std::function<void(ssil_pass::run_params& params)> fill_ssil_params;
        /// Surface cache GI. Both halves travel together because they are one feature: the cache
        /// pass populates the world-space entries and the resolve pass gathers them, so settings
        /// resolved from different sources would describe two different configurations.
        /// Surface cache GI. Unset means off, exactly like the hooks above -- the feature runs only
        /// where a gi_component asks for it.
        std::function<void(gi_cache_pass::settings&, gi_resolve_pass::settings&)> fill_gi_params;
    };

    pipeline() = default;
    virtual ~pipeline() = default;

    virtual auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Gathers visible models from the scene based on the given query.
     * @param scn The scene to gather models from.
     * @param cam The camera used for visibility determination.
     * @param query The visibility query flags.
     * @param render_mask Render mask to filter entities by layers.
     * @param dt The delta time.
     * @param lod_data_callback The callback to call for each visible model.
     * @param lod_reference_cam Optional camera used only for LOD selection when @p cam is null
     *        (e.g. shadow gathering, which must not frustum-cull but benefits from
     *        distance-appropriate LODs). The shadow LOD bias is applied on top.
     * @return A vector of handles to the visible models.
     */
    virtual void gather_visible_models(scene& scn,
                                       const camera* cam,
                                       visibility_flags query,
                                       const layer_mask& render_mask,
                                       delta_t dt,
                                       const std::function<void(entt::handle entity, const lod_data& lod_data)>& lod_data_callback,
                                       const camera* lod_reference_cam = nullptr);

    /**
     * @brief LOD bias applied on top of the main selection when rendering shadows.
     * 0 = shadows use the same LOD the main view would pick; positive values push shadow
     * rendering toward coarser LODs. Replaces the old behavior of always using LOD 0.
     */
    static auto get_shadow_lod_bias() -> float;
    static void set_shadow_lod_bias(float bias);

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
                              const run_params& params,
                              layer_mask render_mask = layer_mask{
                                  layer_reserved::everything_layer}) -> gfx::frame_buffer::ptr = 0;

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
                              const run_params& params,
                              layer_mask render_mask = layer_mask{layer_reserved::everything_layer}) = 0;

    virtual void set_debug_pass(int pass) = 0;

    virtual void run_ui_pass(scene& scn,
                         const camera& camera,
                         gfx::render_view& rview,
                         const gfx::frame_buffer::ptr& output);

    virtual void run_particle_pass(scene& scn,
                               const camera& camera,
                               gfx::render_view& rview,
                               const gfx::frame_buffer::ptr& output);

    virtual auto create_run_params(entt::handle camera_ent) const -> rendering::pipeline::run_params;
    virtual auto create_run_params(entt::handle camera_ent, scene* scn, const camera* cam) const -> rendering::pipeline::run_params;

    auto get_stats() -> pipeline_stats&
    {
        return stats_;
    }

protected:
    prefilter_pass prefilter_pass_{};
    blit_pass blit_pass_{};
    atmospheric_pass_perez atmospheric_pass_perez_{};
    atmospheric_pass_skybox atmospheric_pass_skybox_{};
    assao_pass assao_pass_{};
    auto_exposure_pass auto_exposure_pass_{};
    bloom_pass bloom_pass_{};
    fxaa_pass fxaa_pass_{};
    taa_pass taa_pass_{};
    tonemapping_pass tonemapping_pass_{};
    ssr_pass ssr_pass_{};
    hiz_pass hiz_pass_{}; ///< Hi-Z buffer generation pass
    ssil_pass ssil_pass_{};
    gi_cache_pass gi_cache_pass_{};
    gi_resolve_pass gi_resolve_pass_{};
    sdf_debug_pass sdf_debug_pass_{}; ///< Diagnostic only; see sdf_debug_pass.h

    std::unique_ptr<gpu_program> particle_program_instanced_{};
    std::unique_ptr<gpu_program> particle_program_instanced_mask_{};
    std::unique_ptr<gpu_program> world_quad_program_{};

    pipeline_stats stats_{};


    entt::registry cache_registry_;
    entt::entity cache_entity_;
};
} // namespace rendering
} // namespace unravel
