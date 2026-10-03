#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/gi_resolve_pass.h>

#include <graphics/texture.h>

#include <array>

namespace unravel
{

/**
 * @brief The Lumen radiance cache (UE 5.8 LumenRadianceCache, analysis chapter f): sparse, world-locked
 *        radiance probes on 4 camera-centred clipmaps of 48^3 cells (1.04 m doubling), marked by the
 *        screen probes that will read them, new probes traced the frame they appear, the rest re-traced
 *        oldest first within a fixed budget, filtered over their face neighbours and stored with an
 *        octahedral border for bilinear reads.
 *
 * Owned by lumen_gather_pass, which updates it after the probe placement and binds it to the probe trace
 * for the hand-off. The probes' rays read the light voxels at hits (the surface cache's stand-in).
 */
class lumen_radiance_cache
{
public:
    /// The screen-probe layout the marking pass reads (lumen_common.sh uniforms).
    struct frame_inputs
    {
        const gi_resolve_pass::run_params* params{};
        gfx::texture::ptr probe_records;
        uint32_t probes_x{};
        /// Rows of the probe atlas: the uniform probes and the adaptive ones below them all mark.
        uint32_t probe_rows{};
        const float* frame{};
        const float* probes{};
        const float* view{};
    };

    ~lumen_radiance_cache();

    auto init(rtti::context& ctx) -> bool;

    /// Updates the cache for this frame. False when it could not run; the gather then traces without it.
    auto update(const frame_inputs& inputs) -> bool;

    /// Binds the cache for the hand-off read (lumen_radiance_cache_sample.sh) at the given stages and sets
    /// the clipmap uniforms.
    void bind_for_sampling(uint8_t indirection_stage, uint8_t final_stage, uint8_t depth_stage) const;

private:
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_lumen_rc_clipmaps;
        gfx::program::uniform_ptr u_lumen_rc_prev_clipmaps;
        gfx::program::uniform_ptr u_lumen_rc_params;
        gfx::program::uniform_ptr u_lumen_rc_camera;
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_probes;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_lumen_hit_lighting;
        gfx::program::uniform_ptr s_lumen_probe_records;
        gfx::program::uniform_ptr s_lumen_env_sh;
        gfx::program::uniform_ptr s_lumen_rc_radiance;
        gfx::program::uniform_ptr s_lumen_rc_depth;
        gfx::program::uniform_ptr s_lumen_rc_final;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_sdf_clipmap_coverage;

        void cache_uniforms();
    } uniforms_;

    /// The bookkeeping pass's modes (cs_lumen_rc_bookkeeping.sc).
    enum class bookkeeping : int
    {
        reset = 0,
        frame_start = 1,
        budget = 2,
        trace_list = 3,
        trace_args = 4,
    };

    auto has_programs() const -> bool;
    auto ensure_resources() -> bool;
    void release_resources();
    /// Snaps the clipmaps around the camera (double precision) and keeps last frame's for the carry-over.
    void place_clipmaps(const math::vec3& camera);
    /// Sets the cache uniforms every pass reads, with @p mode for the bookkeeping pass.
    void set_cache_uniforms(bookkeeping mode) const;
    void run_bookkeeping(bookkeeping mode) const;
    /// One dispatch over every indirection entry.
    void run_over_indirection(gpu_program& program, const char* name) const;
    void run_mark(const frame_inputs& inputs) const;
    void run_trace(const frame_inputs& inputs) const;
    void run_filter() const;

    gpu_program::ptr clear_program_;
    gpu_program::ptr mark_program_;
    gpu_program::ptr update_program_;
    gpu_program::ptr bookkeeping_program_;
    gpu_program::ptr allocate_program_;
    gpu_program::ptr select_program_;
    gpu_program::ptr tiles_program_;
    gpu_program::ptr trace_program_;
    gpu_program::ptr filter_program_;

    /// Ping-pong indirection: this frame's and last frame's.
    std::array<bgfx::DynamicIndexBufferHandle, 2> indirection_{bgfx::DynamicIndexBufferHandle{bgfx::kInvalidHandle},
                                                               bgfx::DynamicIndexBufferHandle{bgfx::kInvalidHandle}};
    bgfx::DynamicIndexBufferHandle probe_state_{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle counters_{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle traces_{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle tiles_{bgfx::kInvalidHandle};
    bgfx::IndirectBufferHandle args_{bgfx::kInvalidHandle};
    gfx::texture::ptr radiance_atlas_;
    gfx::texture::ptr depth_atlas_;
    gfx::texture::ptr final_atlas_;

    /// Per clipmap: xyz = corner of cell (0, 0, 0), w = cell size; this frame's and last frame's.
    std::array<float, 16> clipmaps_{};
    std::array<float, 16> prev_clipmaps_{};
    /// xyz = this frame's camera position (the trace cost and the angular level depend on the distance).
    std::array<float, 4> camera_{};
    /// The index of this frame's indirection in indirection_.
    uint32_t current_ = 0;
    /// Cache frames since the start (0 = never updated); the probes' last-used / last-traced stamps.
    uint32_t frame_ = 0;
    /// False until a frame has run with these resources: the next frame then starts from scratch.
    bool persistent_ = false;
};

} // namespace unravel
