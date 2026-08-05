#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/surface_cache_service.h>
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
        /// Rays per pixel. Low on purpose: each one returns a PREFILTERED cell rather than a
        /// point sample of the incoming radiance, so the variance a path tracer would fight here
        /// has already been paid down by the cache.
        int ray_count = 4;
        /// Drop settled pixels to @ref min_ray_count using the temporal pass's per-pixel variance.
        ///
        /// A pixel with deep history and low luminance deviation has converged; tracing the full
        /// budget into it re-measures a known number. Disocclusion resets the history count and a
        /// lighting change raises the variance, and either restores the full budget exactly where
        /// it is needed -- so on a mostly static view this recovers a large share of the trace
        /// cost with no place for quality to hide: any pixel that could show the difference is by
        /// definition not settled. Requires temporal accumulation.
        bool adaptive_ray_count = true;
        /// Rays a settled pixel still traces. One is cheapest; two keeps a little exploration so
        /// a slow lighting drift is noticed within a few frames rather than only via the clamp.
        int min_ray_count = 2;
        /// How far a gather ray travels before giving up and taking the environment instead.
        float max_distance = 200.0f;
        /// Lift off the surface before tracing, as a FRACTION OF A VOXEL of the field answering at
        /// the shading point.
        ///
        /// A ray starting on the isosurface reads a distance of zero and stops immediately,
        /// reporting its own origin as an occluder -- surface acne. What the lift has to clear is
        /// the trace's hit acceptance, which is @ref surface_bias voxels, so this is measured in
        /// the same units and wants to be comfortably larger than it.
        ///
        /// Deliberately NOT a world distance, which is what it used to be. The cascade's voxel runs
        /// from 0.25 m at level 0 to 2 m at the outer level, so the world distance needed varies
        /// eightfold across a single view: measured, 0.1 cleared the acne in a scene contained
        /// inside level 0 while a view spanning levels 1-3 still needed 1.0. In voxels one value
        /// covers both.
        ///
        /// Raising it costs contact detail -- the lift carries rays over the small-scale occlusion
        /// they exist to find -- so prefer the smallest value that removes the acne. The launch
        /// suppression in the tracer now does the heavy lifting, which is what lets this sit at a
        /// fraction of a voxel instead of the 3.0 it needed before.
        float normal_bias_voxels = 0.2f;
        /// Gain on the cached bounce. The environment fallback is deliberately left at probe
        /// intensity, so this scales the scene's own contribution only.
        float intensity = 1.0f;
        /// Range in which per-instance fields are traced. Beyond it the global cascade answers,
        /// which cannot represent anything thinner than its voxels but costs one lookup.
        ///
        /// NON-ZERO is load bearing for contact range, not merely nicer. Inside this range the
        /// cascade is not consulted at all (the clipmap tier starts at this distance), and that is
        /// the only real defence against its dilation: a hedge or a cluster of furniture composes
        /// into a solid blob a voxel or more fatter than the geometry, and with the cascade
        /// answering from t = 0 every ray leaving the pavement beside it hits that phantom at
        /// point-blank range and reads the neighbour's dark cells at full weight -- a black pool
        /// hugging the geometry that no origin bias can remove, because the obstacle is not where
        /// the rays start but what they are traced against. The per-instance fields resolve the
        /// same geometry at mesh-voxel accuracy.
        ///
        /// On cost: the oft-quoted 8.9 ms for this tier on Bistro was measured while the near
        /// field forced its cone relaxation to zero, so every grazing ray burned its full step
        /// budget. The relaxation now applies here too and bounds exactly that case; re-measure
        /// before trading this range away, and prefer shortening it over zeroing it.
        float near_field_distance = 5.0f;
        /// View distance at which the near field has faded out entirely, 0 to disable fading.
        ///
        /// The per-instance tier exists for CONTACT fidelity, and contact detail is only visible
        /// near the camera: a pixel thirty metres away renders a cascade-scale area, so tracing
        /// its first few metres against exact mesh fields buys nothing the half-res gather and
        /// the denoiser can show. Fading the near field with view distance concentrates its cost
        /// -- the single most expensive thing in the GI frame -- on the pixels that can display
        /// what it pays for. The fade starts at half this distance and reaches zero here.
        float near_field_fade_distance = 24.0f;
        int max_steps = 32;
        /// Hit acceptance, as a FRACTION OF A VOXEL of whichever field answered. An absolute
        /// distance is meaningless here because voxel size varies with bake resolution, instance
        /// scale and cascade.
        float surface_bias = 0.1f;
        /// Cone half-angle tangent: hit acceptance grows by this fraction of distance travelled.
        ///
        /// This is the lever on the dominant cost in the whole system. Measured on Bistro, removing
        /// the per-instance tier drops this pass from 8.9 ms to 1.0 ms, and the step-count view puts
        /// that cost where a ray runs nearly parallel to a large surface -- a grazing sphere trace
        /// advances by a distance that stays small for its entire length. A growing acceptance
        /// radius is what bounds it.
        ///
        /// Safe in one direction and not the other, which is worth knowing before tuning it. It
        /// widens what counts as a HIT and never the step, so it can only ever stop a ray early --
        /// it cannot carry one through a wall, and so cannot leak light. What it does cost is
        /// over-occlusion at range, and a hit that sits further short of the surface for the cache
        /// to address from.
        ///
        /// Try 0.01 to 0.05. Watch the Resolve pass time, contact darkening at range, and the
        /// agreement rates in test_surface_resolve_addresses_one_cell_from_both_sides.
        float step_relaxation = 0.05f;
        /// How far along its OWN DIRECTION a ray starts, in voxels of the level covering the
        /// point.
        ///
        /// This does the job a large normal bias was doing, without its cost. Both skip the
        /// region where a ray would hit the surface it started on -- unavoidable, because the
        /// ray originates on the RASTER surface while it is traced against the SDF, and those
        /// disagree by up to a voxel.
        ///
        /// The difference is what they do to the shading point. A normal offset MOVES it, so it
        /// sees past nearby geometry and everything reads over-lit -- and since the offset has
        /// to be large enough for the worst ray, that over-lighting is paid by every ray. This
        /// leaves the point exactly where it is and skips only along the ray, so occlusion
        /// stays correct.
        ///
        /// Raise this and lower the normal bias, not the other way round.
        float ray_start_voxels = 1.0f;
        /// Replace the radiance output with a per-ray failure diagnostic.
        ///
        /// A gather ray contributes light only if it HITS geometry, its hit can be ADDRESSED,
        /// and a cache entry is FOUND there. Those three fail for completely different
        /// reasons and are indistinguishable in the lit image -- all three read as "dark".
        /// This shows them as R, G and B, so whichever channel is dark names the stage at
        /// fault instead of leaving it to be inferred.
        /// 0 = off. 1 = per-ray STAGE diagnostic (hit / addressed / found as R / G / B).
        /// 2 = per-ray DISTANCE diagnostic: R is the fraction of rays hitting within four
        /// voxels of their origin -- a self-hit, where the gather reads the radiance of the
        /// very surface it is shading and feeds it back. Mode 1 cannot see that, because a
        /// self-hit succeeds at every stage; it is the difference every origin-offset knob
        /// actually moves.
        int debug_ray_diagnostics = 0;
        /// Interpolate cached radiance across the four cells bracketing a hit in its tangent plane,
        /// rather than point sampling the one cell it lands in.
        ///
        /// A cell is metres across and a pixel is millimetres, so a point lookup makes the gather
        /// piecewise constant at cell scale -- blocks that shift as the cascade re-snaps or the
        /// level steps. That is bias rather than noise, so temporal accumulation converges to it
        /// instead of averaging it away, and the spatial filter cannot remove it without removing
        /// real detail too, because a cell boundary and a lighting edge look identical to a
        /// luminance edge stop.
        ///
        /// Costs four cache lookups per ray instead of one. The cheaper alternative -- jitter the
        /// lookup within the cell and let the temporal filter integrate it -- adds no lookups but
        /// turns the blocks into shimmer, which is worse exactly when the camera is moving, because
        /// that is when disocclusion has reset the accumulation count and there are no frames to
        /// integrate over.
        ///
        /// Off restores the point lookup, for comparison without a rebuild.
        bool interpolate_cache = true;
        /// Treat a ray that HIT geometry but found no cache entry as occluded rather than as
        /// unknown.
        ///
        /// A miss says the cell has not been lit yet; it does not say the cell is dark. But the ray
        /// did hit something, so it does say the environment is blocked in that direction. Off, that
        /// fraction of the hemisphere falls back to the consumer's SH irradiance probe -- and in a
        /// sealed room every ray misses, so the room stays lit by the sky through solid walls and
        /// never converges.
        ///
        /// On, such a ray contributes zero radiance at full weight, so an unlit room goes black.
        /// The cost is that a cache still filling in reads dark rather than probe-coloured. That is
        /// transient and self-correcting where the leak is permanent, which is why this defaults on.
        bool occlude_on_cache_miss = true;
        /// Indirect diffuse is low frequency, so tracing below full resolution costs little.
        trace_resolution resolution = trace_resolution::half;

        /// Gather through screen-space radiance probes instead of per-pixel ray bundles.
        ///
        /// One probe per @ref probe_spacing pixel tile traces 64 octahedral directions once;
        /// pixels integrate the four probes around them. Rays stop scaling with resolution,
        /// origins are shared so traces are coherent, and radiance is filtered in probe space
        /// before any pixel sees it -- the structure Lumen's final gather converged on, and the
        /// reason its quality-per-ray is unreachable for a per-pixel gather. The per-ray
        /// pipeline itself is shared (gi_gather_common.sh), so toggling this compares the
        /// gather ARCHITECTURE and nothing else.
        ///
        /// The per-pixel path remains the diagnostic surface: a non-zero
        /// @ref debug_ray_diagnostics forces it, because its modes read individual rays that
        /// the probe path deliberately aggregates away.
        bool use_probe_gather = true;
        /// Probe tile edge in FULL-RESOLUTION pixels. 16 puts ~8k probes on a 1080p frame --
        /// about half a million rays against the per-pixel path's two million -- and the
        /// spacing halves in trace-target pixels at half resolution, so probe density in the
        /// trace target follows the trace resolution automatically.
        int probe_spacing = 16;
        /// Frames of per-texel history a probe accumulates; 1 or less disables probe history.
        ///
        /// This is the probe path's PRIMARY stabiliser, and it works where the screen temporal
        /// cannot: probe error is spatially correlated -- one texel feeds a whole tile of pixels
        /// -- so the screen filter's neighbourhood clamp sees the neighbourhood move in unison
        /// and lets it through. Accumulating per DIRECTION in probe space happens before that
        /// correlation is created: each texel converges to the mean of its jittered cone over
        /// about this many frames. History cuts on disocclusion (anchor leaves its surface), so
        /// raising it costs lag only where lighting actually changes.
        float probe_history_frames = 16.0f;

        /// Averaging across frames is what turns a handful of rays into an effective sample count
        /// in the hundreds. Without it the estimate is re-rolled every frame and the noise MOVES,
        /// which reads far worse than a fixed pattern of the same magnitude.
        bool enable_temporal = true;
        /// Frames of history the running mean is allowed to reach.
        ///
        /// The blend weight is 1/n while n grows toward this, which is a TRUE mean and genuinely
        /// settles. A fixed weight is an exponential moving average instead, and that converges
        /// to a distribution rather than a value -- it keeps shimmering forever however long the
        /// camera is held still. The cap is what keeps it responsive to light that really changed.
        float max_accum_frames = 48.0f;
        /// Reprojection tolerance as a FRACTION of view distance, so one value works near and
        /// far -- reprojection error and depth precision both grow with distance.
        ///
        /// Measured: raising this to 1.0 removes essentially all fireflies, which is high enough
        /// that the world-position test never rejects and is effectively off. That reads alarming
        /// and is defensible, because rejection is what CAUSES the fireflies: a rejected pixel
        /// falls back to a single frame of a four-ray gather, and a single frame of a four-ray
        /// gather IS a firefly. Under sub-pixel TAA jitter the previous depth buffer was rasterised
        /// at a different offset again, so on high-frequency geometry -- foliage, railings, ivy --
        /// the reconstructed previous position disagrees constantly with nothing actually moving.
        ///
        /// What guards history instead is the neighbourhood clamp (@ref history_clamp_sigma), which
        /// bounds both failure modes at once rather than choosing between them. `lessons.md` records
        /// this: the reprojection test is a coarse guard, not a cliff, and no longer has to be
        /// exact. The offscreen and sky tests still reject outright.
        ///
        /// Lower it if ghosting appears behind fast-moving geometry -- that is the one thing the
        /// clamp cannot catch, because a smoothly moving object's history agrees with its
        /// neighbourhood.
        float reprojection_tolerance = 1.0f;
        /// Width of the history clamp, in standard deviations of the current frame's 3x3
        /// neighbourhood. Zero disables clamping and restores accept-or-reject.
        ///
        /// This is what makes the reprojection test above a coarse guard rather than a cliff.
        /// Binary rejection has no good setting under sub-pixel TAA jitter: the reprojected
        /// sample moves every frame, so high-frequency geometry fails a strict test constantly
        /// even with a static camera, and each failure drops the pixel to a single frame of a
        /// four-ray gather -- which is exactly what fireflies are. Loosening the test instead
        /// keeps stale history and smears it behind moving geometry.
        ///
        /// Clamping avoids choosing: agreeing history survives intact, disagreeing history is
        /// pulled to the edge of what this frame actually sees. Lower values suppress more noise
        /// and risk more clipping of genuinely bright indirect light; higher values are closer
        /// to unclamped accumulation.
        float history_clamp_sigma = 2.0f;

        /// Temporal alone leaves visible grain: a dozen frames of a few rays is not many samples
        /// for a signal where a ray either finds a lit surface or a shadowed one. Averaging across
        /// space closes the gap, and costs little because indirect diffuse is low frequency.
        bool enable_spatial_denoise = true;
        /// Tap spacing DOUBLES each pass, so reach grows exponentially while cost stays linear.
        /// That is the entire point of the a-trous formulation.
        int denoise_passes = 4;
        /// Exponent on normal agreement. Higher keeps light from turning corners.
        float denoise_normal_power = 32.0f;
        /// Multiplier on the measured luminance standard deviation, forming the luminance
        /// edge-stop tolerance. Low values preserve more detail and filter less; the variance
        /// term already widens the tolerance wherever the estimate has not settled.
        float denoise_luma_phi = 32.0f;
        /// How far off the centre pixel's PLANE a tap may sit before being rejected, as a
        /// fraction of view distance so one value works at every depth.
        float denoise_plane_tolerance = 0.02f;
        /// Luminance tolerance multiplier applied at ONE accumulated sample, decaying to 1 as the
        /// count grows. Compensates for freshly disoccluded pixels, which have no usable temporal
        /// variance and would otherwise be left almost unfiltered exactly where they are noisiest.
        float denoise_low_count_boost = 16.0f;

        /// Reconstruct full resolution with a surface-aware upsample rather than a bilinear tap.
        /// A bilinear tap blends across silhouettes, which is where the gather is noisiest, so it
        /// spreads exactly the values that should not be spread.
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
        const camera* cam{};
        surface_cache_service* surface_cache{};
        /// This camera's cascade. The cascade is snapped around a viewer, so it cannot live on
        /// the service without two cameras fighting over one set of levels.
        surface_cache_view* view_cache{};
        /// Probe DEBUG view, selected in the editor's debug-view list beside the other GI
        /// visualisers -- a per-run pipeline input, not a scene setting. 0 = off. 1 = the raw
        /// radiance atlas in place, every tile showing its probe's 64 octahedral texels (a texel
        /// blinking here is trace-side variance; a still atlas under a dancing image means the
        /// fault is downstream). 2 = integration health (R = weight sum, dark = probe
        /// starvation; G = resolved fraction). 3 = history state (R = this frame's blend weight,
        /// red = history cut; G = sample count over 32). Flickering red in mode 3 on a STATIC
        /// camera means the history validity test cuts where it should hold. While active the
        /// pass returns the raw trace target, skipping temporal, denoise and upsample, so the
        /// view is crisp.
        int probe_debug = 0;
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

private:
    /// Blends this frame's gather into the reprojected history. Returns the accumulated texture.
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

    struct resolve_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_resolve_params;
        gfx::program::uniform_ptr u_gi_resolve_trace;
        gfx::program::uniform_ptr u_gi_resolve_camera;
        gfx::program::uniform_ptr u_gi_resolve_filter;
        gfx::program::uniform_ptr u_gi_resolve_debug;
        gfx::program::uniform_ptr u_gi_cache_params;
        gfx::program::uniform_ptr u_gi_cache_params2;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        /// Diagnostic only: the shader divides a debug readout by the albedo the CONSUMER will
        /// multiply it back by, so the value on screen is the number the shader wrote rather than
        /// the number times the surface's paint. See GiDebugUnshade in fs_gi_resolve.sc.
        gfx::program::uniform_ptr s_gi_base_color;
        /// Previous frame's accumulated moments, for the variance-guided ray budget.
        gfx::program::uniform_ptr s_gi_prev_moments;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_resolve_params, "u_gi_resolve_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_trace, "u_gi_resolve_trace", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_camera, "u_gi_resolve_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_filter, "u_gi_resolve_filter", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_debug, "u_gi_resolve_debug", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params, "u_gi_cache_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params2, "u_gi_cache_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_base_color, "s_gi_base_color", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_prev_moments, "s_gi_prev_moments", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } resolve_program_;

    struct probe_trace_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_resolve_params;
        gfx::program::uniform_ptr u_gi_resolve_trace;
        gfx::program::uniform_ptr u_gi_resolve_camera;
        gfx::program::uniform_ptr u_gi_resolve_filter;
        gfx::program::uniform_ptr u_gi_cache_params;
        gfx::program::uniform_ptr u_gi_cache_params2;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_gi_probe_params;
        gfx::program::uniform_ptr u_gi_probe_screen;
        gfx::program::uniform_ptr u_gi_probe_temporal;
        gfx::program::uniform_ptr u_gi_prev_view_proj;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_probe_history;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_resolve_params, "u_gi_resolve_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_trace, "u_gi_resolve_trace", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_camera, "u_gi_resolve_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_filter, "u_gi_resolve_filter", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params, "u_gi_cache_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params2, "u_gi_cache_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_screen, "u_gi_probe_screen", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_temporal, "u_gi_probe_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_prev_view_proj, "u_gi_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_probe_history, "s_probe_history", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } probe_trace_program_;

    /// False until the probe atlases and buffer hold a frame of real data; forces a full-weight
    /// write for one frame after any recreate so garbage never blends into the history.
    bool probe_history_valid_ = false;
    /// Probe lattice of the last traced frame. Reprojection indexes the READ half by the same
    /// layout, so a lattice change makes the whole history unaddressable and must reset it.
    uint32_t probe_grid_x_ = 0;
    uint32_t probe_grid_y_ = 0;

    struct probe_filter_program : uniforms_cache
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
    } probe_filter_program_;

    struct probe_integrate_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_probe_params;
        gfx::program::uniform_ptr u_gi_probe_screen;
        gfx::program::uniform_ptr u_gi_probe_temporal;
        gfx::program::uniform_ptr u_gi_integrate_camera;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_probe_radiance;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_probe_params, "u_gi_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_screen, "u_gi_probe_screen", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_probe_temporal, "u_gi_probe_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_integrate_camera, "u_gi_integrate_camera",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_probe_radiance, "s_probe_radiance", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } probe_integrate_program_;

    /// Probe SH + meta storage. A member rather than a render-view resource because it is a
    /// buffer, and its capacity only ever grows.
    gfx::dynamic_vertex_buffer_handle probe_buffer_{bgfx::kInvalidHandle};
    uint32_t probe_buffer_capacity_ = 0;

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
    static auto create_or_update_target(gfx::render_view& rview,
                                        const std::string& name,
                                        const usize32_t& size,
                                        gfx::texture::ptr& out_tex) -> gfx::frame_buffer::ptr;

    /// As @ref create_or_update_target, with a second attachment for luminance moments and the
    /// accumulation count. They share a framebuffer because they are written by one pass and must
    /// stay exactly in step -- a count that disagreed with its colour would corrupt the mean.
    static auto create_or_update_target_mrt(gfx::render_view& rview,
                                            const std::string& name,
                                            const usize32_t& size,
                                            gfx::texture::ptr& out_color,
                                            gfx::texture::ptr& out_moments) -> gfx::frame_buffer::ptr;

    /// Consecutive frames with no usable history. A couple is normal at startup and after a
    /// resize; a sustained run means accumulation is not happening at all.
    static constexpr uint32_t history_warning_frames = 120;
    uint32_t frames_without_history_ = 0;
};

} // namespace unravel
