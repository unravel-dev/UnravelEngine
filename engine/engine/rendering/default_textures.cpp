#include "default_textures.h"
#include <graphics/graphics.h>
#include <array>
#include <cstdint>

namespace unravel
{

namespace
{

constexpr uint16_t default_texture_size = 4;
constexpr uint16_t default_texture_layers = 1;
constexpr std::size_t cube_face_count = 6;

/// Copies pixel_count RGBA8 pixels of one color into bgfx-owned memory.
template<std::size_t pixel_count>
auto copy_solid_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> const bgfx::Memory*
{
    std::array<uint8_t, pixel_count * 4> pixels{};
    for(size_t i = 0; i < pixel_count; ++i)
    {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
    return bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size()));
}

auto create_4x4_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> gfx::texture::ptr
{
    constexpr std::size_t pixel_count = default_texture_size * default_texture_size;
    return std::make_shared<gfx::texture>(
        default_texture_size, default_texture_size,
        false,
        default_texture_layers,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
        copy_solid_rgba8<pixel_count>(r, g, b, a));
}

auto create_4x4_cube_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> gfx::texture::ptr
{
    constexpr std::size_t pixel_count = default_texture_size * default_texture_size * cube_face_count;
    return std::make_shared<gfx::texture>(
        default_texture_size,
        false,
        default_texture_layers,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
        copy_solid_rgba8<pixel_count>(r, g, b, a));
}

} // namespace

default_textures::default_textures() = default;

auto default_textures::get() -> default_textures&
{
    static default_textures instance;
    return instance;
}

void default_textures::generate()
{
    black_       = create_4x4_rgba8(0, 0, 0, 255);
    white_       = create_4x4_rgba8(255, 255, 255, 255);
    missing_     = create_4x4_rgba8(255, 0, 255, 255);
    transparent_ = create_4x4_rgba8(0, 0, 0, 0);
    black_cube_  = create_4x4_cube_rgba8(0, 0, 0, 255);

    cloud_noise_.generate();
    specular_occlusion_.generate();
}

void default_textures::clear()
{
    black_.reset();
    white_.reset();
    missing_.reset();
    transparent_.reset();
    black_cube_.reset();

    cloud_noise_.clear();
    specular_occlusion_.clear();
}

} // namespace unravel
