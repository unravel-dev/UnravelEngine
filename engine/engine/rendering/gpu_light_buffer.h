#pragma once

#include <engine/engine_export.h>

#include <graphics/graphics.h>
#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

class scene;

/**
 * @brief Every active light in the scene, in a buffer a shader can enumerate.
 *
 * The deferred path draws one fullscreen pass per light with that light's parameters in
 * uniforms, which works for shading the G-buffer but cannot answer "how much light reaches
 * world point P" from inside a compute or tracing shader -- there is no light to enumerate,
 * only the one currently bound. Global illumination needs exactly that question answered at
 * arbitrary points along a traced ray, so the lights have to be resident as data.
 *
 * Populated from EVERY active light, deliberately including ones outside the view frustum:
 * a light behind the camera still illuminates surfaces that bounce into it, and culling here
 * would reintroduce the view dependence the whole world-space approach exists to remove.
 */
class gpu_light_buffer
{
public:
    /// vec4 elements per light. Must match GPU_LIGHT_STRIDE in gi/gpu_lights.sh.
    static constexpr uint32_t light_vec4_stride = 4;

    /// Mirrors light_type; kept explicit because the value is packed into the buffer and read
    /// by shader code that cannot see the C++ enum.
    enum class gpu_light_type : uint32_t
    {
        spot = 0,
        point = 1,
        directional = 2,
    };

    auto init() -> bool;
    void shutdown();

    auto is_valid() const -> bool
    {
        return bgfx::isValid(buffer_);
    }

    /**
     * @brief Rebuilds the buffer from the scene's active lights and uploads it.
     */
    void update(scene& scn);

    auto get_buffer() const -> gfx::dynamic_vertex_buffer_handle
    {
        return buffer_;
    }

    /// Order-independent-enough hash of the uploaded light data (GI v2 plan section 8): the
    /// world probes compare it frame to frame and halve their refresh window while it changes,
    /// so a moved or toggled light propagates through the bounce chain at double speed.
    auto get_content_hash() const -> uint64_t
    {
        return content_hash_;
    }

    auto get_light_count() const -> uint32_t
    {
        return light_count_;
    }

private:
    void ensure_capacity(uint32_t required_vec4);

    gfx::dynamic_vertex_buffer_handle buffer_{bgfx::kInvalidHandle};
    uint32_t capacity_vec4_ = 0;
    uint32_t light_count_ = 0;
    std::vector<float> data_;
    uint64_t content_hash_ = 0;
    /// Whether the current buffer object holds the bytes content_hash_ describes. Cleared on
    /// recreate: an unchanged hash must still upload into a fresh buffer.
    bool buffer_uploaded_ = false;
};

} // namespace unravel
