#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/surface_cache_service.h>
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
        /// How far a gather ray travels before giving up and taking the environment instead.
        float max_distance = 200.0f;
        /// Lift off the surface before tracing. A ray starting exactly on the isosurface reads a
        /// distance of zero and stops immediately, reporting its own origin as an occluder.
        float normal_bias = 0.05f;
        /// Gain on the cached bounce. The environment fallback is deliberately left at probe
        /// intensity, so this scales the scene's own contribution only.
        float intensity = 1.0f;
        /// Range in which per-instance fields are traced. Beyond it the global cascade answers,
        /// which cannot represent anything thinner than its voxels but costs one lookup.
        float near_field_distance = 30.0f;
        int max_steps = 96;
        /// Hit acceptance, as a FRACTION OF A VOXEL of whichever field answered. An absolute
        /// distance is meaningless here because voxel size varies with bake resolution, instance
        /// scale and cascade.
        float surface_bias = 0.5f;
        float step_relaxation = 0.0f;
        /// Indirect diffuse is low frequency, so tracing below full resolution costs little.
        trace_resolution resolution = trace_resolution::half;

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
        float reprojection_tolerance = 0.03f;
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
        int denoise_passes = 3;
        /// Exponent on normal agreement. Higher keeps light from turning corners.
        float denoise_normal_power = 32.0f;
        /// Multiplier on the measured luminance standard deviation, forming the luminance
        /// edge-stop tolerance. Low values preserve more detail and filter less; the variance
        /// term already widens the tolerance wherever the estimate has not settled.
        float denoise_luma_phi = 4.0f;
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
        settings settings;
    };

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
        gfx::program::uniform_ptr u_gi_cache_params;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_resolve_params, "u_gi_resolve_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_trace, "u_gi_resolve_trace", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_resolve_camera, "u_gi_resolve_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params, "u_gi_cache_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } resolve_program_;

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
