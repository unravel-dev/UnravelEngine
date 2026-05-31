#pragma once

#include "cloud_noise.h"
#include <graphics/texture.h>
#include <memory>

namespace unravel
{

class default_textures
{
public:
    static auto get() -> default_textures&;

    void generate();
    void clear();

    auto black_texture() const -> gfx::texture::ptr { return black_; }
    auto white_texture() const -> gfx::texture::ptr { return white_; }
    auto missing_texture() const -> gfx::texture::ptr { return missing_; }
    /// Fully transparent black (alpha 0). Used as the SSIL-disabled fallback so the
    /// indirect-lighting mix(irradiance, ssil.rgb, ssil.a) collapses to the SH probe.
    auto transparent_texture() const -> gfx::texture::ptr { return transparent_; }

    auto cloud_noise() -> cloud_noise_textures& { return cloud_noise_; }
    auto cloud_noise() const -> const cloud_noise_textures& { return cloud_noise_; }

private:
    default_textures();

    gfx::texture::ptr black_;
    gfx::texture::ptr white_;
    gfx::texture::ptr missing_;
    gfx::texture::ptr transparent_;

    cloud_noise_textures cloud_noise_;
};

} // namespace unravel
