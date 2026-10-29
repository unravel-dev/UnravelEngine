#pragma once

#include <engine/engine_export.h>

#include <engine/animation/animation.h>
#include <engine/assets/asset_handle.h>
#include <engine/rendering/skinning_palette_buffer.h>

#include <bgfx/bgfx.h>
#include <math/math.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace unravel
{
class mesh;

/// How a bake carries clips onto the mesh's skeleton (animation_pose_retargeting).
enum class baked_animation_retarget : uint8_t
{
    /// Local transforms as authored: clips exported with the mesh's own skeleton.
    copy,
    /// Rest aligned: clips of other exports or proportions, as long as both skeletons rest in the same pose.
    rest_aligned,
};

/// What one instance of a baked draw shows: a clip of the bake and a time in it (seconds). Scripts pass it as is.
struct baked_animation_state
{
    int32_t clip = 0;
    float time = 0.0f;
};
static_assert(sizeof(baked_animation_state) == 8, "BakedAnimationState in BakedAnimation.cs");

/**
 * @class baked_animation
 * @brief A skinned mesh's clips sampled once at a fixed rate, for drawing many animated instances without bones.
 *
 * Per frame it holds, as rows of the store's buffer, the skinning matrix of every palette bone (bone x inverse bind,
 * in the mesh's model space) and then one bone per rigid submesh: the model-space transform of its node, so rigid
 * submeshes on animated nodes (a sword in a hand) follow too. An instance picks a frame by clip and time; the shaders
 * put the frame's rows under the instance's world matrix. Clips are sampled in place: root motion is extracted and
 * dropped.
 */
class ENGINE_EXPORT baked_animation
{
public:
    /// Rows of a submesh without bones.
    static constexpr uint32_t INVALID_ROW = skinning_palette_rows::INVALID_ROW;

    auto get_mesh() const -> const asset_handle<mesh>&;
    auto get_frames_per_second() const -> float;
    auto get_clip_count() const -> uint32_t;
    /// Length of @p clip in seconds; 0 for a clip the bake does not have.
    auto get_clip_length(int32_t clip) const -> float;
    /// The frame (counted over every clip) nearest to @p state. Times outside the clip clamp to its ends; a clip the
    /// bake does not have shows the first clip's first frame.
    auto get_frame(const baked_animation_state& state) const -> uint32_t;
    /// The first row of @p frame's palettes in the store's buffer; INVALID_ROW when the mesh has no bones.
    auto get_frame_row(uint32_t frame) const -> uint32_t;
    /// Rows from a frame's first row to @p submesh's palette: its bones, or for a rigid submesh the one bone of its
    /// node. INVALID_ROW for a submesh the mesh does not have.
    auto get_palette_offset(uint32_t submesh) const -> uint32_t;
    /// True for a submesh without bones: its vertices follow its node alone (weight 1 on the palette's one bone).
    auto is_rigid(uint32_t submesh) const -> bool;
    /// Submeshes of the mesh's first LOD.
    auto get_submesh_count() const -> uint32_t;
    /// Radius of a sphere around the mesh's origin that holds the skinned vertices in every frame.
    auto get_radius() const -> float;
    auto get_frame_count() const -> uint32_t;

private:
    friend class baked_animation_store;

    struct baked_clip
    {
        uint32_t first_frame = 0;
        uint32_t frame_count = 0;
        float length = 0.0f;
    };

    asset_handle<mesh> mesh_;
    float frames_per_second_ = 0.0f;
    baked_animation_retarget retarget_ = baked_animation_retarget::copy;
    std::vector<baked_clip> clips_;
    uint32_t submesh_count_ = 0;
    std::vector<uint32_t> palette_offsets_;
    /// Per submesh: 1 when it is rigid (is_rigid()).
    std::vector<uint8_t> rigid_;
    /// The first row of every frame (a later clip's frames continue wherever the store's rows end).
    std::vector<uint32_t> frame_rows_;
    /// Rows one frame takes: every skinned submesh's palette, then one bone per rigid submesh.
    uint32_t rows_per_frame_ = 0;
    float radius_ = 0.0f;
};

/**
 * @class baked_animation_store
 * @brief The baked animations scripts made (BakedAnimation.Bake), their rows in one GPU buffer.
 *
 * Draws bind the buffer at SKINNING_PALETTE_STAGE in place of the frame's palette buffer, so the instanced skinned
 * shaders read baked rows with the same skin_palette_matrix. The buffer keeps its rows across frames and is uploaded
 * again only when the rows change. A released bake keeps its rows while draws still hold it; reclaim_released() then
 * packs the other bakes' rows over them. Main thread only.
 */
class ENGINE_EXPORT baked_animation_store
{
public:
    baked_animation_store();
    ~baked_animation_store();
    baked_animation_store(const baked_animation_store&) = delete;
    auto operator=(const baked_animation_store&) -> baked_animation_store& = delete;

    /**
     * @brief Starts a bake of @p render_mesh, loading the mesh first when it is not loaded.
     * @return The bake's id, 0 when the mesh is missing or has no skin: no armature, or vertices without the blend
     *         weights and indices the baked shaders read. add_clip() bakes its clips.
     */
    auto create(const asset_handle<mesh>& render_mesh, float frames_per_second, baked_animation_retarget retarget)
        -> uint32_t;
    /**
     * @brief Samples @p clip into bake @p id, loading the clip first when it is not loaded.
     * @return The clip's index in the bake; -1 when the bake or the clip is missing.
     */
    auto add_clip(uint32_t id, const asset_handle<animation_clip>& clip) -> int32_t;
    /// The bake @p id, or null. Draws hold it, so a release does not end it while they draw.
    auto find(uint32_t id) const -> std::shared_ptr<const baked_animation>;
    void release(uint32_t id);
    /// Reclaims the rows of the released bakes no draw holds any more. Call once draws of past frames are dropped.
    void reclaim_released();
    /// Releases every bake (play mode ends: scripts' bakes go with their scripts).
    void clear();
    /**
     * @brief Binds the rows for the next draw, uploading what bakes added since the last upload, and the part it
     * draws: @p palette_offset rows from each instance's frame row, a rigid submesh when @p is_rigid
     * (baked_animation::get_palette_offset(), is_rigid()).
     */
    void bind(uint32_t palette_offset, bool is_rigid);
    /// Releases the GPU buffer and uniform; the destructor does too. Call before the graphics backend shuts down.
    void destroy();
    /// Every row, for tests.
    auto get_rows() const -> hpp::span<const math::vec4>;

private:
    /// Packs the rows of the live and the still held released bakes from the first row on, and points their frames
    /// at the new places.
    void compact_rows();

    std::unordered_map<uint32_t, std::shared_ptr<baked_animation>> animations_;
    /// Released bakes whose rows are not reclaimed yet.
    std::vector<std::shared_ptr<baked_animation>> released_;
    uint32_t next_id_ = 1;
    skinning_palette_rows rows_;
    bgfx::VertexLayout layout_;
    bgfx::DynamicVertexBufferHandle buffer_ = BGFX_INVALID_HANDLE;
    /// u_baked_part of skinning_palette.sh: x = palette offset, y = 1 for a rigid submesh.
    bgfx::UniformHandle part_uniform_ = BGFX_INVALID_HANDLE;
    bool is_upload_pending_ = false;
};

} // namespace unravel
