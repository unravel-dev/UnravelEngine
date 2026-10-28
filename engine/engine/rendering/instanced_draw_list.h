#pragma once

#include <engine/assets/asset_handle.h>
#include <engine/rendering/instanced_mesh_source.h>
#include <engine/rendering/model.h>

#include <hpp/span.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace unravel
{
class mesh;
class material;
class instanced_draw_list;

/**
 * @class instanced_cluster
 * @brief The instances of one draw on one ground tile. The pipeline culls each cluster against the camera and the
 * shadow cascades as one source, so instances spread over an arena draw only where a view reaches.
 */
class instanced_cluster : public instanced_mesh_source
{
public:
    auto get_mesh_bounds() const -> const math::bbox& override;
    void collect_mesh_instances(batch_collector& collector, bool with_previous) const override;
    auto collect_mesh_shadow_instances(std::vector<shadow_batch_collector>& collectors, uint8_t view_mask) const
        -> bool override;
    auto get_mesh_shadow_key() const -> uint64_t override;

    /// The layers the instances are on; a camera draws them when its render mask shares one.
    auto get_layer_mask() const -> int;
    /// True when the instances cast shadows.
    auto casts_shadow() const -> bool;

private:
    friend class instanced_draw_list;

    const instanced_draw_list* owner_ = nullptr;
    uint32_t draw_ = 0;
    /// The cluster's first instance within its draw's placed matrices, and how many follow.
    uint32_t first_ = 0;
    uint32_t count_ = 0;
    math::bbox bounds_;
};

/**
 * @class instanced_draw_list
 * @brief The meshes scripts draw as instances this frame (Graphics.DrawMeshInstanced).
 *
 * A draw copies its matrices in; before the frame renders they are grouped by ground tile into clusters the
 * pipeline draws in the G-buffer, velocity and shadow passes like mesh particles. The list empties when the frame
 * ends. The models built for (mesh, material) pairs are kept while draws use them. Main thread only.
 */
class instanced_draw_list
{
public:
    /// How one draw is drawn.
    struct draw_settings
    {
        asset_handle<mesh> render_mesh;
        /// The material of every slot of the mesh; unset, the mesh's own materials.
        asset_handle<material> render_material;
        int layer_mask = 1;
        bool casts_shadow = true;
    };

    /**
     * @brief Adds a draw of @p transforms.size() instances for this frame.
     * @param previous Last frame's matrices, for the motion of moving instances; empty, or as many as
     *        @p transforms. Without them the instances move with the camera only.
     */
    void add(const draw_settings& settings, hpp::span<const math::mat4> transforms, hpp::span<const math::mat4> previous);

    /**
     * @brief add() for matrices that need not be aligned like math::mat4, such as a script's array: 16 floats each,
     * column by column. @p previous is null or holds @p count matrices too.
     */
    void add_unaligned(const draw_settings& settings, const float* transforms, size_t count, const float* previous);

    /// This frame's clusters, built at the first call after a draw was added.
    auto get_clusters() -> const std::vector<instanced_cluster>&;

    /// Drops the frame's draws; models no draw used for a while are released too.
    void clear();

private:
    friend class instanced_cluster;

    /// A (mesh, material) pair built into a model, and what the instances need of the mesh.
    struct cached_model
    {
        model mesh_model;
        /// The loaded mesh it was built from (compared, never read).
        const mesh* source = nullptr;
        /// The mesh's node transform per submesh; empty when they are all identity.
        std::vector<math::mat4> node_transforms;
        /// Radius of a sphere around the mesh's origin that holds it, before scale.
        float mesh_radius = 0.0f;
        /// Frames since a draw last used it.
        uint32_t unused_frames = 0;
    };

    /// One add() call.
    struct draw
    {
        draw_settings settings;
        cached_model* model = nullptr;
        bool has_previous = false;
        /// Its matrices in the input arrays.
        uint32_t input_first = 0;
        uint32_t input_count = 0;
        /// Its first element in the placed arrays.
        size_t placed_first = 0;
    };

    auto find_model(const draw_settings& settings) -> cached_model*;
    void refresh_model(const draw_settings& settings, cached_model& cached) const;
    void build_clusters();
    void build_draw_clusters(uint32_t draw_index);
    auto get_stride(const draw& item) const -> uint32_t;

    std::vector<draw> draws_;
    std::vector<math::mat4> input_transforms_;
    std::vector<math::mat4> input_previous_;
    /// The matrices placed cluster after cluster: [instance * stride + submesh] within a draw's range.
    std::vector<math::mat4> placed_transforms_;
    std::vector<math::mat4> placed_previous_;
    std::vector<instanced_cluster> clusters_;
    std::unordered_map<std::string, cached_model> models_;
    bool are_clusters_dirty_ = false;
    /// Scratch of build_draw_clusters: the cluster of each tile, and of each instance.
    std::unordered_map<uint64_t, uint32_t> cluster_by_tile_;
    std::vector<uint32_t> instance_clusters_;
};

} // namespace unravel
