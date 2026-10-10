#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/lumen_pass_common.h>
#include <engine/rendering/pipeline/passes/lumen_run_params.h>

#include <graphics/texture.h>

#include <bgfx/bgfx.h>

#include <cstdint>

namespace unravel
{

/**
 * @brief The adaptive screen probes (cs_lumen_adaptive_mark.sc, cs_lumen_adaptive_spawn.sc, cs_lumen_adaptive_args.sc):
 *        extra screen probes where the uniform probes cannot interpolate a pixel (thin features, silhouettes, depth
 *        discontinuities), placed in the probe atlas rows below the uniform probes, and the dispatch arguments every
 *        per-probe pass runs with over the probes placed this frame.
 *
 * Owned by lumen_gather_pass. The uniform placement clears the adaptive state each frame (cs_lumen_probe_place.sc);
 * run() then marks and spawns this frame's adaptive probes and writes the arguments. The state buffer's layout is
 * lumen_adaptive_probes.sh's; the integrate reads its tile lists.
 */
class lumen_adaptive_probes
{
public:
    /// The dispatch argument slots run() writes (cs_lumen_adaptive_args.sc).
    enum args_slot : uint16_t
    {
        /// One group per probe.
        args_group_per_probe = 0,
        /// 8x8 thread groups over the bordered radiance texels.
        args_border_threads = 1,
        /// 8x8 thread groups over the probes.
        args_thread_per_probe = 2,
        args_slot_count = 3,
    };

    /// The frame's probe layout and inputs (lumen_common.sh uniforms).
    struct frame_inputs
    {
        const lumen_run_params* params{};
        gfx::texture::ptr probe_records;
        uint32_t probes_x{};
        uint32_t probes_y{};
        /// The adaptive probes this frame can hold (get_capacity).
        uint32_t capacity{};
        /// Texels per axis of a probe's bordered radiance (args_border_threads).
        uint32_t border_resolution{};
        const float* frame{};
        const float* probes{};
        const float* view{};
        /// False places none (an A/B): run() only writes the dispatch arguments over the uniform probes.
        bool place = true;
        /// The candidates per uniform probe (the GI quality tier's).
        lumen_pass::adaptive_probe_layout layout;
    };

    ~lumen_adaptive_probes();

    auto init(rtti::context& ctx) -> bool;
    auto has_programs() const -> bool;
    /// Creates the dispatch arguments once and the state buffer for @p uniform_tiles uniform probes, growing it when a
    /// view needs more; false when they could not be created.
    auto ensure_resources(uint32_t uniform_tiles) -> bool;

    /// Marks and spawns this frame's adaptive probes, then writes the per-probe dispatch arguments.
    void run(const frame_inputs& inputs) const;
    /// Binds the state buffer (lumen_adaptive_probes.sh) at @p stage.
    void bind_state(uint8_t stage, bgfx::Access::Enum access) const;
    /// Dispatches @p program over this frame's probes with the arguments of @p slot.
    void dispatch(bgfx::ViewId view, const gpu_program& program, args_slot slot) const;

    /// The adaptive probes a uniform lattice of @p probes_x x @p probes_y can hold.
    static auto get_capacity(uint32_t probes_x, uint32_t probes_y) -> uint32_t;

private:
    /// Every uniform of the three programs. bgfx uniforms are name-global, so one set serves all.
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_probes;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr u_lumen_adaptive;
        gfx::program::uniform_ptr s_lumen_depth;
        gfx::program::uniform_ptr s_lumen_normal;
        gfx::program::uniform_ptr s_lumen_probe_records;

        void cache_uniforms();
    } uniforms_;

    void set_uniforms(const frame_inputs& inputs) const;
    void run_mark(const frame_inputs& inputs) const;
    void run_spawn(const frame_inputs& inputs) const;
    void run_args(const frame_inputs& inputs) const;

    gpu_program::ptr mark_program_;
    gpu_program::ptr spawn_program_;
    gpu_program::ptr args_program_;
    bgfx::DynamicIndexBufferHandle state_{bgfx::kInvalidHandle};
    /// The uniform tiles state_ has room for.
    uint32_t state_tiles_ = 0;
    bgfx::IndirectBufferHandle args_{bgfx::kInvalidHandle};
};

} // namespace unravel
