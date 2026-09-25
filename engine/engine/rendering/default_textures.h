#pragma once

#include "cloud_noise.h"
#include "specular_occlusion_lut.h"
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
    /// Opaque black cube map for a cube sampler a program declares but a given submit does not
    /// read: D3D11 flags an empty slot, and a 2D default would not match the sampler type.
    auto black_cube_texture() const -> gfx::texture::ptr { return black_cube_; }

    auto cloud_noise() -> cloud_noise_textures& { return cloud_noise_; }
    auto cloud_noise() const -> const cloud_noise_textures& { return cloud_noise_; }

    /// The GTSO specular occlusion table (SpecularOcclusionGTSO in lighting.sh).
    auto specular_occlusion() const -> const specular_occlusion_lut& { return specular_occlusion_; }

private:
    default_textures();

    gfx::texture::ptr black_;
    gfx::texture::ptr white_;
    gfx::texture::ptr missing_;
    gfx::texture::ptr transparent_;
    gfx::texture::ptr black_cube_;

    cloud_noise_textures cloud_noise_;
    specular_occlusion_lut specular_occlusion_;
};

} // namespace unravel
