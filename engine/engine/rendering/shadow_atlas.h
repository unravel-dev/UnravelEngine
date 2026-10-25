#pragma once

#include <engine/engine_export.h>

#include <bgfx/bgfx.h>
#include <hpp/optional.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace unravel
{
namespace shadow
{

/// A light's square tile in the shadow atlas, in texels.
struct shadow_atlas_tile
{
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t size = 0;

    auto operator==(const shadow_atlas_tile& other) const -> bool = default;
};

/// What a light asks the shadow atlas for in a view.
struct shadow_atlas_request
{
    /// The light's shadow map generator: one tile per owner.
    std::uintptr_t owner = 0;
    /// Edge (texels, a power of two) of the map at the light's own settings.
    std::uint16_t size = 0;
    /// How much of the view the light reaches: the most important lights keep their resolution longest when the
    /// atlas is full.
    float importance = 0.0f;
};

/**
 * @brief The layout of the shadow atlas: which light's map sits where, at what size, in an atlas of what size. No GPU
 * resources, so it can be tested on its own.
 *
 * Tiles are power-of-two squares packed as a quadtree in a power-of-two atlas: any set of tiles whose areas sum to the
 * atlas area or less fits. The atlas grows with the tiles it holds, up to max_atlas_size.
 *
 * Every light keeps the resolution it asks for while all of them fit in the largest atlas. Past that, each light has a
 * share: the least important lights step down one level at a time, never below min_tile_size, until all fit. A light
 * that asks for a shadow is never left without one (up to the 1024 lights the smallest tiles fit).
 *
 * Nothing that would show changes on its own from frame to frame:
 * - a light's importance is the most any view of this frame or the previous one gave it, so the views of a frame
 *   (cameras, probe faces) agree on which lights matter;
 * - a light gives up resolution only when the tiles no longer fit, and only a light holding more than its share;
 * - a light gets more only after it has wanted it for regain_frames frames, and the atlas shrinks only after its tiles
 *   have fit a smaller one for shrink_frames frames; both are decided only by the first view of a frame.
 * Tiles stay where they are unless the atlas resizes or a tile changes level; every such change bumps the generation,
 * and maps rendered under an older generation are stale.
 */
class ENGINE_EXPORT shadow_atlas_allocator
{
public:
    static constexpr std::uint16_t min_tile_size = 128;
    static constexpr std::uint16_t min_atlas_size = 512;
    static constexpr std::uint16_t max_atlas_size = 4096;
    /// Frames a light that was stepped down must have room again before it gets its resolution back.
    static constexpr std::uint32_t regain_frames = 30;
    /// Frames the tiles must fit a smaller atlas before the atlas shrinks.
    static constexpr std::uint32_t shrink_frames = 300;

    /**
     * @brief Gives every light of @p requests a tile for render frame @p frame, keeping the tiles of lights already
     * placed. A view calls it before it renders any of those lights. A new frame first takes back the tiles of lights
     * no view asked for in the previous frame.
     * @return True when the generation changed: tiles rendered earlier, in this frame or before, are stale.
     */
    auto reserve(std::uint64_t frame, const std::vector<shadow_atlas_request>& requests) -> bool;

    /// The tile of @p owner; none when it has not asked, or when even the smallest tiles no longer fit.
    auto find(std::uintptr_t owner) const -> hpp::optional<shadow_atlas_tile>;

    /// Takes back the tile of @p owner (a light that stopped casting shadows or went away).
    void release(std::uintptr_t owner);

    /// Edge of the atlas in texels; 0 while no light uses it.
    auto get_size() const -> std::uint16_t
    {
        return size_;
    }

    /// Bumped whenever tiles move or change size.
    auto get_generation() const -> std::uint64_t
    {
        return generation_;
    }

    /// Lights holding a tile.
    auto get_tile_count() const -> size_t;

    /// Forgets every tile.
    void clear();

private:
    struct entry
    {
        /// Edge the light asks for, clamped to the tile limits.
        std::uint16_t requested = 0;
        /// Edge the light renders at: the request, or less while the atlas is full.
        std::uint16_t assigned = 0;
        /// The most importance any view gave the light in the previous frame, and so far in this one.
        float previous_importance = 0.0f;
        float frame_importance = 0.0f;
        std::uint64_t last_frame = 0;
        /// Consecutive frames the light's share was larger than the tile it holds.
        std::uint32_t regain_votes = 0;
        bool is_placed = false;
        shadow_atlas_tile tile{};

        auto get_importance() const -> float
        {
            return previous_importance > frame_importance ? previous_importance : frame_importance;
        }
    };

    using size_map = std::unordered_map<std::uintptr_t, std::uint16_t>;
    /// Owners from the least important up; equal importance in a fixed order.
    using importance_order = std::vector<std::pair<float, std::uintptr_t>>;

    void begin_frame(std::uint64_t frame);
    auto get_importance_order() const -> importance_order;
    /// Each light's share: the edge it should render at so that all of them fit the largest atlas.
    auto compute_targets(const importance_order& order) const -> size_map;
    /// Applies the shares to the assigned edges; @return true when a placed tile's edge changed.
    auto assign_sizes(bool is_first_view) -> bool;
    /// The atlas edge for the assigned tiles; @return true when it changed.
    auto choose_atlas_size(bool is_first_view) -> bool;
    void repack_all();
    auto place(entry& e) -> bool;
    void free_square(shadow_atlas_tile square);
    auto take_square(std::uint16_t size) -> hpp::optional<shadow_atlas_tile>;

    std::unordered_map<std::uintptr_t, entry> entries_;
    /// Free squares of the quadtree.
    std::vector<shadow_atlas_tile> free_squares_;
    std::uint16_t size_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t frame_ = 0;
    std::uint32_t shrink_votes_ = 0;
};

/**
 * @brief The shadow atlas every spot and point light with a depth-compared shadow (hard, PCF, PCSS) renders its map
 * into: one float-depth colour texture and one depth texture, shared by every view of the frame. VSM and ESM lights
 * keep maps of their own.
 *
 * Process-wide like the lights' maps it replaces: a local light's map is the same for every view of a frame, so the
 * first view that renders it wins (shadowmap_generator::already_resolved), and every view samples the same atlas.
 */
class ENGINE_EXPORT shadow_atlas
{
public:
    static auto get() -> shadow_atlas&;

    /// Reserves the tiles of @p requests for the current render frame and fits the textures to the layout.
    /// @return True when the generation changed (see shadow_atlas_allocator::reserve).
    auto reserve(const std::vector<shadow_atlas_request>& requests) -> bool;
    auto find(std::uintptr_t owner) const -> hpp::optional<shadow_atlas_tile>;
    void release(std::uintptr_t owner);

    auto get_frame_buffer() const -> bgfx::FrameBufferHandle
    {
        return frame_buffer_;
    }

    /// The stored-depth colour texture (R32F) and the depth texture (D24S8) the PCF comparisons read.
    auto get_color_texture() const -> bgfx::TextureHandle;
    auto get_depth_texture() const -> bgfx::TextureHandle;

    /// Edge of the textures in texels; 0 while no light uses the atlas.
    auto get_size() const -> std::uint16_t
    {
        return texture_size_;
    }

    auto get_generation() const -> std::uint64_t
    {
        return allocator_.get_generation();
    }

    auto get_tile_count() const -> size_t
    {
        return allocator_.get_tile_count();
    }

    /// Frees the textures and forgets every tile (renderer shutdown).
    void clear();

private:
    void fit_textures();
    void destroy_textures();

    shadow_atlas_allocator allocator_;
    bgfx::FrameBufferHandle frame_buffer_{bgfx::kInvalidHandle};
    std::uint16_t texture_size_ = 0;
};

} // namespace shadow
} // namespace unravel
