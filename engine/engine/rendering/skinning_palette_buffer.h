#pragma once
#include <engine/engine_export.h>

#include <bgfx/bgfx.h>
#include <entt/container/dense_map.hpp>
#include <entt/entity/handle.hpp>
#include <entt/entity/registry.hpp>
#include <hpp/span.hpp>
#include <math/math.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace unravel
{
struct pose_mat4;

/// Binding stage of the palette buffer in the instanced skinned shaders (s_skin_palettes in skinning_palette.sh).
/// Below 8: D3D11 feature level 11.0 binds 8 buffers per draw. Clear of the G-buffer material samplers (0-5) and the
/// shadow cutout sampler (0).
constexpr uint8_t SKINNING_PALETTE_STAGE = 7;

/**
 * @brief The bone rows of one frame before they reach the GPU. A bone is the top three rows of its affine skinning
 * matrix (bone world x inverse bind); the bottom row is always (0, 0, 0, 1), and the shaders restore the blend of it.
 */
class ENGINE_EXPORT skinning_palette_rows
{
public:
    static constexpr uint32_t ROWS_PER_BONE = 3;
    /// The row of a palette without bones. No vertex reads it: a submesh without a palette is never drawn.
    static constexpr uint32_t INVALID_ROW = std::numeric_limits<uint32_t>::max();

    void clear();
    /// Reserves the rows of @p bone_count bones and returns the first one, INVALID_ROW for no bones. pack() fills them.
    auto reserve(size_t bone_count) -> uint32_t;
    /// Packs @p bones into the rows reserved from @p first_row on. Calls for distinct reservations may run in
    /// parallel.
    void pack(uint32_t first_row, hpp::span<const math::mat4> bones);
    /// reserve() and pack() in one.
    auto append(hpp::span<const math::mat4> bones) -> uint32_t;
    auto get_rows() const -> hpp::span<const math::vec4>;
    auto get_row_count() const -> uint32_t;
    /// Exchanges the row storage with @p storage (the upload hands the rows to the GPU without copying them).
    void swap_storage(std::vector<math::vec4>& storage);

    /// Writes the top three rows of @p bone to @p rows (row i = (m[0][i], m[1][i], m[2][i], m[3][i])).
    static void pack_bone(const math::mat4& bone, math::vec4* rows);
    /// The inverse of pack_bone.
    static auto unpack_bone(const math::vec4* rows) -> math::mat4;

private:
    std::vector<math::vec4> rows_;
};

/**
 * @brief The bone palettes of the instanced skinned draws of one frame, in one GPU buffer.
 *
 * The first draw path that needs a model's palettes in a frame reserves their rows; every later view of the frame
 * (shadow cascades and faces, probe captures, the velocity pass) gets the same rows. pack_pending() fills the rows in
 * parallel at the end of each pipeline run, while the models' palettes are certain to be alive. flush() uploads
 * everything once, right before the frame is submitted: bgfx runs dynamic-buffer updates ahead of the frame's draws, so
 * draws recorded earlier in the frame read this frame's rows, and when the buffer has to grow bgfx re-points the draws
 * already recorded at the new storage. Main thread only.
 */
class ENGINE_EXPORT skinning_palette_buffer
{
public:
    /// The first row of each of a model's palettes, indexed like the palettes passed to acquire(). Valid until the
    /// next acquire().
    struct model_rows
    {
        hpp::span<const uint32_t> current;
        /// Empty unless acquire() was asked for the previous palettes.
        hpp::span<const uint32_t> previous;
    };

    skinning_palette_buffer();
    ~skinning_palette_buffer();
    skinning_palette_buffer(const skinning_palette_buffer&) = delete;
    auto operator=(const skinning_palette_buffer&) -> skinning_palette_buffer& = delete;

    /// Forgets the previous frame's rows. Keeps the storage.
    void begin_frame();
    /**
     * @brief The rows of @p owner's palettes this frame, reserving them on the first call of the frame. The palettes
     * are read by the next pack_pending(), so they must stay unchanged until then.
     * @param owner The model's entity. Not the component's address: the editor deletes and creates entities between
     *        the runs of a frame, and a new component can take a deleted one's storage.
     * @param palettes The model's current palettes (model_component::get_skinning_transforms).
     * @param previous The previous-frame palettes for the velocity pass, or null. A palette whose previous pose is
     *        missing or has another bone count gets its current rows.
     */
    auto acquire(entt::const_handle owner,
                 const std::vector<pose_mat4>& palettes,
                 const std::vector<pose_mat4>* previous) -> model_rows;
    /// Packs the palettes acquired since the last call into their rows. Call before the models can change: at the end
    /// of each pipeline run.
    void pack_pending();
    /// Binds the buffer for the next draw (bindings are discarded with each submit).
    void bind();
    /// Uploads this frame's rows. Call once per frame, before gfx::frame().
    void flush();
    /// Releases the GPU buffer. Call before the graphics backend shuts down.
    void destroy();

    /// Rows uploaded by the last flush(), for statistics.
    auto get_uploaded_row_count() const -> uint32_t;
    /// This frame's rows so far (until flush() hands them to the GPU), for tests.
    auto get_rows() const -> hpp::span<const math::vec4>;

private:
    /// A model entity in one registry; the entity carries its version, so a recycled id is another owner.
    struct owner_key
    {
        const entt::registry* registry = nullptr;
        entt::entity entity = entt::null;

        auto operator==(const owner_key& other) const -> bool
        {
            return registry == other.registry && entity == other.entity;
        }
    };

    struct owner_key_hash
    {
        auto operator()(const owner_key& key) const noexcept -> std::size_t;
    };

    struct owner_entry
    {
        uint32_t first_current = 0;
        uint32_t first_previous = 0;
        uint32_t palette_count = 0;
        bool has_current = false;
        bool has_previous = false;
    };

    /// A palette waiting for pack_pending(): its bones and the first of its reserved rows.
    struct pack_job
    {
        hpp::span<const math::mat4> bones;
        uint32_t first_row = 0;
    };

    /// Row storage handed to bgfx by reference; bgfx's release callback (render thread) frees it for a later frame.
    struct upload_storage
    {
        std::vector<math::vec4> rows;
        std::atomic<bool> in_flight{false};
    };

    /// Reserves the palettes @p entry does not have yet this frame (the current ones, the previous ones if asked).
    void reserve_rows(owner_entry& entry, const std::vector<pose_mat4>& palettes, const std::vector<pose_mat4>* previous);
    /// Reserves the rows of @p bones and queues their packing; returns the first row.
    auto queue_palette(hpp::span<const math::mat4> bones) -> uint32_t;
    auto take_free_storage() -> upload_storage&;
    void ensure_buffer();

    skinning_palette_rows rows_;
    /// The palettes' first rows, a run per owner (and a second run once its previous palettes were asked for).
    std::vector<uint32_t> first_rows_;
    entt::dense_map<owner_key, owner_entry, owner_key_hash> owners_;
    std::vector<pack_job> pending_;
    std::vector<std::unique_ptr<upload_storage>> uploads_;
    bgfx::VertexLayout layout_;
    bgfx::DynamicVertexBufferHandle buffer_ = BGFX_INVALID_HANDLE;
    /// The rows the GPU buffer holds.
    uint32_t capacity_rows_ = 0;
    uint32_t uploaded_row_count_ = 0;
    /// Set by flush(): rows acquired later in the same frame would never reach the GPU.
    bool flushed_ = false;
};

} // namespace unravel
