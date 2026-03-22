#include "default_textures.h"
#include <graphics/graphics.h>
#include <array>
#include <cstdint>

namespace unravel
{

namespace
{

auto create_4x4_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> gfx::texture::ptr
{
    constexpr uint16_t width = 4;
    constexpr uint16_t height = 4;
    constexpr uint16_t num_layers = 1;
    std::array<uint8_t, width * height * 4> pixels{};
    for(size_t i = 0; i < width * height; ++i)
    {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
    auto* mem = gfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size()));
    return std::make_shared<gfx::texture>(
        width, height,
        false,
        num_layers,
        gfx::texture_format::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
        mem);
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
    black_   = create_4x4_rgba8(0, 0, 0, 255);
    white_   = create_4x4_rgba8(255, 255, 255, 255);
    missing_ = create_4x4_rgba8(255, 0, 255, 255);

    cloud_noise_.generate();
}

void default_textures::clear()
{
    black_.reset();
    white_.reset();
    missing_.reset();

    cloud_noise_.clear();
}

} // namespace unravel
