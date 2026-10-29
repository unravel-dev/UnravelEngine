#pragma once

#include <engine/rendering/batch_collector.h>
#include <math/math.h>

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace unravel
{
class mesh;
class material;

/// The layouts a source packs its instances in, once per frame for every pass that draws them.
enum class instanced_layout : uint8_t
{
    /// What the G-buffer and the shadow maps read (instance_vertex_data, baked_instance_vertex_data).
    base,
    /// What the velocity pass reads (velocity_instance_vertex_data, baked_velocity_instance_vertex_data).
    velocity,
    count
};

/// One submesh a source draws from packed instances, resolved once per frame.
struct instanced_part
{
    std::shared_ptr<mesh> mesh_ptr;
    std::shared_ptr<material> material_ptr;
    uint32_t submesh = 0;
    /// What the shadow maps draw it with; invalid when its material casts no shadow.
    shadow_batch_key shadow_key;
    /// Its bones come from the baked animation store (the baked layouts and programs); else a static mesh.
    bool is_baked = false;
    /// Baked parts: rows from an instance's frame row to the submesh's palette (baked_animation::get_palette_offset()).
    uint32_t palette_offset = 0;
    /// Baked parts: the submesh is rigid and follows the one bone of its node (baked_animation::is_rigid()).
    bool is_rigid = false;
};

/// A range of one part's packed instances, drawn as one instanced draw.
struct instanced_run
{
    const instanced_part* part = nullptr;
    /// The frame's instance buffer the range is in; a draw takes no more than it holds.
    const bgfx::InstanceDataBuffer* buffer = nullptr;
    uint32_t first = 0;
    uint32_t count = 0;
};

/**
 * @class instanced_run_list
 * @brief The runs one view draws. Ranges that continue each other in one buffer, such as neighbouring tiles of one
 * draw, merge into one draw.
 */
class instanced_run_list
{
public:
    void clear();
    void add(const instanced_run& run);
    /// Orders the runs by part and position and merges the ones that continue each other.
    void merge();
    auto get_runs() const -> const std::vector<instanced_run>&;
    auto empty() const -> bool;

private:
    std::vector<instanced_run> runs_;
};

/**
 * @class instanced_mesh_source
 * @brief Something that is not a model entity but draws many meshes as instances: mesh particles, scripted
 * instance draws (Graphics.DrawMeshInstanced).
 *
 * The deferred pipeline gathers the sources a camera sees and draws them in the G-buffer, velocity and shadow passes
 * without entities of their own. A source either adds loose instances to the batches its view's models fill, or adds
 * runs of instances it packed once for the frame, which every pass draws without packing them again.
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
    virtual void collect_mesh_instances(batch_collector& /*collector*/, bool /*with_previous*/) const
    {
    }

    /**
     * @brief Adds the shadow casting instances to the shadow views in @p view_mask (bit i = collectors[i]).
     * @return True when anything was collected.
     */
    virtual auto collect_mesh_shadow_instances(std::vector<shadow_batch_collector>& /*collectors*/,
                                               uint8_t /*view_mask*/) const -> bool
    {
        return false;
    }

    /**
     * @brief Adds the runs of this frame's packed instances in @p layout to @p runs. With the velocity layout only
     * instances that have last frame's matrices are added: the others move with the camera alone.
     */
    virtual void collect_mesh_runs(instanced_run_list& /*runs*/, instanced_layout /*layout*/) const
    {
    }

    /**
     * @brief Adds the shadow casting runs to the shadow views in @p view_mask (bit i = runs[i]).
     * @return True when anything was added.
     */
    virtual auto collect_mesh_shadow_runs(std::vector<instanced_run_list>& /*runs*/, uint8_t /*view_mask*/) const
        -> bool
    {
        return false;
    }

    /// What the shadows draw, as a key: a cached shadow map is reused while it does not change.
    [[nodiscard]] virtual auto get_mesh_shadow_key() const -> uint64_t = 0;
};

} // namespace unravel
