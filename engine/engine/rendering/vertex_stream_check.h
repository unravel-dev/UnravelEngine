#pragma once

#include <context/context.hpp>

#include <memory>

namespace unravel
{

/**
 * @brief A check of the vertex streams a morph deform pass draws from, run once by the launch option
 * --gfx-check-vertex-streams on the device the engine renders with.
 *
 * A compute shader (cs_vertex_stream_check.eshader), or the CPU, writes each vertex's position and basis into a
 * buffer of its own, read from a mesh-like vertex buffer and changed. A draw then reads that buffer as stream 0, the
 * mesh's own buffer as stream 1 under a layout that skips the four attributes stream 0 replaces, and a second
 * written buffer as stream 2 holding the previous positions (a_texcoord7). Its vertex shader compares every attribute
 * with what it must hold; the result is read back and logged once: per path (compute-written, CPU-written, and a
 * control that reads every attribute from one buffer), how many cells drew right, and which attributes came from the
 * wrong place.
 *
 * Owned by the renderer, which submits it once a frame until it is done.
 */
class vertex_stream_check
{
public:
    vertex_stream_check();
    ~vertex_stream_check();

    vertex_stream_check(const vertex_stream_check&) = delete;
    auto operator=(const vertex_stream_check&) -> vertex_stream_check& = delete;

    /**
     * @brief Records the check's work into views of the frame being built once its programs are ready, and nothing
     * while it waits for them or for the result. Call it before the frame's last view is recorded.
     */
    void submit(rtti::context& ctx);

    /// Whether the check logged its result or gave up; its owner then releases it.
    auto is_done() const -> bool;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace unravel
