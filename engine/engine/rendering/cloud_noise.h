#pragma once

#include <graphics/texture.h>
#include <memory>
#include <cstdint>

namespace unravel
{

struct cloud_noise_textures
{
    static constexpr uint16_t resolution = 64;
    static constexpr uint16_t flat_resolution = 256;
    static constexpr int tile_period = 6;

    /// Returns the singleton cloud noise textures. Created once in defaults::init_assets.
    static cloud_noise_textures& get();

    void generate();
    void clear();

    /// 64^3 RGBA16F 3D tileable noise for volumetric clouds.
    std::unique_ptr<gfx::texture> base_noise;

    /// 256x256 RGBA16F 2D tileable noise for flat clouds.
    std::unique_ptr<gfx::texture> flat_noise;

private:
    cloud_noise_textures() = default;
    void generate_3d();
    void generate_flat();
};

} // namespace unravel
