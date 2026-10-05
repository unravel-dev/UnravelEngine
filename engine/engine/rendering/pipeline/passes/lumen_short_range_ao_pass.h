#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/lumen_pass_common.h>
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
 * (every pixel at Epic) here, then the gather's integrate accumulates it at full resolution over its own reprojection
 * taps and history length (cs_lumen_integrate_ao.sc), as UE's temporal does. The constants and their UE sources are
 * in engine/rendering/gi/lumen_constants.h; the measurements in tasks/lumen_transform.
 */
class lumen_short_range_ao_pass
{
public:
    struct run_params
    {
        /// The gather's inputs this frame: the G-buffer and the camera.
        const lumen_run_params* gather{};
        /// The gather's u_lumen_frame, u_lumen_probes and u_lumen_view this frame (4 floats each).
        const float* frame{};
        const float* probes{};
        const float* view{};
        /// The noise follows the R2 sequence over frames (lumen_short_range_ao.sh); false = a per-frame hash.
        bool r2_noise = true;
    };

    /// This frame's search, the AO history ping-pong the integrate accumulates into and the composite's screen AO
    /// (rgb = bent normal x 0.5 + 0.5, a = visibility).
    struct frame_targets
    {
        gfx::texture::ptr search;
        gfx::texture::ptr history_read;
        gfx::texture::ptr history_write;
        gfx::texture::ptr screen;
        /// The read half holds last frame at this size.
        bool has_history{};
    };

    /// Creates the uniforms and the search program. The gather calls it before it creates the integrate programs,
    /// which read these uniforms (the OpenGL renderer wires a program's uniforms at link time).
    auto init(rtti::context& ctx) -> bool;
    auto has_programs() const -> bool;

    /// Searches this frame's AO; the returned targets have no search texture when the pass could not run.
    auto run_search(gfx::render_view& rview, const run_params& params) -> frame_targets;

    /**
     * @brief Binds the AO's part of the gather's integrate (cs_lumen_integrate_ao.sc): the search at stage 14, last
     *        frame's accumulation at 15, the accumulation and the screen AO as images 5 and 7, and the uniform.
     * @param has_gather_history The integrate's histories hold last frame (its taps are valid).
     */
    void bind_accumulation(const frame_targets& targets, const run_params& params, bool has_gather_history) const;

private:
    /// The uniforms of the search and of the integrate's AO. bgfx uniforms are name-global, so one set serves both.
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_probes;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr u_lumen_settings;
        gfx::program::uniform_ptr u_lumen_short_range_ao;
        gfx::program::uniform_ptr s_lumen_depth;
        gfx::program::uniform_ptr s_lumen_normal;
        gfx::program::uniform_ptr s_lumen_short_range_ao;
        gfx::program::uniform_ptr s_lumen_short_range_ao_history;

        void cache_uniforms();
    } uniforms_;

    static auto acquire_targets(gfx::render_view& rview, const usize32_t& size) -> frame_targets;
    /// Sets u_lumen_short_range_ao: x = @p has_history, y = the R2 noise.
    void set_short_range_ao_uniform(const run_params& params, bool has_history) const;

    gpu_program::ptr search_program_;
};

} // namespace unravel
