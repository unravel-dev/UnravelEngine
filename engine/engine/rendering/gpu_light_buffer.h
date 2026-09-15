#pragma once

#include <engine/engine_export.h>

#include <graphics/graphics.h>
#include <math/math.h>

#include <array>
#include <cstdint>
#include <unordered_map>
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
    /// A directional light's brightness (colour luminance x intensity) must change by more than
    /// this factor for the change to count as GLOBAL - Lumen's sun / sky rule (its scene
    /// rendering resets the caches on a >4x change). Smaller changes, direction changes and every
    /// local light change propagate through the normal refresh cadence
    /// (tasks/lumen57_deep_dive_2026-09-14.md plan item 1.2). The deferred irradiance pass applies
    /// the same ratio to the sky's environment revision.
    static constexpr float global_change_ratio = 4.0f;
    /// A local (point or spot) light's change: an influence sphere it lit before or lights now,
    /// keyed by the light's entity so a light that keeps changing extends one dirty region
    /// instead of opening a new one every frame.
    struct local_change
    {
        uint32_t light_id = 0;
        math::bbox bounds{};
    };

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
    /// Revision of GLOBAL lighting changes: bumped when a directional light appears, disappears
    /// or changes brightness past global_change_ratio. The world probes' fast window, the
    /// relight's EMA snap and the screen temporal's scene-wide fast cap key on this; the content
    /// hash only wakes the quiescence gate.
    auto get_global_revision() const -> uint64_t
    {
        return global_revision_;
    }
    /// Local light changes of the last update (see local_change); the surface cache turns them
    /// into dirty regions.
    auto get_local_changes() const -> const std::vector<local_change>&
    {
        return local_changes_;
    }

    auto get_light_count() const -> uint32_t
    {
        return light_count_;
    }

private:
    void ensure_capacity(uint32_t required_vec4);
    /// Compares this update's lights with the previous update's, by entity, and fills
    /// global_revision_ and local_changes_.
    void classify_changes();

    gfx::dynamic_vertex_buffer_handle buffer_{bgfx::kInvalidHandle};
    uint32_t capacity_vec4_ = 0;
    uint32_t light_count_ = 0;
    std::vector<float> data_;
    uint64_t content_hash_ = 0;
    /// One light's packed GPU record (light_vec4_stride vec4s), by entity id: this update's and
    /// the previous update's.
    using light_record = std::array<float, light_vec4_stride * 4u>;
    std::unordered_map<uint32_t, light_record> current_lights_;
    std::unordered_map<uint32_t, light_record> previous_lights_;
    std::vector<local_change> local_changes_;
    uint64_t global_revision_ = 0;
    /// Whether the current buffer object holds the bytes content_hash_ describes. Cleared on
    /// recreate: an unchanged hash must still upload into a fresh buffer.
    bool buffer_uploaded_ = false;
};

} // namespace unravel
