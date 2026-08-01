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
 * @brief GPU mirror of a @ref global_sdf_clipmap.
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
     * x = resolution, y = blend band width in voxels, z = encode range, w = non-zero when the
     * cascade is resident and worth consulting.
     *
     * Built here rather than at each call site because three passes sample the same cascade and
     * must derive the same function from it. A pass that used a different blend width would
     * resolve surfaces a fraction of a voxel away from the pass it shares the radiance cache
     * with, which does not fail loudly -- it just means the two never find each other's entries.
     *
     * The texture depth is deliberately absent: it is `resolution * level_count`, and the shader
     * already hardcodes that layout in its texel addressing, so uploading it separately was one
     * more value that could disagree.
     */
    auto get_sampling_params() const -> const float*
    {
        return sampling_params_.data();
    }

private:
    gfx::texture::ptr texture_;
    uint32_t resolution_ = 0;
    std::array<float, size_t(level_param_count) * 4> level_params_{};
    std::array<float, 4> sampling_params_{};
};

} // namespace unravel
