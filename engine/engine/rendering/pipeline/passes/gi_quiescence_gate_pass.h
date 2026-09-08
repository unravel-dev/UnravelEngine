#pragma once

#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/render_view.h>

#include <array>

namespace unravel
{

/**
 * @brief Publishes the GI quiescence verdict as indirect dispatch arguments, so the gated
 *        passes are skipped on the GPU without a CPU readback.
 *
 * WHY THIS EXISTS. The convergence half of the gate (surface_cache_view::update_quiescence)
 * needs a statistic only the GPU can produce: the mean relative change per relit face. That
 * statistic used to reach the CPU through a staging blit plus gfx::read_texture, which moved
 * 32 bytes and cost about a GPU frame of render-thread time per frame the gate was open -
 * bgfx::readTexture advertises frameNum + 2 latency but every desktop backend implements it
 * as a blocking sync (D3D11 Map without DO_NOT_WAIT, D3D12 CopyTextureRegion + finish,
 * Vulkan kick(true), GL glGetTextureSubImage), run in the post-command buffer AFTER the
 * frame's submit. So the render thread waited for GPU idle at the tail of every moving
 * frame, to save the ~0.8 ms the gate skips in a parked one.
 *
 * The kernel here reads the same statistic in place, keeps the sample ring in a GPU buffer,
 * applies the same two convergence tests, and writes each gated dispatch's group counts (or
 * zeros). Nothing crosses back to the CPU, and the verdict is one frame behind the relight
 * instead of three.
 *
 * FALLBACK. Backends without BGFX_CAPS_DRAW_INDIRECT - and any failure to build the program
 * or the buffers - leave run() returning false, and the pipeline uses the readback path
 * unchanged. Both paths are live; neither is a reimplementation of the other's decision,
 * because the CPU half stays in update_quiescence either way.
 */
class gi_quiescence_gate_pass
{
public:
    /// The gated dispatches, in the order this pass writes their indirect entries. Kept in
    /// one place because the gate must know every count before the passes themselves run.
    enum entry : uint16_t
    {
        entry_light_voxels = 0,
        entry_probe_trace = 1,
        entry_probe_convolve = 2,
        entry_count = 3,
    };
    /// Mirrors GI_GATE_ENTRY_COUNT in cs_gi_quiescence_gate.sc, which sizes the uniform array
    /// this pass fills.
    static constexpr uint16_t shader_entry_count = 3;
    static_assert(entry_count == shader_entry_count,
                  "GI_GATE_ENTRY_COUNT in cs_gi_quiescence_gate.sc must match entry_count");

    /// A gated dispatch's group counts, as the gate writes them into its indirect entry.
    struct dispatch_groups
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t z = 0;
    };

    struct run_params
    {
        surface_cache_view* view_cache = nullptr;
        /// The CPU half of the decision (surface_cache_view::update_quiescence).
        surface_cache_view::quiescence_mode mode = surface_cache_view::quiescence_mode::run;
        /// A tracked input changed this frame: the GPU sample ring is stale and is cleared.
        bool reset = false;
        /// Group counts for each entry, indexed by @ref entry. Supplied by the passes'
        /// static accessors so the derivation has a single home.
        std::array<dispatch_groups, entry_count> groups{};
    };

    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Dispatches the gate kernel and writes this frame's indirect arguments.
     *
     * @return True when the indirect buffer now describes this frame's dispatches, so the
     *         gated passes must dispatch indirectly from it. False means no GPU gate is
     *         available and the caller owns the decision (the readback path).
     */
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

    /// The buffer the gated passes dispatch from; only valid after a run() that returned true.
    auto get_indirect_buffer() const -> gfx::indirect_buffer_handle
    {
        return indirect_;
    }

    /// Whether this backend and build can gate on the GPU at all. False sends the pipeline
    /// down the readback path for the whole session.
    auto is_available() const -> bool;

    ~gi_quiescence_gate_pass();

private:
    /// Allocates the indirect and ring buffers on first use. Separate from init() because a
    /// pass that is never run on a backend without indirect support must not allocate.
    auto ensure_buffers() -> bool;

    struct gate_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_gi_gate_params;
        gfx::program::uniform_ptr u_gi_gate_groups;

        void cache_uniforms()
        {
            cache_uniform(program.get(),
                          u_gi_light_voxel_params,
                          "u_gi_light_voxel_params",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_gate_params, "u_gi_gate_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_gi_gate_groups,
                          "u_gi_gate_groups",
                          gfx::uniform_type::Vec4,
                          entry_count);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    };

    gate_program program_;
    gfx::indirect_buffer_handle indirect_{bgfx::kInvalidHandle};
    /// The sample ring: two header slots (count, head) then the samples as float bits. Never
    /// written by the CPU - bgfx forbids updating a compute-writable buffer - so the kernel
    /// clears it from the reset lane instead.
    gfx::dynamic_index_buffer_handle ring_{bgfx::kInvalidHandle};
    /// Slots the ring buffer holds: the header plus both compared windows.
    static constexpr uint32_t ring_header_slots = 2u;
    static constexpr uint32_t ring_sample_slots =
        uint32_t(gi::GI_QUIESCENCE_COMPARE_FRAMES) + uint32_t(gi::GI_QUIESCENCE_WINDOW_FRAMES);
    /// The memo texture the ring was last fed from: a fresh allocation carries an unwritten
    /// statistics slice, so the first frame against a new one resets the ring.
    const gfx::texture* stats_source_ = nullptr;
    bool unavailable_warning_emitted_ = false;
};

} // namespace unravel
