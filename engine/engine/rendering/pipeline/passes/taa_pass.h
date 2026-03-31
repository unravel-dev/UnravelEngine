#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <base/basetypes.hpp>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>

namespace unravel
{

/**
 * @brief HDR temporal anti-aliasing resolve (depth reprojection). Phase 2: optional velocity buffer.
 */
class taa_pass
{
public:
    struct settings
    {
        /// Enable jitter when greater than 1 (full render frame index drives the sequence).
        std::uint32_t temporal_sample_count = 8;
        /// Subpixel offset sequence (see @c taa_jitter_mode); passed to camera each frame.
        taa_jitter_mode jitter_mode = taa_jitter_mode::msaa_2_rotating;
        /// Scales jitter before projection (1 = full ~±½ pixel). Lower = less whole-screen shake, softer AA.
        float jitter_amplitude = 1.0f;
        /// For golden / Halton / R2: multiplies frame index inside the sequence (1 = original step size).
        /// MSAA rotating modes ignore this. Lower (0.25–0.55) walks the pattern more slowly for smoother TAA.
        float jitter_temporal_phase_scale = 0.45f;
        /// Blend toward clipped history (lower = less gray cast, slightly more aliasing).
        float history_blend = 0.82f;
        /// Unsharp on current neighborhood; keep low to avoid edge shimmer.
        float sharpen = 0.5f;
        float depth_reject_scale = 1.0f;
        /// Standard deviations for RGB variance clip (typical 1.0–1.75).
        float variance_clip_scale = 1.35f;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        gfx::frame_buffer::ptr output;
        const camera* cam = nullptr;
        gfx::frame_buffer::ptr g_buffer;
        settings config{};
    };

    auto init(rtti::context& ctx) -> bool;

    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    void release_resources(gfx::render_view& rview);

private:
    auto create_or_update_history_tex(gfx::render_view& rview,
                                      const gfx::frame_buffer::ptr& reference_color) -> gfx::texture::ptr;

    auto create_or_update_temp_fb(gfx::render_view& rview,
                                  const gfx::frame_buffer::ptr& reference_color) -> gfx::frame_buffer::ptr;

    struct taa_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_curr, "s_curr", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_history, "s_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_prev_view_proj, "u_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), u_taa_params, "u_taa_params", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_curr;
        gfx::program::uniform_ptr s_history;
        gfx::program::uniform_ptr s_depth;
        gfx::program::uniform_ptr u_prev_view_proj;
        gfx::program::uniform_ptr u_taa_params;
        std::unique_ptr<gpu_program> program;
    } program_;
};

} // namespace unravel
