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

    /**
     * @brief (Re)creates the GPU mirror for @p resolution.
     *
     * @param compose_on_gpu The EFFECTIVE composer for this mirror's lifetime. It decides the
     * surface list/count buffer flags: the GPU composer writes them from compute, so they must
     * be BGFX_BUFFER_COMPUTE_WRITE - which bgfx forbids updating from the CPU; the CPU composer
     * uploads them with gfx::update, which requires that flag absent. One set of flags cannot
     * serve both, so a composer change re-creates the mirror (see surface_cache_view::update).
     */
    auto init(uint32_t resolution, bool compose_on_gpu) -> bool;
    void shutdown();

    /// The composer this mirror was created for. True = the compose pass writes the surface
    /// list/count buffers on the GPU; false = upload() writes them from the CPU.
    auto is_composed_on_gpu() const -> bool
    {
        return compose_on_gpu_;
    }

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

    /// Attribute voxels per axis (half the distance resolution; see
    /// global_sdf_clipmap::attr_downsample).
    auto get_attr_resolution() const -> uint32_t
    {
        return resolution_ / global_sdf_clipmap::attr_downsample;
    }

    /// rgb = winning albedo, a = 1 where surface. Levels stacked along Z, like the distance
    /// volume, at attribute resolution.
    auto get_attr_albedo_texture() const -> const gfx::texture::ptr&
    {
        return attr_albedo_texture_;
    }

    /// rgb = winning emissive in radiance units.
    auto get_attr_emissive_texture() const -> const gfx::texture::ptr&
    {
        return attr_emissive_texture_;
    }

    /// The light volume (GI v2 plan 3.2): outgoing radiance per exposed face per surface voxel,
    /// Z-stacked as (level * 6 + face) slabs of attribute resolution. Written by
    /// cs_gi_light_voxels, zeroed per recomposed level by cs_gi_clipmap_attributes.
    auto get_light_voxel_texture() const -> const gfx::texture::ptr&
    {
        return light_voxel_texture_;
    }

    /// The bounce's cage-visibility memo: R32U (32-bit so typed UAV loads are mandatory on
    /// every backend), one texel per light-volume texel (same layout), holding mask +
    /// generation + probe level in the low 16 bits (GiWorldProbeVisMemoPack in
    /// gi_world_probes.sh). Read and restamped by cs_gi_light_voxels' bounce path alone.
    auto get_bounce_vis_memo() const -> const gfx::texture::ptr&
    {
        return bounce_vis_memo_;
    }

    /// True until the compose pass zeroes the memo (generation 0 = "never stamped" relies on
    /// it; allocation garbage could otherwise masquerade as a stamped mask). Kept separate
    /// from the other one-time clears so a missing helper shader only costs the memo itself:
    /// an unseeded memo hands the kernel generation 0, the safe-slow gated-march path.
    auto needs_bounce_vis_memo_seed() const -> bool
    {
        return static_cast<bool>(bounce_vis_memo_) && !bounce_vis_memo_seeded_;
    }

    void mark_bounce_vis_memo_seeded()
    {
        bounce_vis_memo_seeded_ = true;
    }

    /**
     * @brief The bounce visibility-memo generation for this frame, advancing it when any
     *        memoised verdict may have changed.
     *
     * A stored verdict depends on the composed field (tracked by @p content_epoch, the
     * clipmap's camera-independent content counter) and on the cage lattice positions the
     * probe windows expose (tracked per level as the camera's window cell - a scroll re-fills
     * probe slots, so the stored masks conservatively die with it). Camera motion WITHIN a
     * cell can flip which cascade level answers a query without moving either tracker; the
     * memo's per-texel LEVEL tag catches that, so it needs no invalidation here.
     *
     * Returns 0 - "memo unavailable, use the gated march" - until the memo is seeded. Live
     * generations wrap 1..63 (the texel stores 6 bits, 0 reserved for "never stamped").
     */
    auto refresh_bounce_vis_generation(uint64_t content_epoch,
                                       const math::vec3& camera_position,
                                       float base_spacing) -> uint32_t;

    /// World probes per cascade axis. MUST equal GI_WORLD_PROBE_AXIS in gi_world_probes.sh;
    /// derived as resolution / GI_WORLD_PROBE_DIVISOR + 1 at the runtime resolution of 128, and
    /// the probe resources are only created when that derivation holds, because the shader
    /// hardcodes the axis for its group-shared layout.
    static constexpr uint32_t world_probe_axis = 9;

    auto has_world_probes() const -> bool
    {
        return static_cast<bool>(world_probe_radiance_);
    }

    /// 16x16 octahedral radiance tiles (rgb radiance, a hitT; a < 0 = sky), tile grid
    /// (axis * axis) wide by (axis * levels) tall - see gi_world_probes.sh.
    auto get_world_probe_radiance() const -> const gfx::texture::ptr&
    {
        return world_probe_radiance_;
    }

    /// 8x8 (+1 gutter) octahedral irradiance tiles: rgb = E/pi, a = sky fraction.
    auto get_world_probe_irradiance() const -> const gfx::texture::ptr&
    {
        return world_probe_irradiance_;
    }

    /// 8x8 (+1 gutter) depth moments (mean, mean^2) for the Chebyshev visibility test.
    auto get_world_probe_depth() const -> const gfx::texture::ptr&
    {
        return world_probe_depth_;
    }

    /// One packed world-cell id per probe slot (scroll detection).
    auto get_world_probe_cells() const -> gfx::dynamic_index_buffer_handle
    {
        return world_probe_cells_;
    }

    /// Entries in @ref get_world_probe_cells (0 when world probes are disabled).
    auto get_world_probe_cell_count() const -> uint32_t
    {
        return world_probe_cell_count_;
    }

    /**
     * @brief True until the compose pass seeds the freshly created cell/cursor buffers.
     *
     * bgfx forbids CPU updates on compute-writable buffers (and compiles the check out in
     * release, so relying on it silently diverges per config). The sentinels the claim logic
     * needs - and the zeroed cursors an uncomposed level must present - are therefore GPU fill
     * dispatches, run once by the compose pass, which calls @ref mark_seed_done.
     */
    auto needs_buffer_seed() const -> bool
    {
        return needs_buffer_seed_;
    }

    void mark_seed_done()
    {
        needs_buffer_seed_ = false;
    }

    /**
     * @brief Whether the light volume and probe atlases still hold allocation garbage.
     *
     * A SEPARATE flag from the buffer seed, deliberately: the clears run through their own
     * compute programs, and a machine where those fail to compile must still get its cell
     * sentinels seeded - one late helper must not hold the other's one-time work hostage.
     */
    auto needs_texture_clear() const -> bool
    {
        return needs_texture_clear_;
    }

    void mark_texture_clear_done()
    {
        needs_texture_clear_ = false;
    }

    /// One packed world-cell id per ATTRIBUTE slot per level: the light-radiance survival
    /// detector (a slot whose cell changed resets its light texels; see
    /// cs_gi_clipmap_attributes.sc).
    auto get_attr_cells() const -> gfx::dynamic_index_buffer_handle
    {
        return attr_cells_;
    }

    /// xy = 1 / irradiance-depth atlas size (they share a layout), zw = atlas size.
    auto get_world_probe_atlas_params() const -> const float*
    {
        return world_probe_atlas_params_.data();
    }

    /// Surface-voxel list WITH ITS COUNTS: a level_count-entry header of append cursors
    /// (index = level), then one attr_resolution^3 segment of packed entries per level
    /// (global_sdf_clipmap::pack_surface_voxel layout). One buffer on purpose - the split
    /// count buffer cost cs_gi_light_voxels its last free bgfx stage.
    auto get_surface_list_buffer() const -> gfx::dynamic_index_buffer_handle
    {
        return surface_list_;
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
    gfx::texture::ptr attr_albedo_texture_;
    gfx::texture::ptr attr_emissive_texture_;
    gfx::texture::ptr light_voxel_texture_;
    gfx::texture::ptr bounce_vis_memo_;
    gfx::texture::ptr world_probe_radiance_;
    gfx::texture::ptr world_probe_irradiance_;
    gfx::texture::ptr world_probe_depth_;
    /// Bounce visibility-memo invalidation state (see refresh_bounce_vis_generation).
    bool bounce_vis_memo_seeded_ = false;
    uint32_t bounce_vis_generation_ = 0;
    uint64_t bounce_vis_content_epoch_ = 0;
    std::array<std::array<int32_t, 3>, global_sdf_clipmap::level_count> bounce_vis_window_cells_{};
    gfx::dynamic_index_buffer_handle surface_list_{bgfx::kInvalidHandle};
    gfx::dynamic_index_buffer_handle attr_cells_{bgfx::kInvalidHandle};
    gfx::dynamic_index_buffer_handle world_probe_cells_{bgfx::kInvalidHandle};
    uint32_t world_probe_cell_count_ = 0;
    bool needs_buffer_seed_ = false;
    bool needs_texture_clear_ = false;
    bool compose_on_gpu_ = true;
    std::array<float, 4> world_probe_atlas_params_{};
    uint32_t resolution_ = 0;
    std::array<float, size_t(level_param_count) * 4> level_params_{};
    std::array<float, 4> sampling_params_{};
};

} // namespace unravel
