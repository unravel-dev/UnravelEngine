#pragma once

#include <cstdint>

namespace unravel
{

/**
 * @brief Project-wide global illumination settings: UE's Lumen scene console variables, beside the per-volume
 *        gi_settings of the Global Illumination component. The renderer applies them every frame.
 */
struct gi_project_settings
{
    /// Edge of the surface cache's physical atlas, in texels (r.LumenScene.SurfaceCache.AtlasSize).
    enum class atlas_size : uint32_t
    {
        size_1024 = 1024,
        size_2048 = 2048,
        size_4096 = 4096,
    };

    /// Edge of the atlas the cards are captured into each frame, in texels: the most texels captured per frame.
    enum class capture_atlas_size : uint32_t
    {
        size_512 = 512,
        size_1024 = 1024,
        size_2048 = 2048,
    };

    ///< The resident card texels. Every texel holds the captured material and the card lighting, about 54 bytes.
    atlas_size surface_cache_atlas_size = atlas_size::size_2048;
    ///< r.LumenScene.SurfaceCache.CardCapturesPerFrame: card pages captured per frame at most.
    uint32_t card_captures_per_frame = 300;
    ///< The texels captured per frame at most.
    capture_atlas_size card_capture_atlas_size = capture_atlas_size::size_1024;
    ///< r.LumenScene.SurfaceCache.CardCaptureRefreshFraction: the share of the capture budget spent capturing resident
    ///< pages again, oldest first, so material changes reach the surface cache. 0 disables.
    float card_capture_refresh_fraction = 0.125f;
    ///< r.LumenScene.SurfaceCache.CardTexelDensityScale: card texels per unit of half extent over the viewer distance.
    float card_texel_density_scale = 100.0f;
    ///< r.LumenScene.SurfaceCache.CardMaxTexelDensity, in texels per metre.
    float card_max_texel_density = 20.0f;
    ///< r.LumenScene.SurfaceCache.CardMaxResolution.
    uint32_t card_max_resolution = 512;
    ///< r.LumenScene.SurfaceCache.CardMinResolution: a card that would get fewer texels is not resident.
    uint32_t card_min_resolution = 2;
    ///< r.LumenScene.SurfaceCache.MeshCardsMinSize, in metres: a card face smaller than this squared is not resident.
    float mesh_cards_min_size = 0.1f;

    friend auto operator==(const gi_project_settings& lhs, const gi_project_settings& rhs) -> bool = default;
};

} // namespace unravel
