#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/global_sdf_clipmap.h>

#include <graphics/graphics.h>
#include <graphics/texture.h>

#include <array>
#include <cstdint>

namespace unravel
{

/**
 * @brief GPU mirror of a @ref global_sdf_clipmap: the distance volume and its Lumen coverage.
 *
 * The cascade lives in ONE 3D texture with the levels stacked along Z, rather than one texture
 * per level. bgfx has no 3D texture arrays, and the tracer is already using four of its
 * sixteen binding slots; a slot per cascade would not scale and would force the sampling code
 * to branch over bindings instead of over an offset.
 *
 * Stacking is safe against filter bleed for the same reason the brick atlas is: the sampler
 * only ever addresses a level's interior with at least a half-voxel margin, so no trilinear
 * tap reaches the neighbouring level's slab.
 */
class global_sdf_clipmap_gpu
{
public:
    /// vec4 of level parameters uploaded per cascade: xyz = world origin, w = voxel size.
    static constexpr uint32_t level_param_count = global_sdf_clipmap::level_count;

    /// (Re)creates the GPU mirror for @p resolution voxels per level axis.
    auto init(uint32_t resolution) -> bool;
    void shutdown();

    auto is_valid() const -> bool
    {
        return static_cast<bool>(texture_);
    }

    /**
     * @brief Uploads the levels the clipmap flagged dirty, then clears those flags.
     */
    void upload(global_sdf_clipmap& clipmap);

    auto get_texture() const -> const gfx::texture::ptr&
    {
        return texture_;
    }

    /// Voxels per coverage texel along each axis (UE GLOBAL_DISTANCE_FIELD_COVERAGE_DOWNSAMPLE_FACTOR). Mirror of
    /// SDF_CLIPMAP_COVERAGE_DOWNSAMPLE in gi/sdf_clipmap.sh.
    static constexpr uint32_t coverage_downsample = 2;

    /**
     * @brief The Lumen coverage: R8, one texel per coverage_downsample^3 voxels, levels stacked along Z like the
     *        distance; 0 where only two-sided meshes lie near the voxel, 1 elsewhere. The compose writes it with the
     *        distance (cs_gi_clipmap_compose.sc); the Lumen global SDF march reads it (gi/sdf_clipmap.sh
     *        SdfSampleClipmapCoverage).
     */
    auto get_coverage_texture() const -> const gfx::texture::ptr&
    {
        return coverage_texture_;
    }

    /// Level voxels per coarse-mip texel along each axis (UE r.AOGlobalDistanceField.MipFactor). Mirror of
    /// SDF_CLIPMAP_MIP_FACTOR in gi/sdf_clipmap.sh.
    static constexpr uint32_t mip_factor = 4;

    /**
     * @brief The coarse mip of every level (UE GlobalDistanceFieldMipTexture): R8, one texel per mip_factor^3 voxels,
     *        levels stacked along Z like the distance, each texel the distance from its centre to the level's own
     *        surfaces over mip_factor times the level's encode range. Built from each level right after it composes
     *        (gi_clipmap_compose_pass, cs_gi_clipmap_mip.sc); the global SDF march steps through empty space by it
     *        (gi/sdf_clipmap.sh SdfSampleClipmapMip). Zero (the most negative distance, no skip) until built.
     */
    auto get_mip_texture() const -> const gfx::texture::ptr&
    {
        return mip_texture_;
    }

    /// One level's worth of mip texels, the other half of the mip build's propagation ping-pong.
    auto get_mip_scratch() const -> const gfx::texture::ptr&
    {
        return mip_scratch_;
    }

    /// Mip texels per level axis: ceil(resolution / mip_factor).
    auto get_mip_resolution() const -> uint32_t
    {
        return (resolution_ + mip_factor - 1u) / mip_factor;
    }

    /**
     * @brief Experiment bits the global SDF march reads from `u_sdf_clipmap_params.w` (gi/sdf_clipmap.sh
     *        u_sdf_clipmap_experiments): two code paths in one build for an in-session A/B. Zero in production.
     */
    void set_march_experiments(uint32_t bits)
    {
        march_experiments_ = bits;
    }

    /**
     * @brief Per-level parameters for the tracer, as `level_param_count` vec4s.
     * xyz = level origin in world space, w = level voxel size.
     */
    auto get_level_params() const -> const float*
    {
        return level_params_.data();
    }

    auto get_resolution() const -> uint32_t
    {
        return resolution_;
    }

    /**
     * @brief The vec4 every clipmap consumer binds as `u_sdf_clipmap_params`.
     *
     * x = resolution, y = blend band width in voxels, z = encode range, w = 1 + the march experiment bits when
     * the cascade is resident and worth consulting, 0 otherwise.
     *
     * Built here rather than at each call site because every pass that samples the cascade must
     * derive the same function from it: a pass with a different blend width would resolve surfaces
     * a fraction of a voxel away from the others.
     *
     * The texture depth is deliberately absent: it is `resolution * level_count`, and the shader
     * already hardcodes that layout in its texel addressing, so uploading it separately would be
     * one more value that could disagree.
     */
    auto get_sampling_params() const -> const float*
    {
        return sampling_params_.data();
    }

private:
    gfx::texture::ptr texture_;
    gfx::texture::ptr coverage_texture_;
    gfx::texture::ptr mip_texture_;
    gfx::texture::ptr mip_scratch_;
    uint32_t march_experiments_ = 0;
    uint32_t resolution_ = 0;
    std::array<float, size_t(level_param_count) * 4> level_params_{};
    std::array<float, 4> sampling_params_{};
};

} // namespace unravel
