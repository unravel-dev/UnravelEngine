#pragma once

#include <engine/rendering/gpu_program.h>

#include <context/context.hpp>

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>

namespace unravel
{

/**
 * @brief Zeroes a range of a uint buffer the GPU writes (cs_clear_uints.sc). bgfx updates no BGFX_BUFFER_COMPUTE_WRITE
 *        buffer from the CPU (its debug checks fail every such update, and the D3D11 backend did not create the buffer
 *        dynamic), so a frame's counters in one start over by a dispatch ordered before their first reader: earlier in
 *        the same view (bgfx runs a view's dispatches in submission order) or in an earlier view.
 *
 * Users: the Lumen gather's probe trace ray count and shader_print's header (the symbol count).
 */
class buffer_clear
{
public:
    /// Loads the clear program; false, with a warning, when it failed to load.
    auto init(rtti::context& ctx) -> bool;

    /// Whether the clear program can dispatch.
    auto is_ready() const -> bool;

    /**
     * @brief Dispatches the clear into view @p view_id: zeroes @p count uints of @p buffer from @p first and keeps the
     * rest. One thread per uint; dispatches nothing when the program is not ready, @p buffer is invalid or @p count is
     * 0. Call it before setting the next dispatch's state: it consumes the view's pending bindings.
     */
    void dispatch(uint16_t view_id, bgfx::DynamicIndexBufferHandle buffer, uint32_t first, uint32_t count) const;

private:
    struct clear_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_clear_uints;
        std::unique_ptr<gpu_program> program;
    };

    clear_program program_;
};

} // namespace unravel
