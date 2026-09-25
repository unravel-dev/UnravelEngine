#pragma once

#include <graphics/texture.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace unravel
{

/**
 * @brief Specular occlusion table (GTSO, Jimenez et al. 2016): the share of a GGX lobe that
 * lies inside the visibility cone of an ambient occlusion value.
 *
 * The visibility model is GTAO's: a cone whose cosine-weighted solid angle equals the
 * occlusion, cos(aperture) = sqrt(1 - visibility), around the bent normal (the normal when
 * there is none). The lobe is the GGX reflected-direction distribution at normal incidence -
 * rotationally symmetric - turned onto the lobe's dominant direction, and directions below the
 * cone axis's horizon count in neither integral. Measured against the exact integral over
 * view angle x roughness x visibility this holds an RMS error of about 0.04, where the
 * saturate(pow(NoV + AO, a) - 1 + AO) fit it replaces is at 0.26.
 *
 * Texel (x, y, z), sampled at texel centres: x = cosine of the angle between the cone axis and
 * the lobe's dominant direction, mapped from [-1, 1]; y = perceptual roughness; z = visibility.
 * Consumed by SpecularOcclusionGTSO (lighting.sh).
 */
struct specular_occlusion_lut
{
    /// Edge of the table in texels.
    static constexpr uint16_t resolution = 32;
    /// Stratified lobe samples per roughness row.
    static constexpr uint32_t lobe_samples = 4096;

    void generate();
    void clear();

    /**
     * @brief Builds the table on the CPU.
     * @return resolution^3 UNORM8 values, x fastest, then y, then z. Separate from generate()
     * so the table can be checked without a device.
     */
    static auto build() -> std::vector<uint8_t>;

    /// resolution^3 R8, filtered, clamped on every axis.
    std::unique_ptr<gfx::texture> texture;
};

} // namespace unravel
