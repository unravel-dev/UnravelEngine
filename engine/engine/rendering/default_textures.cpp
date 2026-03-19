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
    std::array<uint8_t, 4 * 4> pixel = {r, g, b, a, r, g, b, a, r, g, b, a, r, g, b, a};
    auto* mem = gfx::copy(pixel.data(), static_cast<uint32_t>(pixel.size()));
    return std::make_shared<gfx::texture>(
        uint16_t(1), uint16_t(1),
        false,
        uint16_t(1),
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
