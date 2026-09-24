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

    auto run_indirect_lighting_pass(scene& scn, const camera& camera, gfx::render_view& rview, bool apply_reflection, delta_t dt)
        -> gfx::frame_buffer::ptr;

    void run_reflection_probe_pass(scene& scn, const camera& camera, gfx::render_view& rview, bool apply_probes, delta_t dt);

    auto run_atmospherics_pass(gfx::frame_buffer::ptr input,
                               scene& scn,
                               const camera& camera,
                               gfx::render_view& rview,
                               delta_t dt) -> gfx::frame_buffer::ptr;

    /// Renders the cloud shadow map for this run into cloud_shadow_ (before the lighting passes).
    void run_cloud_shadow_pass(scene& scn, const camera& camera, gfx::render_view& rview);

    void run_ssr_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams);

    void run_ssil_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams);
    /// Ground Truth Ambient Occlusion into the "GTAO" texture (runs right after the G-buffer,
    /// consumed by the indirect lighting).
    void run_gtao_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams);

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

    auto run_tonemapping_pass(gfx::render_view& rview, const gfx::frame_buffer::ptr& input, const gfx::frame_buffer::ptr& output,
                              const run_params& rparams)
        -> gfx::frame_buffer::ptr;
    void run_debug_visualization_pass(const camera& camera,
                                      gfx::render_view& rview,
                                      const gfx::frame_buffer::ptr& output);

    /// Debug pass ids at or above this one are handled by the distance field visualiser
    /// rather than by the G-buffer visualiser, whose shader only knows modes 0..14.
    static constexpr int debug_pass_sdf_normals = 15;
    static constexpr int debug_pass_sdf_step_count = 16;
    static constexpr int debug_pass_sdf_headers = 17;
    static constexpr int debug_pass_sdf_probe = 18;
    static constexpr int debug_pass_sdf_entry = 19;
    static constexpr int debug_pass_sdf_clipmap = 20;
    static constexpr int debug_pass_sdf_direct = 21;
    static constexpr int debug_pass_sdf_cascade_levels = 22;
    static constexpr int debug_pass_sdf_attr_albedo = 23;
    static constexpr int debug_pass_sdf_light_voxels = 24;
    static constexpr int debug_pass_sdf_world_probes = 25;
    static constexpr int debug_pass_sdf_sun_tiers = 26;
    static constexpr int debug_pass_sdf_probe_sky = 27;
    static constexpr int debug_pass_sdf_vis_memo = 28;
    /// Velocity buffer visualization. Not part of the SDF band: dispatched by an exact match
    /// BEFORE the >= debug_pass_sdf_normals check. Selecting it forces velocity production
    /// for camera runs even when no other consumer (TAA) is active.
    static constexpr int debug_pass_velocity = 29;
    /// GTAO visibility and bent normal (the "GTAO" texture), through the G-buffer visualization
    /// program (shader modes 15 / 16); dispatched BEFORE the >= debug_pass_sdf_normals check.
    static constexpr int debug_pass_gtao = 30;
    static constexpr int debug_pass_gtao_bent_normal = 31;

    /// GI views added after the velocity/GTAO ids, so those keep the numbers the editor's
    /// static_asserts pin. All four are >= debug_pass_sdf_normals, so they route to the SDF
    /// debug pass and keep the GI world state alive exactly like the rest of that group.
    static constexpr int debug_pass_gi_attr_emissive = 32;
    static constexpr int debug_pass_gi_cage_health = 33;
    static constexpr int debug_pass_gi_dirty_regions = 34;
    static constexpr int debug_pass_gi_probe_lattice = 35;
    /// SCREEN-SPACE GI views. Also handled by the SDF debug pass (it is the one fullscreen pass
    /// with the GI bindings), but they read screen buffers rather than tracing, so the shader
    /// answers them before the march.
    static constexpr int debug_pass_gi_screen_probes = 36;
    static constexpr int debug_pass_gi_temporal = 37;
    static constexpr int debug_pass_gi_probe_tiers = 38;
    /// The temporal's reset CAUSE per pixel (which mechanism limited the accumulation count)
    /// and the explicit emitter sampling census per screen probe (record [7]).
    static constexpr int debug_pass_gi_temporal_cause = 39;
    static constexpr int debug_pass_gi_emitter_share = 40;
    /// Auto exposure's own state, drawn as a blended panel OVER the finished image (UE's
    /// Visualize HDR). Dispatched by an exact match before the >= debug_pass_sdf_normals
    /// check, like velocity and GTAO - it is an overlay, not a replacement image.
    static constexpr int debug_pass_exposure = 41;
    void run_exposure_debug_pass(gfx::render_view& rview,
                                 const gfx::frame_buffer::ptr& output,
                                 const run_params& rparams);
    void run_sdf_debug_pass(const camera& camera,
                            gfx::render_view& rview,
                            const run_params& rparams,
                            const gfx::frame_buffer::ptr& output);

    /// Resolves the blended gi_settings for this run from the volume hooks; false = GI off.
    auto resolve_gi_settings(const run_params& rparams, gi_settings& gi) -> bool;

    /// GI world-state preparation for a camera run: surface-cache residency, the
    /// viewer-snapped clipmap cascade and its compose (also kept alive for the SDF
    /// debug views), then -- when a gi_component asks for GI -- voxel lighting and
    /// world-probe tracing. No-op for probe captures and when neither GI nor the
    /// SDF debug views need the cache.
    void run_gi_scene_passes(scene& scn, const camera& camera, gfx::render_view& rview, const run_params& params);

    /// Lights the resident surface voxels (GI v2 plan 3.2), with sun visibility
    /// answered by the sun's CSM cascade 0 when one was rendered this frame.
    /// @param indirect The quiescence gate's argument buffer when it decides on the GPU,
    ///        invalid when the CPU already decided (see gi_quiescence_gate_pass).
    /// @param collect_stats Stage the convergence readback this frame; only ever true on the
    ///        readback path, and only while the CPU-side inputs are still enough for the
    ///        sample to survive update_quiescence.
    void run_gi_light_voxel_pass(scene& scn,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 surface_cache_system& surface_cache,
                                 surface_cache_view& view_cache,
                                 const gi_settings& gi,
                                 bgfx::IndirectBufferHandle indirect,
                                 bool collect_stats);

    /// Traces world probes against the freshly lit voxels (GI v2 plan 3.3).
    /// @param indirect See run_gi_light_voxel_pass.
    void run_gi_world_probe_pass(const camera& camera,
                                 gfx::render_view& rview,
                                 surface_cache_system& surface_cache,
                                 surface_cache_view& view_cache,
                                 const gi_settings& gi,
                                 bgfx::IndirectBufferHandle indirect);

    /// World-space specular tier into RBUFFER, layered UNDER SSR. No-op unless a
    /// camera run with GI reflections enabled.
    void run_gi_reflection_pass(const camera& camera, gfx::render_view& rview, const run_params& params);

    /// Gathers the world structures into a screen-space indirect diffuse buffer.
    /// See gi_resolve_pass.
    /// @return true when the pass produced a result, which also means it needs PREV_DEPTH
    ///         snapshotted this frame for its temporal accumulation.
    /// Far-field radiance for hits beyond the cascades comes from @c PREV_SCENE_HDR
    /// (last frame's post-TAA linear scene color -- the SSR convention, same source).
    auto run_gi_resolve_pass(const camera& camera,
                             gfx::render_view& rview,
                             const run_params& rparams) -> bool;

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

    geom_program geom_program_;
    geom_program geom_program_skinned_;
    geom_program geom_program_instanced_;

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
            cache_uniform(program.get(), s_gtao, "s_gtao", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_gtao_params, "u_gtao_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_indirect_params, "u_indirect_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
        }
        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr u_light_data;
        gfx::program::uniform_ptr u_camera_position;
        std::array<gfx::program::uniform_ptr, 7> s_tex;
        gfx::program::uniform_ptr s_irradiance;
        gfx::program::uniform_ptr s_ssil;
        gfx::program::uniform_ptr s_gtao;
        gfx::program::uniform_ptr u_gtao_params;
        /// x = 1 when a real GI resolve / SSIL texture feeds s_ssil, 0 when the transparent
        /// fallback does; the shader then takes the resolve outright instead of mixing the
        /// environment SH back in (fs_pbr_lighting.sh, pbr_indirect).
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
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr u_params;
        std::array<gfx::program::uniform_ptr, 9> s_tex;

        std::unique_ptr<gpu_program> program;

    } debug_visualization_program_;

    struct irradiance_pass_result
    {
        gfx::texture::ptr irradiance_tex;
        math::vec3 global_color = {1.0f, 1.0f, 1.0f};
        float global_intensity = 0.0f;
        /// Revision of the environment radiance this pass baked: every input that can change
        /// IRRADIANCE_SH, folded. The GI world side keys its wake-up on it, because neither the
        /// analytic light set nor the clipmap content epoch moves when only the sky changes.
        /// Also published on the render view under GI_ENVIRONMENT_HASH, next to the texture.
        uint64_t environment_hash = 0;
    };
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
    /// After SSIL/SSR; copies G-buffer depth into @c PREV_DEPTH for next-frame reprojection.
    void snapshot_prev_depth(gfx::render_view& rview, const usize32_t& viewport_size);

    /// After TAA; copies the SCENE-REFERRED linear HDR target into @c PREV_SCENE_HDR for
    /// next frame's SSR trace and GI far-field. Deliberately pre-bloom/tonemap/UI: the old
    /// source (final OBUFFER) fed display-encoded values back into linear lighting, which
    /// with free-floating auto exposure formed a brightness feedback loop in dark scenes.
    void snapshot_prev_scene_color(gfx::render_view& rview,
                                   const gfx::frame_buffer::ptr& source,
                                   const camera& camera);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    int debug_pass_{-1};
    /// See pipeline::set_debug_view_scale.
    float debug_view_scale_{1.0f};
    /**
     * @brief UE FViewInfo::UpdatePreExposure: this run's scene-color scale. Camera runs with HDR
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
    /// Render frame of the last velocity pass that drew ANY mover (individual or batched),
    /// stamped inside run_velocity_pass's own visibility walk - the CPU-side signal for the
    /// GI reflection temporal's mover gate, held one temporal window by the consumer.
    /// Riding the owning pass's loop keeps the signal exactly as covered as the buffer it
    /// describes (off-screen movers are accepted as uncovered by design - no registry scan).
    uint64_t velocity_movers_frame_{~0ull};
    /// Rotation phase of the light-voxel update (GI_LIGHT_VOXEL_UPDATE_DENOM slices).
    uint32_t light_voxel_frame_{0};

};

} // namespace rendering
} // namespace unravel
