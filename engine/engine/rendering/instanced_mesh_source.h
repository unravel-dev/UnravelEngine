#pragma once

#include <engine/rendering/batch_collector.h>
#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @class instanced_mesh_source
 * @brief Something that is not a model entity but draws many meshes as instances: mesh particles, scripted
 * instance draws (Graphics.DrawMeshInstanced).
 *
 * The deferred pipeline gathers the sources a camera sees and adds their instances to the same batches its
 * models fill, so they draw in the G-buffer, velocity and shadow passes without entities of their own.
 */
class instanced_mesh_source
{
public:
    virtual ~instanced_mesh_source() = default;

    /// The world box the instances fill this frame (not populated when there are none).
    [[nodiscard]] virtual auto get_mesh_bounds() const -> const math::bbox& = 0;

    /**
     * @brief Adds this frame's instances to @p collector, with last frame's matrices when @p with_previous (the
     * velocity pass draws those). The matrices must stay alive until the frame's batches are finished.
     */
    virtual void collect_mesh_instances(batch_collector& collector, bool with_previous) const = 0;

    /**
     * @brief Adds the shadow casting instances to the shadow views in @p view_mask (bit i = collectors[i]).
     * @return True when anything was collected.
     */
    virtual auto collect_mesh_shadow_instances(std::vector<shadow_batch_collector>& collectors, uint8_t view_mask) const
        -> bool = 0;

    /// What the shadows draw, as a key: a cached shadow map is reused while it does not change.
    [[nodiscard]] virtual auto get_mesh_shadow_key() const -> uint64_t = 0;
};

} // namespace unravel
