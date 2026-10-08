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

    /// A scalability tier of the global illumination or the reflections (UE sg.GlobalIlluminationQuality and
    /// sg.ReflectionQuality: High 2, Epic 3).
    enum class quality_level : uint32_t
    {
        high = 2,
        epic = 3,
    };

    /// Edge of the atlas the cards are captured into each frame, in texels: the most texels captured per frame.
    enum class capture_atlas_size : uint32_t
    {
        size_512 = 512,
        size_1024 = 1024,
        size_2048 = 2048,
    };

    ///< The diffuse GI's tier (UE BaseScalability.ini GlobalIlluminationQuality): High places screen probes every 32
    ///< pixels, re-traces fewer radiance cache probes, lights the cards half as often and their radiosity with probes
    ///< every 8 texels of 3 x 3 rays; Epic is the full quality.
    quality_level global_illumination_quality = quality_level::epic;
    ///< The reflections' tier (UE ReflectionQuality): High traces one pixel of every 2 x 2 block and resolves each
    ///< pixel from 3 neighbouring rays at least as heavy as its own; Epic traces every pixel, 5 rays.
    quality_level reflection_quality = quality_level::epic;
    ///< The resident card texels (UE Epic's 4096). Every texel holds the captured material and the card lighting,
    ///< about 34 bytes.
    atlas_size surface_cache_atlas_size = atlas_size::size_4096;
    ///< r.LumenScene.SurfaceCache.Feedback (on in UE): the reflections report the card pages they hit and the cards
    ///< map finer pages there. Off by default: the report reaches the CPU through a readback that waits for the GPU
    ///< once every 16 frames.
    bool hi_res_reflection_pages = false;
    ///< r.LumenScene.SurfaceCache.CardCapturesPerFrame: card pages captured per frame at most.
    uint32_t card_captures_per_frame = 300;
    ///< The texels captured per frame at most (UE: an eighth of the surface cache atlas edge, at least one 512
    ///< texel card).
    capture_atlas_size card_capture_atlas_size = capture_atlas_size::size_512;
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
