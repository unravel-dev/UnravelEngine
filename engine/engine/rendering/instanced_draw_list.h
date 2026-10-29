#pragma once

#include <engine/assets/asset_handle.h>
#include <engine/rendering/baked_animation.h>
#include <engine/rendering/instanced_mesh_source.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/model.h>

#include <hpp/span.hpp>
#include <hpp/uuid.hpp>

#include <cstdint>
#include <memory>
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
    void collect_mesh_runs(instanced_run_list& runs, instanced_layout layout) const override;
    auto collect_mesh_shadow_runs(std::vector<instanced_run_list>& runs, uint8_t view_mask) const -> bool override;
    auto get_mesh_shadow_key() const -> uint64_t override;

    /// The layers the instances are on; a camera draws them when its render mask shares one.
    auto get_layer_mask() const -> int;
    /// True when the instances cast shadows.
    auto casts_shadow() const -> bool;

private:
    friend class instanced_draw_list;

    /// The cluster's instances of the draw list's part @p part in @p layout.
    auto make_run(uint32_t part, instanced_layout layout) const -> instanced_run;

    const instanced_draw_list* owner_ = nullptr;
    uint32_t draw_ = 0;
    /// The cluster's first instance within its draw's placed instances, and how many follow.
    uint32_t first_ = 0;
    uint32_t count_ = 0;
    /// The ground tile. A draw places its clusters in tile order, so the runs of neighbouring tiles merge.
    uint64_t tile_ = 0;
    math::bbox bounds_;
};

/**
 * @class instanced_draw_list
 * @brief The meshes scripts draw as instances this frame (Graphics.DrawMeshInstanced).
 *
 * A draw copies its matrices in; before the frame renders they are grouped by ground tile into clusters the
 * pipeline draws in the G-buffer, velocity and shadow passes. Each layout of the frame's instances is packed once into
 * the frame's instance buffers (prepare()), and every pass draws ranges of it. The draws last until clear(), the
 * packed data one frame (end_frame()). The models built for (mesh, material) pairs are kept while draws use them.
 * Main thread only.
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
        /// A material instance of render_material that every slot draws in its place; null draws render_material.
        std::shared_ptr<material_instance> render_material_instance;
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

    /**
     * @brief add_unaligned() for instances that play @p animation, the mesh it baked (settings.render_mesh is ignored):
     * each shows @p states[i] (clip and time), and last frame showed @p previous_states[i] for the velocity pass.
     * Skinned submeshes take their bones from the bake, rigid submeshes ride their animated nodes.
     * @param previous_states Null, or @p count states.
     */
    void add_baked(const draw_settings& settings,
                   std::shared_ptr<const baked_animation> animation,
                   const float* transforms,
                   const baked_animation_state* states,
                   size_t count,
                   const float* previous,
                   const baked_animation_state* previous_states);

    /// This frame's clusters, built at the first call after a draw was added.
    auto get_clusters() -> const std::vector<instanced_cluster>&;

    /**
     * @brief Packs this frame's instances in @p layout into the frame's instance buffers, which the clusters' runs
     * draw from; later calls of the frame do nothing. Baked parts are packed when @p with_baked (the device draws
     * baked animation), static parts always.
     */
    void prepare(instanced_layout layout, bool with_baked);

    /**
     * @brief How many instances the packed data of the baked or of the static parts holds in @p layout: one block
     * per part matrix. The velocity layout holds only the draws with last frame's matrices.
     */
    auto get_packed_count(instanced_layout layout, bool is_baked) const -> uint32_t;

    /// The bytes per instance of the packed data of the baked or of the static parts in @p layout.
    static auto get_packed_stride(instanced_layout layout, bool is_baked) -> uint16_t;

    /**
     * @brief Writes the packed data of the baked or of the static parts in @p layout as prepare() packs it: instance
     * i at @p out + i * get_packed_stride(), for i below @p count (at most get_packed_count()).
     */
    void write_packed(instanced_layout layout, bool is_baked, uint8_t* out, uint32_t count) const;

    /// Drops the draws; models no draw used for a while are released too.
    void clear();

    /// Ends the frame: the draws stay until clear(), and the next frame packs them again into its own buffers.
    void end_frame();

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

    /// The baked frames an instance shows now and showed last frame.
    struct baked_frames
    {
        uint32_t current = 0;
        uint32_t previous = 0;
    };

    /**
     * @brief The packed instances of one draw that parts share: one per instance, in placement order, starting at
     * `first` of a layout in the packed data of its kind. Every part of a baked draw shares one block: the shaders add
     * the part's palette to the instance's frame row.
     */
    struct packed_block
    {
        bool is_baked = false;
        /// The placed matrix of an instance it packs: instance * stride + slot within the draw's range.
        uint32_t slot = 0;
        /// By layout; the velocity layout packs only the draws with last frame's matrices.
        uint32_t first[size_t(instanced_layout::count)] = {};
    };

    /// A part the clusters' runs draw, and its block in blocks_.
    struct packed_part
    {
        instanced_part part;
        uint32_t block = 0;
    };

    /// One add() call.
    struct draw
    {
        draw_settings settings;
        cached_model* model = nullptr;
        /// Set for add_baked().
        std::shared_ptr<const baked_animation> animation;
        bool has_previous = false;
        /// Its matrices in the input arrays.
        uint32_t input_first = 0;
        uint32_t input_count = 0;
        /// Its first element in the placed arrays.
        size_t placed_first = 0;
        /// Its parts in parts_ and its blocks in blocks_.
        uint32_t first_part = 0;
        uint32_t part_count = 0;
        uint32_t first_block = 0;
        uint32_t block_count = 0;
    };

    /// The packed data of one layout and kind of part for the frame.
    struct packed_buffer
    {
        bgfx::InstanceDataBuffer buffer{};
        bool is_packed = false;
    };

    /// A (mesh, material) pair by asset uid.
    struct model_key
    {
        hpp::uuid mesh_uid;
        hpp::uuid material_uid;

        auto operator==(const model_key& other) const -> bool
        {
            return mesh_uid == other.mesh_uid && material_uid == other.material_uid;
        }
    };

    struct model_key_hash
    {
        auto operator()(const model_key& key) const noexcept -> std::size_t;
    };

    static constexpr size_t PART_KIND_COUNT = 2;

    auto find_model(const draw_settings& settings) -> cached_model*;
    void refresh_model(const draw_settings& settings, cached_model& cached) const;
    void build_clusters();
    void build_draw_clusters(uint32_t draw_index);
    /// Sorts the clusters a draw added from @p first_cluster on by tile and points instance_clusters_ at their new
    /// places.
    void sort_draw_clusters(size_t first_cluster);
    /// Places a baked draw's instance at @p slot: its matrices and its frames.
    void place_baked_instance(const draw& item, size_t slot, uint32_t instance);
    auto get_stride(const draw& item) const -> uint32_t;
    /// Resolves the parts @p item draws and the blocks they read.
    void build_draw_parts(draw& item);
    /// Adds @p part, drawn from the block of @p item that packs what @p wanted packs.
    void add_part(draw& item, instanced_part part, const packed_block& wanted);
    /// The block of @p item that packs what @p wanted packs, added when there is none yet.
    auto find_or_add_block(const draw& item, const packed_block& wanted) -> uint32_t;
    /// The change keys of what @p item's slots draw: its material instance's resolved material, else its model's.
    static auto get_material_change_keys(const draw& item) -> render_change_values;
    /// Forgets the frame's packed data; the counts and blocks stay.
    void reset_packed();
    void pack(instanced_layout layout, bool is_baked);
    void write_block(const draw& item, const packed_block& block, instanced_layout layout, uint8_t* out, uint32_t count)
        const;
    /// Writes the data in @p layout of @p item's instance whose first placed index is @p first to @p target.
    void write_static_instance(const packed_block& block, instanced_layout layout, size_t first, uint8_t* target) const;
    void write_baked_instance(const draw& item,
                              const packed_block& block,
                              instanced_layout layout,
                              size_t first,
                              uint8_t* target) const;

    std::vector<draw> draws_;
    std::vector<math::mat4> input_transforms_;
    std::vector<math::mat4> input_previous_;
    std::vector<baked_frames> input_frames_;
    /// The matrices placed cluster after cluster: [instance * stride + submesh] within a draw's range.
    std::vector<math::mat4> placed_transforms_;
    std::vector<math::mat4> placed_previous_;
    /// A baked instance's frames, at its placed index.
    std::vector<baked_frames> placed_frames_;
    std::vector<instanced_cluster> clusters_;
    std::vector<packed_part> parts_;
    std::vector<packed_block> blocks_;
    /// Instances in the packed data by layout and kind of part (static, baked).
    uint32_t packed_counts_[size_t(instanced_layout::count)][PART_KIND_COUNT] = {};
    /// The frame's packed data by layout and kind of part.
    packed_buffer packed_[size_t(instanced_layout::count)][PART_KIND_COUNT];
    std::unordered_map<model_key, cached_model, model_key_hash> models_;
    bool are_clusters_dirty_ = false;
    /// Scratch of build_draw_clusters: the cluster of each tile, and of each instance.
    std::unordered_map<uint64_t, uint32_t> cluster_by_tile_;
    std::vector<uint32_t> instance_clusters_;
    /// Scratch of sort_draw_clusters.
    std::vector<uint32_t> cluster_order_;
    std::vector<uint32_t> cluster_places_;
    std::vector<instanced_cluster> sorted_clusters_;
};

} // namespace unravel
