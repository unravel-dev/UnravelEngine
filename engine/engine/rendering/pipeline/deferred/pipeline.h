#pragma once
#include "../pipeline.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/light.h>
#include <engine/rendering/shadow.h>
#include <engine/rendering/batch_collector.h>

#include <graphics/utils/font/font_manager.h>
#include <graphics/utils/font/text_buffer_manager.h>
#include <graphics/utils/font/text_metrics.h>
#include <graphics/utils/bgfx_utils.h>
#include <graphics/utils/entry/entry.h>

namespace unravel
{
class surface_cache_system;
class surface_cache_view;

namespace rendering
{

class deferred : public pipeline
{
public:
    deferred();
    ~deferred();

    auto init(rtti::context& ctx) -> bool override;
    auto deinit(rtti::context& ctx) -> bool;

    auto run_pipeline(scene& scn, const camera& camera, gfx::render_view& rview, delta_t dt, const run_params& params, layer_mask render_mask = layer_mask{layer_reserved::everything_layer})
        -> gfx::frame_buffer::ptr override;

    void run_pipeline(const gfx::frame_buffer::ptr& output,
                      scene& scn,
                      const camera& camera,
                      gfx::render_view& rview,
                      delta_t dt,
                      const run_params& params,
                      layer_mask render_mask = layer_mask{layer_reserved::everything_layer}) override;
    void set_debug_pass(int pass) override;
    void set_debug_view_scale(float scale) override;
    auto get_pre_exposure(gfx::render_view& rview) const -> pre_exposure_state override;

    /// Bitmask for @c pipeline::run_params::pflags (deferred path only).
    enum pipeline_steps : uint32_t
    {
        geometry_pass = 1u << 1,
        shadow_pass = 1u << 2,
        reflection_probe = 1u << 3,
        atmospheric = 1u << 5,
        particles_pass = 1u << 6,
        /// Velocity (motion vector) buffer production - UNCONDITIONAL for camera runs (a
        /// standing frame resource, like depth). Camera runs use the default full mask so
        /// this is on by default; probe captures build pflags from 0 and never set it, and
        /// clearing the bit is the opt-out for custom callers.
        velocity_pass = 1u << 7,

        full = 0xFFFFFFFFu,
    };

    void run_pipeline_impl(const gfx::frame_buffer::ptr& output,
                           scene& scn,
                           const camera& camera,
                           gfx::render_view& rview,
                           delta_t dt,
                           const run_params& params,
                           layer_mask render_mask = layer_mask{layer_reserved::everything_layer});

    /// This run's visible models, which the G-buffer and velocity passes rasterize. Empty without the
    /// geometry step, which both passes then render nothing for.
    auto collect_visible_models(scene& scn,
                                const camera& camera,
                                const run_params& rparams,
                                layer_mask render_mask,
                                delta_t dt) -> visibility_set_models_t;

    /// Copies @p input into @p output, the pipeline's hand-off to a caller-owned target.
    void blit_to_output(gfx::render_view& rview,
                        const gfx::frame_buffer::ptr& input,
                        const gfx::frame_buffer::ptr& output);

    void run_g_buffer_pass(const visibility_set_models_t& visibility_set,
                           const camera& camera,
                           gfx::render_view& rview,
                           delta_t dt);

    /// Produces the per-pixel velocity (motion vector) buffer ("VELOCITY", RG16F, uv delta
    /// uv_curr - uv_prev): a fullscreen camera-motion pass reconstructed from depth, then the
    /// movers (has_motion models) drawn over it with true per-object motion, depth-tested
    /// EQUAL against the shared G-buffer depth. Removes the buffer when inactive.
    void run_velocity_pass(const visibility_set_models_t& visibility_set,
                           const camera& camera,
                           gfx::render_view& rview);

    /// Renders the VELOCITY buffer visualization (debug view @c debug_pass_velocity):
    /// hue = direction, brightness = magnitude, magenta = NaN.
    void run_velocity_debug_pass(const camera& camera,
                                 gfx::render_view& rview,
                                 const gfx::frame_buffer::ptr& output);

    void run_assao_pass(const camera& camera,
                        gfx::render_view& rview,
                        delta_t dt,
                        const run_params& rparams);

    auto run_direct_lighting_pass(scene& scn, const camera& camera, gfx::render_view& rview, bool apply_shadows, delta_t dt)
        -> gfx::frame_buffer::ptr;

    /// The sky's irradiance SH (run_irradiance_pass): its texture and the flat ambient colour.
    struct irradiance_pass_result
    {
        gfx::texture::ptr irradiance_tex;
        math::vec3 global_color = {1.0f, 1.0f, 1.0f};
        float global_intensity = 0.0f;
    };
    auto run_indirect_lighting_pass(const camera& camera,
                                    gfx::render_view& rview,
                                    const irradiance_pass_result& irradiance,
                                    bool apply_reflection,
                                    delta_t dt) -> gfx::frame_buffer::ptr;

    void run_reflection_probe_pass(scene& scn, const camera& camera, gfx::render_view& rview, bool apply_probes, delta_t dt);

    /// Clears RBUFFER to no traced radiance (the whole pixel left to the probe layer), for a view whose traced
    /// reflections do not write every pixel this frame.
    void clear_traced_reflections(gfx::render_view& rview);

    auto run_atmospherics_pass(gfx::frame_buffer::ptr input,
                               scene& scn,
                               const camera& camera,
                               gfx::render_view& rview,
                               delta_t dt) -> gfx::frame_buffer::ptr;

    /// Renders the cloud shadow map for this run into cloud_shadow_ (before the lighting passes).
    void run_cloud_shadow_pass(scene& scn, const camera& camera, gfx::render_view& rview);

    void run_ssr_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams);

    /// @p gi_active: the GI published this frame's indirect diffuse, which the indirect pass takes over SSIL's.
    void run_ssil_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams, bool gi_active);
    /// The screen-space AO of the frame, right after the G-buffer: GTAO when its volume is
    /// enabled, otherwise ASSAO - never both.
    void run_screen_ao_pass(const camera& camera, gfx::render_view& rview, delta_t dt, const run_params& rparams);
    /// Ground Truth Ambient Occlusion into the "GTAO" texture. @return true when it ran.
    auto run_gtao_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams) -> bool;

    auto run_taa_pass(const camera& camera,
                      gfx::render_view& rview,
                      const gfx::frame_buffer::ptr& input,
                      const gfx::frame_buffer::ptr& output,
                      const run_params& rparams) -> gfx::frame_buffer::ptr;

    auto run_fxaa_pass(gfx::render_view& rview, const gfx::frame_buffer::ptr& input, const gfx::frame_buffer::ptr& output,
                       const run_params& rparams)
        -> gfx::frame_buffer::ptr;

    void run_auto_exposure_pass(gfx::render_view& rview,
                                const camera& camera,
                                const gfx::frame_buffer::ptr& input,
                                const run_params& rparams,
                                delta_t dt);

    auto run_bloom_pass(gfx::render_view& rview,
                        const gfx::frame_buffer::ptr& input,
                        const run_params& rparams) -> gfx::frame_buffer::ptr;

    /// @p scene_before_bloom is the scene @p input was composited from by the bloom (the input
    /// itself without bloom): local exposure measures and scales it, bloom rides on top.
    auto run_tonemapping_pass(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& input,
                              const gfx::frame_buffer::ptr& scene_before_bloom,
                              const gfx::frame_buffer::ptr& output,
                              const run_params& rparams) -> gfx::frame_buffer::ptr;
    /// The lit image's tone mapping operator, which the debug views that read like the frame go through (none
    /// without HDR).
    static auto get_debug_tonemapping(const run_params& rparams) -> tonemapping_method;
    /// The G-buffer visualiser's views; the indirect diffuse view goes through the lit image's tone mapping
    /// operator (get_debug_tonemapping), so it reads like the lit frame.
    void run_debug_visualization_pass(const camera& camera,
                                      gfx::render_view& rview,
                                      const gfx::frame_buffer::ptr& output,
                                      const run_params& rparams);
    /// The GI debug views (lumen_visualize_pass), over the finished image; their labels into debug_view_labels_.
    void run_lumen_visualize_pass(const camera& camera,
                                  gfx::render_view& rview,
                                  const gfx::frame_buffer::ptr& output,
                                  const run_params& rparams);
    /// The GI overlays (lumen_visualize_pass::draw_overlays: lines and shader_print text) over the finished image,
    /// whatever the debug view.
    void run_lumen_visualize_overlays(const camera& camera,
                                      gfx::render_view& rview,
                                      const gfx::frame_buffer::ptr& output,
                                      const run_params& rparams);
    /// The world-space GI visualizations (lumen_visualize_pass::draw_world), into the scene colour like
    /// translucency: ahead of the exposure and the tone map. Draws nothing unless one of them is selected.
    void run_lumen_visualize_scene_color(const camera& camera,
                                         gfx::render_view& rview,
                                         const pre_exposure_state& pre_exposure);
    /// Frees what the card generation view holds while it is not the selected one.
    void release_lumen_visualize_card_generation();
    /// The selected debug view over the finished image, and the GI overlays on top of whichever one it is.
    void run_debug_passes(const camera& camera,
                          gfx::render_view& rview,
                          const gfx::frame_buffer::ptr& output,
                          const run_params& rparams);

    /// Debug pass ids below this one are the G-buffer visualizer shader's own modes; every view with a larger id is
    /// dispatched by an exact match.
    static constexpr int debug_pass_gbuffer_modes = 15;
    /// Velocity buffer visualization. Selecting it forces velocity production for camera runs even when no other
    /// consumer (TAA) is active.
    static constexpr int debug_pass_velocity = 29;
    /// The screen-space AO bent normal, through the G-buffer visualization program.
    static constexpr int debug_pass_ao_bent_normals = 31;
    /// Auto exposure's own state, drawn as a blended panel OVER the finished image
    /// (fs_exposure_debug.sc): an overlay, not a replacement image.
    static constexpr int debug_pass_exposure = 41;
    /// The GI debug views, ids debug_pass_lumen_scene + lumen_visualize_pass::view (lumen_visualize_pass): the
    /// scene, lighting and update views, the physical card atlas, the card coverage, the object grid, the screen-space
    /// views and the two overviews.
    static constexpr int debug_pass_lumen_scene = 42;
    static constexpr int debug_pass_lumen_card_atlas = 43;
    static constexpr int debug_pass_lumen_card_coverage = 44;
    static constexpr int debug_pass_lumen_scene_albedo = 45;
    static constexpr int debug_pass_lumen_surface_cache = 46;
    static constexpr int debug_pass_lumen_object_grid = 47;
    static constexpr int debug_pass_lumen_scene_direct = 48;
    static constexpr int debug_pass_lumen_scene_indirect = 49;
    static constexpr int debug_pass_lumen_reflection_rays = 50;
    static constexpr int debug_pass_lumen_reflection_view = 51;
    static constexpr int debug_pass_lumen_geometry_normals = 52;
    static constexpr int debug_pass_lumen_scene_normals = 53;
    static constexpr int debug_pass_lumen_scene_emissive = 54;
    static constexpr int debug_pass_lumen_card_weights = 55;
    static constexpr int debug_pass_lumen_direct_lighting_updates = 56;
    static constexpr int debug_pass_lumen_indirect_lighting_updates = 57;
    static constexpr int debug_pass_lumen_radiosity_frames = 58;
    static constexpr int debug_pass_lumen_screen_probe_frames = 59;
    static constexpr int debug_pass_lumen_overview = 60;
    static constexpr int debug_pass_lumen_performance_overview = 61;
    void run_exposure_debug_pass(gfx::render_view& rview,
                                 const gfx::frame_buffer::ptr& output,
                                 const run_params& rparams);
    /// The surface cache for this frame under the view's @p scene_settings: card placement and resolution,
    /// captures rasterized with the G-buffer program (one orthographic view per page), copied into the physical
    /// atlases, then lit (direct lighting and the final combine).
    void run_lumen_surface_cache(const camera& camera,
                                 gfx::render_view& rview,
                                 surface_cache_system& gi_scene,
                                 const gi_settings::scene_settings& scene_settings);
    /// Rasterizes this frame's card captures, one orthographic view per page.
    void capture_lumen_cards(const camera& camera, const surface_cache_system& gi_scene);

    /// Resolves the blended gi_settings for this run from the volume hooks; false = GI off.
    auto resolve_gi_settings(const run_params& rparams, gi_settings& gi) -> bool;

    /// The GI scene for a camera run that asks for GI: instance residency (shared by every camera, refreshed once per
    /// frame) and this view's global distance field, snapped around the camera and composed on the GPU.
    void run_gi_scene_passes(scene& scn, const camera& camera, gfx::render_view& rview, const run_params& params);

    /// Whether the GI reflections own this view's reflection buffers: a camera run with the probe stack and
    /// float buffers and GI with its reflections enabled. SSR and the reflection probes then step aside (no
    /// other specular composites under the GI's).
    auto lumen_reflections_own_view(const run_params& rparams) -> bool;
    /// True when the GI's short-range AO is enabled in this view: it replaces the screen-space AO at any intensity
    /// (the screen-space AO never stacks on the GI's).
    auto lumen_short_range_ao_owns_view(const run_params& rparams) -> bool;

    /// The GI reflections into RBUFFER and PBUFFER after the screen probe gather of @p gather_params; true when they
    /// wrote both, every pixel.
    auto run_lumen_reflection_pass(gfx::render_view& rview, const lumen_run_params& gather_params) -> bool;

    /// GI for a camera run that asks for it: the surface cache, the screen probe gather (published as
    /// GI_RESOLVE, its rough specular as GI_ROUGH_SPECULAR) and the reflections.
    /// @return true when the gather produced a result, which also means it needs PREV_DEPTH
    ///         snapshotted this frame for its temporal accumulation.
    auto run_lumen_gi_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams) -> bool;

    /// The render view's scale of GI_RESOLVE and GI_ROUGH_SPECULAR (the gather's histories): the GI intensity.
    static constexpr const char* gi_resolve_scale = "GI_RESOLVE_SCALE";
    static auto get_gi_resolve_scale(gfx::render_view& rview) -> float;

    /// This view's inputs to the GI passes under @p gi.
    auto make_lumen_run_params(const camera& camera, gfx::render_view& rview, const gi_settings& gi)
        -> lumen_run_params;

    void build_reflections(scene& scn, const camera& camera, delta_t dt);

    void build_shadows(scene& scn, const camera& camera, delta_t dt, layer_mask render_mask = layer_mask{layer_reserved::everything_layer});

    /// Builds or drops Hi-Z + related depth history. @c true if SSIL/SSR may use HiZ this frame.
    auto run_hiz_pass(const camera& camera,
                      gfx::render_view& rview,
                      const run_params& params,
                      const usize32_t& viewport_size,
                      delta_t dt) -> bool;

private:
    struct ref_probe_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_data0, "u_data0", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_data1, "u_data1", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_capture, "u_capture", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_tex[0], "s_tex0", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[1], "s_tex1", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[2], "s_tex2", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[3], "s_tex3", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[4], "s_tex4", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex_cube, "s_tex_cube", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr u_data0;
        gfx::program::uniform_ptr u_data1;
        gfx::program::uniform_ptr u_capture;

        std::array<gfx::program::uniform_ptr, 5> s_tex;
        gfx::program::uniform_ptr s_tex_cube;

        std::unique_ptr<gpu_program> program;
    };

    struct box_ref_probe_program : ref_probe_program
    {
        void cache_uniforms()
        {
            ref_probe_program::cache_uniforms();

            cache_uniform(program.get(), u_data2, "u_data2", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_inv_world, "u_inv_world", bgfx::UniformType::Mat4);
        }
        gfx::program::uniform_ptr u_inv_world;
        gfx::program::uniform_ptr u_data2;

    } box_ref_probe_program_;

    struct sphere_ref_probe_program : ref_probe_program
    {
    } sphere_ref_probe_program_;

    struct geom_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_tex_color, "s_tex_color", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex_normal, "s_tex_normal", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex_roughness, "s_tex_roughness", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex_metalness, "s_tex_metalness", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex_ao, "s_tex_ao", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex_emissive, "s_tex_emissive", bgfx::UniformType::Sampler);

            cache_uniform(program.get(), u_base_color, "u_base_color", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_subsurface_color, "u_subsurface_color", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_emissive_color, "u_emissive_color", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_surface_data, "u_surface_data", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_tiling, "u_tiling", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_dither_threshold, "u_dither_threshold", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_surface_data2, "u_surface_data2", bgfx::UniformType::Vec4);

            cache_uniform(program.get(), u_camera_wpos, "u_camera_wpos", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_camera_clip_planes, "u_camera_clip_planes", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_lod_params, "u_lod_params", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr s_tex_color;
        gfx::program::uniform_ptr s_tex_normal;
        gfx::program::uniform_ptr s_tex_roughness;
        gfx::program::uniform_ptr s_tex_metalness;
        gfx::program::uniform_ptr s_tex_ao;
        gfx::program::uniform_ptr s_tex_emissive;

        gfx::program::uniform_ptr u_base_color;
        gfx::program::uniform_ptr u_subsurface_color;
        gfx::program::uniform_ptr u_emissive_color;
        gfx::program::uniform_ptr u_surface_data;
        gfx::program::uniform_ptr u_tiling;
        gfx::program::uniform_ptr u_dither_threshold;
        gfx::program::uniform_ptr u_surface_data2;

        gfx::program::uniform_ptr u_camera_wpos;
        gfx::program::uniform_ptr u_camera_clip_planes;
        gfx::program::uniform_ptr u_lod_params;

        std::unique_ptr<gpu_program> program;
    };

    /// The geometry program of the surface cache card captures: each draw brings its own world -> clip transform, so
    /// one pass draws every capture into its tile of the capture atlas (vs_deferred_geom_card_capture.sc).
    struct card_capture_program : geom_program
    {
        void cache_uniforms()
        {
            geom_program::cache_uniforms();
            cache_uniform(program.get(), u_card_capture_view_proj, "u_card_capture_view_proj", bgfx::UniformType::Mat4);
        }

        gfx::program::uniform_ptr u_card_capture_view_proj;
    };

    geom_program geom_program_;
    geom_program geom_program_skinned_;
    geom_program geom_program_instanced_;
    card_capture_program card_capture_program_;

    struct velocity_geom_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_prev_view_proj, "u_prev_view_proj", bgfx::UniformType::Mat4);
        }

        gfx::program::uniform_ptr u_prev_view_proj;

        std::unique_ptr<gpu_program> program;
    };

    velocity_geom_program velocity_program_;
    velocity_geom_program velocity_program_skinned_;
    velocity_geom_program velocity_program_instanced_;

    struct velocity_camera_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_depth, "s_depth", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_prev_view_proj, "u_prev_view_proj", bgfx::UniformType::Mat4);
        }

        gfx::program::uniform_ptr s_depth;
        gfx::program::uniform_ptr u_prev_view_proj;

        std::unique_ptr<gpu_program> program;
    } velocity_camera_program_;

    struct velocity_debug_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_velocity, "s_velocity", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_params, "u_params", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr s_velocity;
        gfx::program::uniform_ptr u_params;

        std::unique_ptr<gpu_program> program;
    } velocity_debug_program_;

    /// The exposure overlay (exposure/fs_exposure_debug.sc): the adaptation trace and this
    /// frame's metering histogram, drawn blended over the lit image.
    struct exposure_debug_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_exposure, "s_exposure", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_exposure_history, "s_exposure_history", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_exposure_histogram, "s_exposure_histogram", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_exposure_debug_rect, "u_exposure_debug_rect", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_exposure_debug_range, "u_exposure_debug_range", bgfx::UniformType::Vec4);
            cache_uniform(program.get(),
                          u_exposure_debug_settings,
                          "u_exposure_debug_settings",
                          bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr s_exposure;
        gfx::program::uniform_ptr s_exposure_history;
        gfx::program::uniform_ptr s_exposure_histogram;
        gfx::program::uniform_ptr u_exposure_debug_rect;
        gfx::program::uniform_ptr u_exposure_debug_range;
        gfx::program::uniform_ptr u_exposure_debug_settings;

        std::unique_ptr<gpu_program> program;
    } exposure_debug_program_;

    struct color_lighting : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_light_position, "u_light_position", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_light_direction, "u_light_direction", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_light_data, "u_light_data", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_light_source, "u_light_source", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_contact_shadow, "u_contact_shadow", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_light_color_intensity, "u_light_color_intensity", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_camera_position, "u_camera_position", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_cloudShadow, "u_cloudShadow", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_cloudShadow2, "u_cloudShadow2", bgfx::UniformType::Vec4);

            cache_uniform(program.get(), s_tex[0], "s_tex0", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[1], "s_tex1", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[2], "s_tex2", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[3], "s_tex3", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[4], "s_tex4", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[5], "s_tex5", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[6], "s_tex6", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_cloudShadow, "s_cloudShadow", bgfx::UniformType::Sampler);
        }
        gfx::program::uniform_ptr u_light_position;
        gfx::program::uniform_ptr u_light_direction;
        gfx::program::uniform_ptr u_light_data;
        /// Point and spot lights' emitter: xyz = the tube's axis scaled by its length, w = the sphere radius.
        gfx::program::uniform_ptr u_light_source;
        gfx::program::uniform_ptr u_contact_shadow;
        gfx::program::uniform_ptr u_light_color_intensity;
        gfx::program::uniform_ptr u_camera_position;
        gfx::program::uniform_ptr u_cloudShadow;
        gfx::program::uniform_ptr u_cloudShadow2;
        std::array<gfx::program::uniform_ptr, 7> s_tex;
        gfx::program::uniform_ptr s_cloudShadow;

        std::shared_ptr<gpu_program> program;
    };

    struct irradiance_compute_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_mode, "u_mode", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_irradiance_tint_intensity, "u_irradiance_tint_intensity", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sun_direction, "u_sun_direction", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sky_luminance_xyz, "u_sky_luminance_xyz", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_exposition, "u_exposition", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_perez_coeff, "u_perez_coeff", bgfx::UniformType::Vec4, 5);
            cache_uniform(program.get(), s_env, "s_env", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_cloudShadow, "s_cloudShadow", bgfx::UniformType::Sampler);
        }
        gfx::program::uniform_ptr u_mode;
        gfx::program::uniform_ptr u_irradiance_tint_intensity;
        gfx::program::uniform_ptr u_sun_direction;
        gfx::program::uniform_ptr u_sky_luminance_xyz;
        gfx::program::uniform_ptr u_exposition;
        gfx::program::uniform_ptr u_perez_coeff;
        gfx::program::uniform_ptr s_env;
        gfx::program::uniform_ptr s_cloudShadow;

        std::unique_ptr<gpu_program> program;
    } irradiance_compute_program_;

    /// Cloud shadow map of the current run (rendered before the lighting passes, consumed by
    /// the directional light and the irradiance bake).
    atmospheric_pass_perez::cloud_shadow_result cloud_shadow_{};

    struct indirect_lighting_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_light_data, "u_light_data", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_camera_position, "u_camera_position", bgfx::UniformType::Vec4);

            cache_uniform(program.get(), s_tex[0], "s_tex0", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[1], "s_tex1", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[2], "s_tex2", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[3], "s_tex3", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[4], "s_tex4", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[5], "s_tex5", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[6], "s_tex6", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_irradiance, "s_irradiance", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_ssil, "s_ssil", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_screen_ao, "s_screen_ao", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_probe_layer, "s_probe_layer", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_specular_occlusion, "s_specular_occlusion", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_screen_ao, "u_screen_ao", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_indirect_params, "u_indirect_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_probe_layer_params, "u_probe_layer_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
        }
        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr u_light_data;
        gfx::program::uniform_ptr u_camera_position;
        std::array<gfx::program::uniform_ptr, 7> s_tex;
        gfx::program::uniform_ptr s_irradiance;
        gfx::program::uniform_ptr s_ssil;
        /// Screen-space AO texture and parameters (get_screen_ao_inputs).
        gfx::program::uniform_ptr s_screen_ao;
        gfx::program::uniform_ptr u_screen_ao;
        /// The untraced reflection layer (get_probe_layer_inputs); s_tex[5] is RBUFFER, the traced layers.
        gfx::program::uniform_ptr s_probe_layer;
        /// What s_probe_layer holds (probe_layer_inputs::params).
        gfx::program::uniform_ptr u_probe_layer_params;
        /// The GTSO table (default_textures::specular_occlusion).
        gfx::program::uniform_ptr s_specular_occlusion;
        /// x = 1 when a real GI resolve / SSIL texture feeds s_ssil, 0 when the transparent
        /// fallback does; the shader then takes the resolve outright instead of mixing the
        /// environment SH back in (fs_pbr_lighting.sh, pbr_indirect). y = 1 when that source is
        /// SSIL, which resolved the screen-space visibility per pixel and takes no screen AO.
        gfx::program::uniform_ptr u_indirect_params;

        std::unique_ptr<gpu_program> program;

    } indirect_lighting_program_;

    struct debug_visualization_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_params, "u_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_tex[0], "s_tex0", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[1], "s_tex1", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[2], "s_tex2", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[3], "s_tex3", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[4], "s_tex4", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[5], "s_tex5", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[6], "s_tex6", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[7], "s_tex7", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[8], "s_tex8", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[9], "s_tex9", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tex[10], "s_tex10", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_screen_ao, "u_screen_ao", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_visualize_indirect, "u_visualize_indirect", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_probe_layer_params, "u_probe_layer_params", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr u_params;
        /// What s_tex[9] holds (probe_layer_inputs::params).
        gfx::program::uniform_ptr u_probe_layer_params;
        /// Screen-space AO parameters for the occlusion views (get_screen_ao_inputs).
        gfx::program::uniform_ptr u_screen_ao;
        /// The indirect diffuse view: x = tone mapping operator, y = multi-bounce albedo cap, z = 1 when the
        /// indirect diffuse is SSIL's.
        gfx::program::uniform_ptr u_visualize_indirect;
        /// 0-4 G-buffer, 5 RBUFFER, 6 environment SH, 7 GI / SSIL, 8 screen-space AO,
        /// 9 the untraced reflection layer (get_probe_layer_inputs), 10 the GTSO table.
        std::array<gfx::program::uniform_ptr, 11> s_tex;

        std::unique_ptr<gpu_program> program;

    } debug_visualization_program_;

    auto run_irradiance_pass(scene& scn, gfx::render_view& rview) -> irradiance_pass_result;

    auto get_light_program(const light& l) const -> const color_lighting&;
    auto get_light_program_no_shadows(const light& l) const -> const color_lighting&;
    void submit_pbr_material(geom_program& program, const pbr_material& mat);
    void submit_batched_geometry(gfx::render_pass& pass, const camera& camera);
    /// Velocity for batched movers: draws the mover instances of this frame's prepared
    /// batches (instance stream doubled with the previous world matrix) into the velocity
    /// target, depth-tested EQUAL. Batches without movers cost nothing.
    void submit_batched_velocity(gfx::render_pass& pass, const math::transform& prev_vp);

    color_lighting color_lighting_[uint8_t(light_type::count)][uint8_t(sm_depth::count)][uint8_t(sm_impl::count)];
    color_lighting color_lighting_no_shadow_[uint8_t(light_type::count)];

    asset_handle<gfx::texture> ibl_brdf_lut_;

    // Static mesh batching system
    batch_collector batch_collector_;

    /// Every shadow caster of the scene, split by how often it has to be refreshed. Both are
    /// rebuilt WHOLESALE (clear + refill) by refresh_shadow_casters, so a destroyed entity
    /// can never survive in them and there is nothing to prune.
    /// Static casters: membership AND bounds only change on a shadow_caster_revision bump.
    shadow::shadow_map_models_t shadow_static_casters_;
    /// Movers: membership only changes on a revision bump, but their bounds and LOD are
    /// re-read every frame from the cached handles - which is a walk over a handful of
    /// entries instead of over every model in the scene.
    shadow::shadow_map_models_t shadow_dynamic_casters_;
    /// The scene's shadow_caster_revision both lists were built from, and the layer mask they
    /// were filtered with; either changing retires them.
    uint64_t shadow_casters_revision_{~0ULL};
    layer_mask shadow_casters_mask_{};
    /// Render frame the mover bounds / LOD were last refreshed in: build_shadows can run
    /// several times per frame (probe faces, then the camera) and one refresh serves them all.
    uint64_t shadow_casters_frame_{~0ULL};
    /// The subset of the two lists reaching into the range of the local light being generated.
    shadow::shadow_map_models_t shadow_light_casters_;

    /// Brings the cached caster lists up to date for this frame: a wholesale rebuild when the
    /// revision or the layer mask changed, then a per-frame refresh of the movers' bounds and
    /// LOD. @return the lists, static first.
    void refresh_shadow_casters(scene& scn, const camera& camera, delta_t dt, layer_mask render_mask);

public:

private:
    /// The screen-space AO the lighting combines with the material AO: the GI's short-range AO in
    /// GI views, else GTAO's texture, or ASSAO's when GTAO is off (visibility in alpha either
    /// way), white when none ran.
    struct screen_ao_inputs
    {
        gfx::texture::ptr texture;
        /// u_screen_ao: x = intensity, y = bent normal strength (GTAO only), z = multi-bounce of
        /// the screen term (GTAO's setting, on otherwise), w = 1 when the texture carries a bent normal.
        std::array<float, 4> params{};
        /// Cap of the albedo the multi-bounce fit uses (LUMEN_SHORT_RANGE_AO_MAX_MULTIBOUNCE_ALBEDO for the
        /// short-range AO); 0 leaves it uncapped.
        float multi_bounce_albedo_cap = 0.0f;
    };
    auto get_screen_ao_inputs(gfx::render_view& rview) const -> screen_ao_inputs;

    /// The untraced reflection layer the lighting and the reflection debug views read: the GI's rough specular
    /// history where the GI reflections wrote this run (they composite nothing else under their traced layer),
    /// else @p pbuffer as the probe pass drew it.
    struct probe_layer_inputs
    {
        gfx::texture::ptr texture;
        /// u_probe_layer_params (fs_pbr_lighting.sh): x = 1 for the rough specular history, y = its scale (the GI
        /// intensity), z = 1 when it exists.
        std::array<float, 4> params{};
    };
    auto get_probe_layer_inputs(gfx::render_view& rview, const gfx::texture::ptr& pbuffer) const -> probe_layer_inputs;

    /// After SSIL/SSR; copies G-buffer depth into @c PREV_DEPTH for next-frame reprojection.
    /// @p has_consumer false releases the texture instead: this pipeline is the sole owner of its
    /// lifetime, so it is dropped here rather than by whichever consumer happens to run first and
    /// notice it does not need it.
    void snapshot_prev_depth(gfx::render_view& rview, const usize32_t& viewport_size, bool has_consumer);

    /// After TAA; copies the SCENE-REFERRED linear HDR target into @c PREV_SCENE_HDR for
    /// next frame's SSR trace and the GI screen traces. Deliberately pre-bloom/tonemap/UI:
    /// display-encoded values fed back into linear lighting would, with free-floating auto
    /// exposure, form a brightness feedback loop in dark scenes.
    /// @p has_consumer false releases the texture instead (the sole ownership of snapshot_prev_depth).
    void snapshot_prev_scene_color(gfx::render_view& rview,
                                   const gfx::frame_buffer::ptr& source,
                                   const camera& camera,
                                   bool has_consumer);

    /// The temporal-stability instrument over the finished image. Dispatches nothing unless a tool armed it.
    void run_temporal_probe_pass(const camera& camera,
                                 gfx::render_view& rview,
                                 const gfx::frame_buffer::ptr& output);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    int debug_pass_{-1};
    /// See pipeline::set_debug_view_scale.
    float debug_view_scale_{1.0f};
    /// The position the global SDF clipmap centres on: the camera's, or the one it had when the
    /// freeze experiment bit was set (has_frozen_clipmap_camera_).
    math::vec3 clipmap_camera_{};
    bool has_frozen_clipmap_camera_{false};
    /**
     * @brief This run's pre-exposure, the scene-color scale. Camera runs with HDR
     * output use the manual exposure times the adapted exposure the GPU delivered a few frames
     * ago (1 before the first); probe captures and LDR runs render unscaled. The state is kept
     * PER RENDER VIEW (pre_exposure_state::view_key, the previous value for the history
     * corrections included) and read back through get_pre_exposure by every pass that writes
     * or reads scene lighting - never held on this object, which serves every view of its
     * camera (probe captures, thumbnails) in turn.
     */
    auto update_pre_exposure(gfx::render_view& rview, const run_params& params, bool is_camera_run) -> pre_exposure_state;
    /// Velocity buffer production is active for the CURRENT run (camera run + velocity_pass
    /// step bit + a consumer). Set per run in run_pipeline_impl; also excludes movers from
    /// static-mesh batching so their G-buffer depth matches the velocity pass raster (EQUAL).
    bool velocity_run_active_{false};
    /// The GI reflections wrote RBUFFER and PBUFFER in this run (run_lumen_gi_pass), so RBUFFER needs no clear.
    bool lumen_reflections_written_{false};
    /// Render frame of the last velocity pass that drew ANY mover (individual or batched),
    /// stamped inside run_velocity_pass's own visibility walk - the CPU-side signal for SSR's
    /// mover gate, held one temporal window by the consumer. Riding the owning pass's loop keeps
    /// the signal exactly as covered as the buffer it describes (off-screen movers are accepted as
    /// uncovered by design - no registry scan).
    uint64_t velocity_movers_frame_{~0ull};

};

} // namespace rendering
} // namespace unravel
