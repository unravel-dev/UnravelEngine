#pragma once

#include <engine/ecs/ecs.h>
#include <engine/layers/layer_mask.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/model.h>
#include <engine/rendering/batch_collector.h>
#include <engine/rendering/feature_limits.h>
#include <engine/rendering/render_feature_support.h>
#include <engine/rendering/particles/ps/particle_types.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>

#include <cstdint>


#include "passes/assao_pass.h"
#include "passes/gtao_pass.h"
#include "passes/atmospheric_pass_perez.h"
#include "passes/atmospheric_pass_skybox.h"
#include "passes/auto_exposure_pass.h"
#include "passes/blit_pass.h"
#include "passes/fog_pass.h"
#include "passes/fxaa_pass.h"
#include "passes/hiz_pass.h"
#include "passes/particle_half_res_pass.h"
#include "passes/prefilter_pass.h"
#include "passes/ssr_pass.h"
#include "passes/gi_clipmap_compose_pass.h"
#include "passes/temporal_probe_pass.h"
#include "passes/gi_gather_pass.h"
#include "passes/gi_reflection_pass.h"
#include "passes/gi_surface_cache_pass.h"
#include "passes/gi_visualize_pass.h"
#include "passes/scene_history_pass.h"
#include "passes/ssil_pass.h"
#include "passes/bloom_pass.h"
#include "passes/tonemapping_pass.h"
#include "passes/upscale_pass.h"
#include "passes/taa_pass.h"
#include "pre_exposure.h"
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
namespace ps_soa
{
struct render_batch;
} // namespace ps_soa

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
    /// Shadow maps rendered and reused this frame (shadow map caching): a spot or point light's map counts once, each
    /// cascade of the sun once.
    uint32_t shadow_maps_rendered = 0;
    uint32_t shadow_maps_reused = 0;
    /// Of the rendered maps, the spot and point lights' that started from their static copy: only the casters that
    /// move were drawn.
    uint32_t shadow_maps_restored = 0;
    /// The GI surface cache this frame (the camera that updated it): its cards, the pages it captured, the placements
    /// whose material changed, and the resident pages whose direct lighting is due.
    uint32_t gi_cards = 0;
    uint32_t gi_captures = 0;
    uint32_t gi_material_changes = 0;
    uint32_t gi_direct_dirty_pages = 0;
    /// The GI pages resident, and those this frame's lighting changes (a light, an occluder) marked for relighting.
    uint32_t gi_pages_used = 0;
    uint32_t gi_direct_invalidated = 0;
    /// Particle emitters: enabled in the scene, simulated this frame (the others are frozen by
    /// renderer-based culling), and drawn by this camera.
    uint32_t active_particle_emitters = 0;
    uint32_t simulated_particle_emitters = 0;
    uint32_t drawn_particle_emitters = 0;
    /// Live particles of those emitters; drawn_particles counts the instances submitted.
    uint32_t active_particles = 0;
    uint32_t simulated_particles = 0;
    uint32_t drawn_particles = 0;
    uint32_t drawn_particles_batches = 0;
    /// Emitters in the camera's frustum it did not draw, and their particles: beyond their max draw
    /// distance, smaller on screen than the project's min screen size, or past its particle budget.
    uint32_t skipped_particle_emitters = 0;
    uint32_t skipped_particles = 0;
    /// Particle sort keys this camera sorted (on the GPU, the sorted range rounded up to a power of two).
    uint32_t sorted_particle_keys = 0;

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
/// What lit particles read from the scene for one view (pipeline.cpp).
struct particle_scene_lighting;

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

        /// The user is editing the scene in this view (a gizmo or a property being dragged in the editor): GI
        /// shortens its histories and traces more of its cache.
        bool is_being_edited = false;

        /// Forces RGBA16F G/L/R buffers even when @c fill_hdr_params is unset.
        /// Reflection probe captures strip the post stack (including tonemapping)
        /// but must still light in HDR: their result feeds an RGBA16F cubemap and
        /// ultimately IBL, so an LDR-clamped capture is silently wrong.
        bool force_hdr_buffers = false;

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
        std::function<void(gtao_pass::run_params& params)> fill_gtao_params;
        /// Global illumination. Unset means off, exactly like the hooks above -- the feature runs only
        /// where a gi_component asks for it.
        std::function<void(gi_settings&)> fill_gi_params;
        /// Height fog (fog_pass). Unset means off, like the hooks above.
        std::function<void(fog_pass::run_params& params)> fill_fog_params;

        /// Camera runs: the fraction of the render resolution the scene renders at, from MIN_RENDER_SCALE to 1.
        /// The finished scene is resampled into the output, at the camera's viewport size.
        float render_scale = 1.0f;
        /// Camera runs: the size the scene renders at before the render scale; empty: the camera's viewport
        /// size. A view can render a resolution of its own this way while its viewport keeps the size it is
        /// shown at, which the screen helpers and the picking work in.
        usize32_t render_resolution{};
        /// Camera runs whose scene renders at another size than the output: also give the output a depth at
        /// its size (the OBUFFER_DEPTH frame buffer), for what draws over the finished image with the scene's
        /// depth, such as the editor's gizmos and grid. A run at the output's size always has it.
        bool needs_output_depth = false;

        /// The features the run may use. apply_feature_limits applies them to the hooks above; the shadow
        /// and lighting passes render every light as limit_light limits it.
        feature_limits limits;
    };

    pipeline() = default;
    virtual ~pipeline() = default;

    virtual auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Limits `params` to `limits`: the hooks of effects they do not allow are cleared, a view asking
     * for temporal AA under an FXAA limit gets FXAA, and `params.limits` is set for the passes that read it.
     */
    static void apply_feature_limits(run_params& params, const feature_limits& limits);

    /**
     * @brief Turns off in `params` the effects the device cannot run that no feature limit covers: GTAO (ASSAO
     * takes over when the device runs it), ASSAO and auto exposure (the camera keeps its fixed exposure).
     */
    static void apply_feature_support(run_params& params, const render_feature_support& support);

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

    /// The world-space GI visualizations to draw over the view, whatever the debug pass (the card placement).
    void set_gi_visualize_settings(const gi_visualize_pass::world_settings& settings)
    {
        gi_visualize_settings_ = settings;
    }

    /// Readback scale for the radiance-valued debug views: the indirect-diffuse view (tone
    /// mapped) takes it as an exposure factor ahead of the tone map.
    /// An instrument for the editor's debug-view tooling (viewport_set_debug_view "scale");
    /// 1 = the views as shipped. The lit path never reads it.
    virtual void set_debug_view_scale(float scale)
    {
        (void)scale;
    }

    /// The pre-exposure @p rview rendered its last frame with (1 when the pipeline has none).
    virtual auto get_pre_exposure(gfx::render_view& rview) const -> pre_exposure_state
    {
        (void)rview;
        return {};
    }

    /// The temporal-stability instrument (temporal_probe_pass): a tool arms it for a number of
    /// frames and reads the reduced statistics once the single readback lands. Nothing is
    /// dispatched while no measurement is armed. @p is_lowpass selects the probe's low-pass lane
    /// (temporal_probe_pass::request).
    void request_temporal_probe(uint32_t frames,
                                bool is_lowpass = false,
                                bool keeps_images = false,
                                std::vector<uint32_t> marks = {})
    {
        temporal_probe_pass_.request(frames, is_lowpass, keeps_images, std::move(marks));
    }
    auto get_temporal_probe() const -> const temporal_probe_pass&
    {
        return temporal_probe_pass_;
    }

    /**
     * @brief Draws the world-space UI documents and texts over the finished scene.
     * @param output The finished scene's color with its depth attached: the elements are drawn into the
     *        color and depth tested against the scene.
     */
    virtual void run_ui_pass(scene& scn,
                         const camera& camera,
                         gfx::render_view& rview,
                         const gfx::frame_buffer::ptr& output);

    /**
     * @brief Draws the particles over the lit scene.
     * @param hiz_depth This frame's Hi-Z buffer when the pipeline built one: soft particles read its
     *        first mip. Without it they read a copy of the depth buffer made for them.
     * @param use_shadow_maps The run built this camera's shadow maps: lit particles may read the sun's.
     * @param wants_reactive The view's TAA runs and reads the particles' reactive coverage; without it the
     *        particles write only the light buffer.
     * @param dt The frame's time step: the camera's velocity over it stretches the particles that stretch against
     *        it.
     * @param fog The fog the scene was composited with (fog_pass::run); the particles fog themselves with it.
     * @return The particles' TAA reactive coverage (R8: the largest coverage x taa_response over each pixel)
     *         when particles drew into it this frame; null otherwise.
     */
    virtual auto run_particle_pass(scene& scn,
                                   const camera& camera,
                                   gfx::render_view& rview,
                                   const gfx::frame_buffer::ptr& output,
                                   const pre_exposure_state& pre_exposure,
                                   const gfx::texture::ptr& hiz_depth,
                                   bool use_shadow_maps,
                                   bool wants_reactive,
                                   delta_t dt,
                                   const fog_pass::shader_params& fog,
                                   layer_mask render_mask) -> gfx::texture::ptr;

    /// Whether @p entity is on a layer @p render_mask draws (an entity without layers is drawn).
    static auto is_on_rendered_layer(const entt::registry& registry, entt::entity entity, const layer_mask& render_mask)
        -> bool;
    /// The share of the view's height @p bounds cover from @p eye (the particle budget's measure).
    static auto get_particle_screen_size(const camera& camera, const math::bbox& bounds, const math::vec3& eye) -> float;

    /// Binds what a lit particle batch reads: the sky, the sun and its shadow, the brightest nearby lights.
    void bind_particle_lighting(const particle_scene_lighting& lighting, const ps_soa::render_batch& batch);

    virtual auto create_run_params(entt::handle camera_ent) const -> rendering::pipeline::run_params;
    virtual auto create_run_params(entt::handle camera_ent, scene* scn, const camera* cam) const -> rendering::pipeline::run_params;

    auto get_stats() -> pipeline_stats&
    {
        return stats_;
    }

    /// The text the last frame's debug view puts on the image (the visualize labels: the overview tiles' names),
    /// for the viewport to draw over it.
    auto get_debug_view_labels() const -> const std::vector<debug_view_label>&
    {
        return debug_view_labels_;
    }

protected:
    prefilter_pass prefilter_pass_{};
    blit_pass blit_pass_{};
    upscale_pass upscale_pass_{};
    atmospheric_pass_perez atmospheric_pass_perez_{};
    atmospheric_pass_skybox atmospheric_pass_skybox_{};
    assao_pass assao_pass_{};
    auto_exposure_pass auto_exposure_pass_{};
    bloom_pass bloom_pass_{};
    fxaa_pass fxaa_pass_{};
    fog_pass fog_pass_{};
    taa_pass taa_pass_{};
    tonemapping_pass tonemapping_pass_{};
    ssr_pass ssr_pass_{};
    hiz_pass hiz_pass_{}; ///< Hi-Z buffer generation pass
    ssil_pass ssil_pass_{};
    gtao_pass gtao_pass_{};
    gi_clipmap_compose_pass gi_clipmap_compose_pass_{}; ///< The global distance field's GPU composition
    temporal_probe_pass temporal_probe_pass_{};
    gi_gather_pass gi_gather_pass_{}; ///< The GI screen probe gather: the indirect diffuse
    gi_surface_cache_pass gi_surface_cache_pass_{}; ///< The surface cache: cards, atlases and captures
    gi_reflection_pass gi_reflection_pass_{}; ///< The GI reflections, with the screen probe gather
    gi_visualize_pass gi_visualize_pass_{}; ///< The GI debug views
    shader_print shader_print_{};                 ///< The view's shader print text
    scene_history_pass scene_history_pass_{}; ///< PREV_SCENE_HDR, view depth in alpha
    particle_half_res_pass particle_half_res_pass_{}; ///< Half-resolution particles and their composite

    std::unique_ptr<gpu_program> particle_program_{};
    std::unique_ptr<gpu_program> particle_program_mask_{};
    /// Lit particles (particle_lighting.sh): their programs, the light they read per draw, and the stand-ins for a
    /// missing sky (zero SH) or sun shadow (far depth).
    std::unique_ptr<gpu_program> particle_program_lit_{};
    std::unique_ptr<gpu_program> particle_program_lit_mask_{};
    gfx::program::uniform_ptr particle_sun_direction_uniform_{};
    gfx::program::uniform_ptr particle_sun_color_uniform_{};
    gfx::program::uniform_ptr particle_lighting_uniform_{};
    gfx::program::uniform_ptr particle_lights_uniform_{};
    gfx::program::uniform_ptr particle_irradiance_sampler_{};
    gfx::program::uniform_ptr particle_normal_sampler_{};
    std::array<gfx::program::uniform_ptr, 4> particle_shadow_samplers_{};
    std::shared_ptr<gfx::texture> particle_no_sky_{};
    std::shared_ptr<gfx::texture> particle_no_shadow_{};
    /// Motion vectors of the particles that write them; u_prev_view_proj is last frame's view-projection.
    std::unique_ptr<gpu_program> particle_velocity_program_{};
    gfx::program::uniform_ptr particle_prev_view_proj_uniform_{};
    /// The particle overdraw debug view: when set, the particle pass also counts the layers each pixel shades
    /// into PARTICLE_OVERDRAW (R16F, cleared every frame), with its own program.
    bool is_particle_overdraw_view_{false};
    std::unique_ptr<gpu_program> particle_overdraw_program_{};
    /// Emitter handles of a view's particle draws (run_particle_pass scratch).
    std::vector<ps_soa::emitter_handle> particle_handles_scratch_{};
    /// u_pre_exposure of both particle programs: particles are scene lighting.
    gfx::program::uniform_ptr particle_pre_exposure_uniform_{};
    std::unique_ptr<gpu_program> world_quad_program_{};

    pipeline_stats stats_{};
    ///< See get_debug_view_labels.
    std::vector<debug_view_label> debug_view_labels_;
    ///< See set_gi_visualize_settings.
    gi_visualize_pass::world_settings gi_visualize_settings_{};


    entt::registry cache_registry_;
    entt::entity cache_entity_;

    /// gather_visible_models scratch, reused across calls: one result slot per packed position
    /// of the view's leading pool (see poolstl::for_each_entity_par), emitted in pool order once
    /// the dispatch is done.
    std::vector<lod_data> cull_lod_data_;
    std::vector<uint8_t> cull_visible_;
};
} // namespace rendering
} // namespace unravel
