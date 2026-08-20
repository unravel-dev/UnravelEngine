#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>

#include "trace_resolution.h"

namespace unravel
{

/**
 * @brief Gathers world-space cached radiance into a screen-space indirect diffuse buffer.
 *
 * The consumer of the surface cache. Rays leave each shading point through the distance field and
 * read the radiance already stored wherever they land, so a single ray returns a fully lit result
 * instead of an unlit hit that would have to be shaded again -- and, once the update pass casts
 * rays of its own, a multi-bounce one.
 *
 * A ray may land on geometry that is off screen or behind the camera and still read a valid
 * value, because the cache is anchored to the world rather than to the frame. That is the
 * property a screen-space estimate cannot have at any sample count.
 */
class gi_resolve_pass
{
public:
    struct settings
    {
        /// Artistic multiplier on the scene's own bounce (the environment fallback keeps probe
        /// intensity). The one energy knob that survives the Phase 8 collapse: everything else
        /// is derived or owned by gi_constants (tasks/gi_rewrite_plan.md, section 5).
        float intensity = 1.0f;
        /// Indirect diffuse is low frequency; tracing below full resolution costs little.
        trace_resolution resolution = trace_resolution::half;
        /// Probe tile edge in FULL-RESOLUTION pixels [S21 s34: Lumen's DownsampleFactor]. The
        /// spacing halves in trace-target pixels at half resolution, so probe density follows
        /// the trace resolution automatically.
        int probe_spacing = gi::GI_SCREEN_PROBE_SPACING;
        /// Hi-Z screen tier of the gather: rays march the depth pyramid first and commit
        /// pixel-precise on-screen hits before the SDF answers. Needs the pyramid (built when
        /// GI or the reflection stack is on); off degrades to pure SDF tracing.
        bool enable_screen_trace = true;
        /// Depth-lobe std (in probe spacings) above which a world-probe cage member's
        /// visibility is settled by the field march instead of its Chebyshev moments. 0
        /// marches every probe - the maximum-quality, slowest extreme (a debugging
        /// baseline); raising it trusts the moments over a wider band - cheaper, with a
        /// wider leak margin on small-mixture silhouette wedges. The constant is the
        /// derivation-carrying default (the reflection_temporal_frames precedent).
        float probe_visibility_variance_gate = gi::GI_WORLD_PROBE_CAGE_VIS_VARIANCE_GATE;
        /// Adaptive gather: odd-lattice probes whose anchor lies on their even-lattice
        /// parents' plane (and faces within one octahedral texel of them) skip the 64-ray
        /// trace and reconstruct from the parents in probe space - flat regions run on a
        /// quarter of the ray budget while geometry breaks keep full probe density. The
        /// classification is exactly the interpolation test the integrate pass applies per
        /// pixel, so a skipped probe's tile is one the pixels would have blended to anyway.
        bool adaptive_probes = true;
        /// Windowed probe-space temporal: each traced probe fires 16 of 64 octahedral rays
        /// per frame (a 2x2 Bayer stratum) and blends them into that probe's own previous
        /// tile. Placement stays sticky while the origin is still in-tile so a still camera
        /// fills one sphere; probes walk to a new Halton after a few complete spheres so
        /// blotches can dissolve. A miss copies the previous tile (never black).
        /// Off traces all 64 every frame.
        bool probe_space_temporal = true;
        /// World-space specular tier (plan phase 9), layered under SSR: rough lobes read the
        /// world-probe radiance atlas, sharp ones trace (screen first, SDF + light voxels
        /// beyond) - contributing the off-screen reflections SSR cannot have.
        bool enable_reflections = true;
        /// Temporal window (frames) for the stochastic reflection ray - steady-state blend
        /// weight is one over this. 0 or 1 disables the reflection temporal entirely, the
        /// A/B knob for verifying the accumulation is alive.
        int reflection_temporal_frames = gi::GI_REFLECTION_TEMPORAL_FRAMES;
        /// Checkerboard reflection trace: half the texels per frame, the temporal fills the
        /// rest from clamped history. OFF BY DEFAULT: on sharp/glossy content the parity
        /// pattern shows at reflection silhouettes, held history ghosts through the wide
        /// edge AABB, and the two halves shimmer against each other (measured on the mirror
        /// test scene, 2026-08-19). Until the fill is velocity-aware and edge-clamped it is
        /// an opt-in trade of those artifacts for ~half the trace cost - viable for scenes
        /// whose reflectors are all in the rough band, where the composite blur hides the
        /// pattern. Ignored while the reflection temporal is off.
        bool reflection_checkerboard = false;
        /// 0 = off. 1 = RAY TIERS: every gather ray paints its answering tier instead of
        /// radiance - green = screen-trace commit, red = SDF hit, blue = world-probe/sky
        /// completion - and the mix survives the whole chain, so the lit image shows the
        /// screen tier's actual coverage. Session-only, deliberately not serialized.
        ///
        /// A screen-space contact AO stage was tried here and REMOVED: it duplicated ASSAO's
        /// role at best. The under-overhang darkness it chased is a RADIANCE property - the
        /// bounce term's cavity occlusion (GiBounceCavityVisibility in cs_gi_light_voxels) -
        /// not a post-multiply. Screen-space AO stays ASSAO's job.
        int debug_view = 0;
        /// Full-resolution temporal accumulation over the integrated irradiance.
        bool enable_temporal = true;
        /// PROBE-SPACE accumulation cap (the screen-probe tiles' 1/n blend gate). The
        /// full-res temporal no longer uses this: it runs the dual-rate pair below.
        float max_accum_frames = float(gi::GI_TEMPORAL_MAX_FRAMES);
        /// The full-res temporal's SLOW lane cap - the stability window. Long on purpose:
        /// a small bright emissive source excites ~16-frame amortization waves (crawling
        /// voxel-scale blobs) that a short mean cannot average. Costs no responsiveness -
        /// the change detector snaps to the 8-frame fast lane on a real lighting change.
        float temporal_slow_frames = float(gi::GI_TEMPORAL_SLOW_FRAMES);
        /// Reprojection depth tolerance (relative depth error per unit view distance).
        float reprojection_tolerance = gi::GI_TEMPORAL_DEPTH_TOLERANCE;
        /// A-trous spatial denoise over the accumulated result.
        bool enable_spatial_denoise = true;
        /// Converged early-out: a pixel at the full accumulation count whose variance has
        /// collapsed skips the 24-tap kernel - the edge stops make the filter an identity
        /// there. The A/B switch for verifying no residual chroma structure is lost.
        bool denoise_converged_early_out = true;
        int denoise_passes = 3;
        float denoise_normal_power = 32.0f;
        float denoise_luma_phi = 32.0f;
        /// Luminance-stop floor as a fraction of each pixel's own luminance. The
        /// variance-driven stop collapses on converged pixels and then preserves ANY
        /// leftover structure - including the coherent probe/voxel-scale sampling-bias
        /// blobs no temporal window can remove. The floor keeps same-plane neighbours
        /// within this contrast merging after convergence; 0 restores the pure
        /// variance-driven stop (the A/B).
        float denoise_luma_floor = 0.08f;
        float denoise_plane_tolerance = 0.02f;
        float denoise_low_count_boost = 16.0f;
        /// Joint bilateral upsample from the trace resolution to full resolution.
        bool enable_bilateral_upsample = true;
        float upsample_normal_power = 32.0f;
        float upsample_plane_tolerance = 0.02f;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr g_buffer;
        /// Previous frame's depth, used to validate reprojected history. Null disables temporal
        /// accumulation for this frame rather than accepting history blindly.
        gfx::texture::ptr prev_depth;
        /// The frame's Hi-Z depth pyramid (shared with SSR/SSIL). When present the gather's
        /// screen-trace tier runs; null falls back to pure SDF tracing with the raw G-buffer
        /// depth bound in its place.
        gfx::texture::ptr hiz;
        /// The lighting pass's environment SH probe. A gather ray that escapes the scene reads
        /// sky radiance from it and counts as RESOLVED, so sky occlusion survives into the lit
        /// image instead of being refilled by the consumer's unoccluded environment term --
        /// the same miss fallback the screen-space SSIL trace uses. Null (first frame, before
        /// the irradiance pass has run once) disables the measurement for the frame.
        gfx::texture::ptr irradiance_sh;
        /// LAST frame's composited output (the SSR convention, same source): far-field
        /// radiance for hits beyond the cascades, where the light voxels hold nothing.
        /// Null (first frame, probe captures) falls back to the sky SH for those hits.
        gfx::texture::ptr prev_color;
        const camera* cam{};
        surface_cache_system* surface_cache{};
        /// This camera's cascade. The cascade is snapped around a viewer, so it cannot live on
        /// the service without two cameras fighting over one set of levels.
        surface_cache_view* view_cache{};
        settings settings;
    };

    ~gi_resolve_pass();

    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Gathers cached radiance for every visible surface.
     * @return The result texture, or null when the pass could not run.
     *
     * The output matches the SSIL convention exactly -- RGB is a hemispherical indirect diffuse
     * estimate in radiance-mean units, A is the weight with which it replaces the environment
     * probe -- so the existing consumer needs no change and the two stay directly comparable.
     */
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr;

    /// Whether the gather programs loaded. The legacy hash-cache paths are gone (Phase 8);
    /// without these programs the pass clears its output and the environment term covers.
    /// Placement, classification, the indirect-args writer and the interp/clear pass are all
    /// REQUIRED with the compacted gather: the trace launches from the classify pass's list
    /// and no longer writes non-traced tiles itself.
    auto has_gather_programs() const -> bool
    {
        return place_program_.is_valid() && classify_program_.is_valid() &&
               args_program_.is_valid() && trace_program_.is_valid() &&
               interp_program_.is_valid() && filter_program_.is_valid() &&
               integrate_program_.is_valid();
    }

private:
    /// The double-buffered temporal history pair for this frame: acquired once per frame by
    /// whichever form of the temporal runs (fused or split) - it advances the parity counter
    /// and owns the no-history warning.
    struct history_targets
    {
        gfx::frame_buffer::ptr write_fbo;
        gfx::texture::ptr write_tex;
        gfx::texture::ptr write_moments;
        gfx::texture::ptr write_fast;
        gfx::texture::ptr read_tex;
        gfx::texture::ptr read_moments;
        gfx::texture::ptr read_fast;
        bool has_history{};
    };
    auto acquire_history(gfx::render_view& rview,
                         const run_params& params,
                         const usize32_t& target_size) -> history_targets;

    /// Blends this frame's gather into the reprojected history. Returns the accumulated texture.
    /// The split fallback: the deliverable path fuses this blend onto the integrate pass.
    auto run_temporal(gfx::render_view& rview,
                      const run_params& params,
                      const gfx::texture::ptr& current,
                      const usize32_t& target_size,
                      gfx::texture::ptr& out_moments) -> gfx::texture::ptr;

    /**
     * @brief Edge-preserving spatial filter over the accumulated result.
     *
     * Deliberately runs AFTER the history has been written, and its output is never fed back.
     * Accumulating an already blurred image would compound the blur every frame and smear
     * indirect lighting across the scene.
     */
    auto run_spatial_denoise(gfx::render_view& rview,
                             const run_params& params,
                             const gfx::texture::ptr& input,
                             const gfx::texture::ptr& moments,
                             const usize32_t& target_size) -> gfx::texture::ptr;

    /// Surface-aware reconstruction of the full-resolution buffer from the reduced-resolution
    /// gather. Only invoked when the two actually differ.
    auto run_upsample(gfx::render_view& rview,
                      const run_params& params,
                      const gfx::texture::ptr& input,
                      const usize32_t& source_size) -> gfx::texture::ptr;

    /// GI gather programs (plan phase 5). Constant-driven: their only uniforms are the probe
    /// lattice descriptors, the camera, and the world-structure bindings.
    struct trace_program : uniforms_cache
    {
        /// Compacted 16-thread group (cs_gi_screen_probe_trace.sc). Used while
        /// probe-space temporal is on: one thread per Bayer-stratum ray.
        gpu_program::ptr program;
        /// 8x8 group (cs_gi_screen_probe_trace_full.sc). A/B-off and the first
        /// untrusted frame: all 64 octahedral rays in parallel.
        gpu_program::ptr full_program;
        gfx::program::uniform_ptr u_gi_camera;
        gfx::program::uniform_ptr u_gi_screen_trace;
        gfx::program::uniform_ptr u_gi_prev_view_proj;
        gfx::program::uniform_ptr u_gi_probe_params;
        gfx::program::uniform_ptr u_gi_probe_screen;
        gfx::program::uniform_ptr u_gi_probe_temporal;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_gi_world_probe_params;
        gfx::program::uniform_ptr u_gi_world_probe_atlas;
        gfx::program::uniform_ptr u_gi_world_probe_radiance_atlas;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        /// The Hi-Z pyramid when the screen tier runs, else the raw G-buffer depth (mip 0 of
        /// the pyramid is the device depth verbatim, so the anchor reads either).
        gfx::program::uniform_ptr s_hiz;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_light_voxels;
        gfx::program::uniform_ptr s_world_probe_depth;
        gfx::program::uniform_ptr s_world_probe_radiance_read;
        gfx::program::uniform_ptr s_gi_env_sh;
        gfx::program::uniform_ptr s_gi_prev_color;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_camera, "u_gi_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_screen_trace, "u_gi_screen_trace", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_prev_view_proj, "u_gi_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_screen, "u_gi_probe_screen", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_temporal, "u_gi_probe_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_light_voxel_params, "u_gi_light_voxel_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_params, "u_gi_world_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_atlas, "u_gi_world_probe_atlas", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_gi_world_probe_radiance_atlas,
                          "u_gi_world_probe_radiance_atlas",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_light_voxels, "s_light_voxels", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_world_probe_depth, "s_world_probe_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(),
                          s_world_probe_radiance_read,
                          "s_world_probe_radiance_read",
                          gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_env_sh, "s_gi_env_sh", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_prev_color, "s_gi_prev_color", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return (program && program->is_valid()) || (full_program && full_program->is_valid());
        }

        /// Compact when temporal is on and that program linked; otherwise the 8x8
        /// full path; last resort the compact program (window 1 walks 4 phases).
        auto select(bool want_compact) const -> gpu_program*
        {
            if(want_compact && program && program->is_valid())
            {
                return program.get();
            }
            if(full_program && full_program->is_valid())
            {
                return full_program.get();
            }
            if(program && program->is_valid())
            {
                return program.get();
            }
            return nullptr;
        }
    } trace_program_;

    struct filter_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_probe_params;
        gfx::program::uniform_ptr u_gi_probe_screen;
        gfx::program::uniform_ptr u_gi_probe_temporal;
        gfx::program::uniform_ptr s_probe_radiance;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_screen, "u_gi_probe_screen", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_temporal, "u_gi_probe_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_probe_radiance, "s_probe_radiance", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } filter_program_;

    /// Placement (adaptive gather): computes every probe's anchor into the records before the
    /// trace, which is what lets the trace classify a probe against its parents' anchors.
    struct place_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_camera;
        gfx::program::uniform_ptr u_gi_probe_params;
        gfx::program::uniform_ptr u_gi_probe_screen;
        gfx::program::uniform_ptr u_gi_probe_temporal;
        gfx::program::uniform_ptr u_gi_world_probe_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_hiz;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_camera, "u_gi_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_screen, "u_gi_probe_screen", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_temporal, "u_gi_probe_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_params, "u_gi_world_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } place_program_;

    /// Classification + compaction: decides traced/interpolated per probe and appends the
    /// traced coordinates to the dense list the trace launches from.
    struct classify_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_probe_params;
        gfx::program::uniform_ptr u_gi_probe_temporal;
        gfx::program::uniform_ptr u_gi_screen_trace;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_temporal, "u_gi_probe_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_screen_trace, "u_gi_screen_trace", gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } classify_program_;

    /// One-thread bridge: traced count -> the trace's indirect dispatch args (two entries:
    /// one-group-per-probe for the full program, four-probes-per-group for the compact one)
    /// plus the count itself, staged into the list head for the kernel's bounds check.
    struct args_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_probe_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } args_program_;

    /// Reconstruction (adaptive gather): fills interpolated probes' tiles from their parents
    /// between the trace and the filter - and clears dead probes' tiles, which the compacted
    /// trace no longer visits.
    struct interp_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_probe_params;
        gfx::program::uniform_ptr u_gi_probe_temporal;
        gfx::program::uniform_ptr u_gi_screen_trace;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_temporal, "u_gi_probe_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_screen_trace, "u_gi_screen_trace", gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } interp_program_;

    struct integrate_program : uniforms_cache
    {
        gpu_program::ptr program;
        /// The FUSED integrate+temporal form (fs_gi_probe_integrate_temporal.sc), preferred
        /// while temporal accumulation is on: the gather feeds the blend in registers and the
        /// GI_TRACE round trip disappears. Temporal-side uniforms ride the temporal_program_
        /// handles (bgfx uniforms are name-global). The split pair stays as the fallback and
        /// serves when temporal is disabled.
        gpu_program::ptr fused_program;
        gfx::program::uniform_ptr u_gi_camera;
        gfx::program::uniform_ptr u_gi_intensity;
        gfx::program::uniform_ptr u_gi_probe_params;
        gfx::program::uniform_ptr u_gi_probe_screen;
        gfx::program::uniform_ptr u_gi_probe_temporal;
        gfx::program::uniform_ptr u_gi_world_probe_params;
        gfx::program::uniform_ptr u_gi_world_probe_atlas;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_probe_irradiance;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_world_probe_irradiance;
        gfx::program::uniform_ptr s_world_probe_depth;
        /// Declared by sdf_common.sh but never sampled here. Bound anyway: on OpenGL the
        /// driver decides what stays active (profile 430 ships raw source), and an active
        /// sampler with no texture at its default unit 0 fails the whole draw.
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_gi_camera, "u_gi_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_intensity, "u_gi_intensity", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_screen, "u_gi_probe_screen", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_temporal, "u_gi_probe_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_params, "u_gi_world_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_atlas, "u_gi_world_probe_atlas", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_probe_irradiance, "s_probe_irradiance", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(),
                          s_world_probe_irradiance,
                          "s_world_probe_irradiance",
                          gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_world_probe_depth, "s_world_probe_depth", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } integrate_program_;

    /// False until both record halves hold real data; gates the trace's importance
    /// reprojection so freshly allocated garbage is never read as history.
    bool records_trusted_ = false;
    /// Probe lattice of the last traced frame. Reprojection indexes the READ half by the same
    /// layout, so a lattice change makes the whole history unaddressable and must reset it.
    uint32_t probe_grid_x_ = 0;
    uint32_t probe_grid_y_ = 0;

    /// Probe SH + meta storage. A member rather than a render-view resource because it is a
    /// buffer, and its capacity only ever grows.
    gfx::dynamic_vertex_buffer_handle probe_buffer_{bgfx::kInvalidHandle};
    uint32_t probe_buffer_capacity_ = 0;
    /// The compacted traced-probe list: [0] = count, [1..] = packed coordinates. Written by
    /// classify, sized to the lattice, consumed by the indirect-args pass and the trace.
    gfx::dynamic_index_buffer_handle probe_traced_{bgfx::kInvalidHandle};
    uint32_t probe_traced_capacity_ = 0;
    /// The trace's indirect dispatch args, written on the GPU from the traced count.
    gfx::indirect_buffer_handle probe_args_{bgfx::kInvalidHandle};

    struct temporal_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_temporal_clamp;
        gfx::program::uniform_ptr u_gi_prev_view_proj;
        gfx::program::uniform_ptr u_gi_prev_inv_view_proj;
        gfx::program::uniform_ptr u_gi_temporal_params;
        gfx::program::uniform_ptr u_gi_temporal_camera;
        gfx::program::uniform_ptr s_gi_current;
        gfx::program::uniform_ptr s_gi_history;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_prev_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_gi_history_moments;
        gfx::program::uniform_ptr s_gi_history_fast;
        gfx::program::uniform_ptr u_gi_temporal_texel;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_temporal_clamp, "u_gi_temporal_clamp", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_prev_view_proj, "u_gi_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), u_gi_prev_inv_view_proj, "u_gi_prev_inv_view_proj",
                          gfx::uniform_type::Mat4);
            cache_uniform(program.get(), u_gi_temporal_params, "u_gi_temporal_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_temporal_camera, "u_gi_temporal_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_current, "s_gi_current", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_history, "s_gi_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_prev_depth, "s_gi_prev_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_history_moments, "s_gi_history_moments",
                          gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_history_fast, "s_gi_history_fast",
                          gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_gi_temporal_texel, "u_gi_temporal_texel", gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } temporal_program_;

    struct denoise_program : uniforms_cache
    {
        gpu_program::ptr program;
        /// The LDS-staged compute form (cs_gi_denoise.sc), preferred when it linked: the
        /// fragment form is fetch-bound at 24 taps x 3 fetches per pixel per pass, and the
        /// staging collapses that to ~9 staged texels per pixel. Same math; the fragment
        /// program stays as the fallback.
        gpu_program::ptr compute_program;
        gfx::program::uniform_ptr u_gi_denoise_params;
        gfx::program::uniform_ptr u_gi_denoise_texel;
        gfx::program::uniform_ptr u_gi_denoise_params2;
        gfx::program::uniform_ptr u_gi_denoise_camera;
        gfx::program::uniform_ptr s_gi_input;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_gi_moments;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_denoise_params, "u_gi_denoise_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_denoise_texel, "u_gi_denoise_texel", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_denoise_params2, "u_gi_denoise_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_denoise_camera, "u_gi_denoise_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_input, "s_gi_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_moments, "s_gi_moments", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } denoise_program_;

    struct upsample_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_upsample_texel;
        gfx::program::uniform_ptr u_gi_upsample_params;
        gfx::program::uniform_ptr u_gi_upsample_camera;
        gfx::program::uniform_ptr s_gi_input;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_upsample_texel, "u_gi_upsample_texel", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_upsample_params, "u_gi_upsample_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_upsample_camera, "u_gi_upsample_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_input, "s_gi_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } upsample_program_;

    /// Creates or resizes an RGBA16F render target owned by the render view.
    /// @param compute_write Also usable as a compute image store target (the denoise chain).
    static auto create_or_update_target(gfx::render_view& rview,
                                        const std::string& name,
                                        const usize32_t& size,
                                        gfx::texture::ptr& out_tex,
                                        bool compute_write = false) -> gfx::frame_buffer::ptr;

    /// As @ref create_or_update_target, with a second attachment for luminance moments and the
    /// accumulation count, and a third for the dual-rate temporal's FAST lane. They share a
    /// framebuffer because they are written by one pass and must stay exactly in step -- a
    /// count that disagreed with its colour would corrupt the mean.
    static auto create_or_update_target_mrt(gfx::render_view& rview,
                                            const std::string& name,
                                            const usize32_t& size,
                                            gfx::texture::ptr& out_color,
                                            gfx::texture::ptr& out_moments,
                                            gfx::texture::ptr& out_fast) -> gfx::frame_buffer::ptr;

    /// Consecutive frames with no usable history. A couple is normal at startup and after a
    /// resize; a sustained run means accumulation is not happening at all.
    static constexpr uint32_t history_warning_frames = 120;
    uint32_t frames_without_history_ = 0;
};

} // namespace unravel
