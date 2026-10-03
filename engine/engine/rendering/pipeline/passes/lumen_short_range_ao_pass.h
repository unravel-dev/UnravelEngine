#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/lumen_run_params.h>

#include <graphics/render_view.h>
#include <graphics/texture.h>

namespace unravel
{

/**
 * @brief Lumen's short-range ambient occlusion (UE 5.8 ScreenSpaceShortRangeAOCS with the horizon search, accumulated
 *        as the screen probe gather's temporal accumulates it): the occlusion detail below the probe lattice, as a bent
 *        normal and a visibility per pixel.
 *
 * Lumen views composite it in place of the screen-space AO: UE applies no SSAO to Lumen GI while the short-range AO
 * is on (LumenDiffuseIndirect.cpp ShouldRenderAOWithLumenGI). A horizon search at LUMEN_SHORT_RANGE_AO_DOWNSAMPLE_FACTOR
 * (every pixel at Epic), then a full-resolution accumulation over the gather's reprojection taps with its weight. The constants and their UE sources are
 * in engine/rendering/gi/lumen_constants.h; the measurements in tasks/lumen_transform.
 */
class lumen_short_range_ao_pass
{
public:
    struct run_params
    {
        /// The gather's inputs this frame: the G-buffer, last frame's depth, the camera, the pre-exposure.
        const lumen_run_params* gather{};
        /// The gather's history this frame reads (a = frame count) and whether it holds last frame: the AO history
        /// is reprojected over its taps and blended with its weight.
        gfx::texture::ptr gather_history;
        bool has_gather_history{};
        /// The gather's u_lumen_frame, u_lumen_probes and u_lumen_view this frame (4 floats each).
        const float* frame{};
        const float* probes{};
        const float* view{};
        /// The noise follows the R2 sequence over frames (lumen_short_range_ao.sh); false = a per-frame hash.
        bool r2_noise = true;
    };

    auto init(rtti::context& ctx) -> bool;
    auto has_programs() const -> bool;

    /**
     * @brief Searches and accumulates this frame's AO.
     * @return The composite's screen AO (rgb = bent normal x 0.5 + 0.5, a = visibility), or null when the pass could
     *         not run.
     */
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr;

private:
    /// Every uniform of the two programs. bgfx uniforms are name-global, so one set serves both.
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_probes;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr u_lumen_settings;
        gfx::program::uniform_ptr u_lumen_short_range_ao;
        gfx::program::uniform_ptr u_lumen_prev_view_proj;
        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr s_lumen_depth;
        gfx::program::uniform_ptr s_lumen_normal;
        gfx::program::uniform_ptr s_lumen_short_range_ao;
        gfx::program::uniform_ptr s_lumen_short_range_ao_history;
        gfx::program::uniform_ptr s_lumen_history;
        gfx::program::uniform_ptr s_lumen_prev_depth;

        void cache_uniforms();
    } uniforms_;

    /// This frame's search target, the AO history ping-pong and the composite's output.
    struct frame_targets
    {
        gfx::texture::ptr search;
        gfx::texture::ptr history_read;
        gfx::texture::ptr history_write;
        gfx::texture::ptr screen;
        /// The read half holds last frame at this size.
        bool has_history{};
    };

    static auto acquire_targets(gfx::render_view& rview, const usize32_t& size) -> frame_targets;
    /// Sets the uniforms both programs read; @p has_history is the temporal's (the search ignores it).
    void set_frame_uniforms(const run_params& params, bool has_history) const;
    void run_search(const run_params& params, const frame_targets& targets, const usize32_t& size) const;
    void run_temporal(const run_params& params, const frame_targets& targets, const usize32_t& size) const;

    gpu_program::ptr search_program_;
    gpu_program::ptr temporal_program_;
};

} // namespace unravel
