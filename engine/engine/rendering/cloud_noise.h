#pragma once

#include <graphics/texture.h>
#include <memory>
#include <cstdint>

namespace unravel
{

/// Tileable noise textures for the sky clouds (consumed by atmospherics/clouds.sh,
/// fs_cloud.sc and fs_sky.sc).
struct cloud_noise_textures
{
    /// 3D base texture edge in texels. With tile_period 6 every baked octave (Perlin up to
    /// 8x, Worley up to 4x) keeps at least ~2.7 texels per lattice cell.
    static constexpr uint16_t resolution = 128;
    static constexpr uint16_t flat_resolution = 256;
    /// Tile period in noise-space units. Mirrors CLOUD_NOISE_PERIOD in clouds.sh.
    static constexpr int tile_period = 6;

    void generate();
    void clear();

    /// 128^3 RGBA8: R = Perlin-Worley base shape, GBA = Worley at 1x / 2x / 4x (erosion).
    std::unique_ptr<gfx::texture> base_noise;

    /// 256x256 RGBA8 slice of the same field (same channel layout) for the flat path.
    std::unique_ptr<gfx::texture> flat_noise;

private:
    void generate_3d();
    void generate_flat();
};

} // namespace unravel
