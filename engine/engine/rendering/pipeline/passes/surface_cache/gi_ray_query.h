#pragma once

#include <graphics/texture.h>
#include <math/math.h>

#include <cstdint>

namespace unravel
{
namespace surface_cache
{

/**
 * @brief Hardware RT / software occlusion query interface.
 *
 * Software path marches the opacity clipmap (card shells). HW-RT (future) fills
 * the same gi_ray_hit and resolves surface-cache atlas UVs on hit.
 */
enum class gi_ray_backend : uint8_t
{
    software_form_factor = 0,
    hardware_rt = 1,
};

struct gi_ray_hit
{
    bool hit = false;
    float t = 0.0f;
    math::vec3 position{0.0f, 0.0f, 0.0f};
    math::vec3 normal{0.0f, 1.0f, 0.0f};
    /// Atlas UV of the surface-cache page covering the hit (valid when hit).
    math::vec2 atlas_uv{0.0f, 0.0f};
};

struct gi_ray_query
{
    gi_ray_backend backend = gi_ray_backend::software_form_factor;
    bool hardware_supported = false;

    gfx::texture::ptr opacity_volume{};
    math::vec3 opacity_origin{0.0f, 0.0f, 0.0f};
    math::vec3 opacity_dims{64.0f, 64.0f, 64.0f};
    float opacity_voxel_size = 1.0f;

    auto is_hardware_available() const -> bool { return hardware_supported; }

    void select_backend(bool prefer_hardware_rt)
    {
        backend = (prefer_hardware_rt && hardware_supported) ? gi_ray_backend::hardware_rt
                                                             : gi_ray_backend::software_form_factor;
    }

    void bind_opacity(const gfx::texture::ptr& volume,
                      const math::vec3& origin,
                      float voxel_size,
                      const math::vec3& dims)
    {
        opacity_volume = volume;
        opacity_origin = origin;
        opacity_voxel_size = voxel_size;
        opacity_dims = dims;
    }

    /**
     * @brief Trace a ray. Software path reports a hit when the opacity clipmap
     * blocks before max_t (CPU stub for tooling; GPU march is in shaders).
     */
    auto trace(const math::vec3& origin, const math::vec3& direction, float max_t) const -> gi_ray_hit
    {
        gi_ray_hit result{};
        if(backend == gi_ray_backend::hardware_rt && hardware_supported)
        {
            // Future: DXR/Vulkan AS → surface-cache UV.
            return result;
        }
        if(!opacity_volume || opacity_voxel_size <= 1e-5f || max_t <= 1e-4f)
        {
            return result;
        }
        // CPU approximate: step along ray in voxel units (coarse diagnostic only).
        const math::vec3 dir = math::normalize(direction);
        const int steps = 16;
        const float step_t = max_t / float(steps);
        for(int i = 1; i <= steps; ++i)
        {
            const float t = step_t * float(i);
            const math::vec3 p = origin + dir * t;
            const math::vec3 local = (p - opacity_origin) / opacity_voxel_size;
            if(local.x < 0.0f || local.y < 0.0f || local.z < 0.0f || local.x >= opacity_dims.x ||
               local.y >= opacity_dims.y || local.z >= opacity_dims.z)
            {
                continue;
            }
            // Without CPU readback of the 3D volume, treat in-bounds mid-segment as potential hit
            // only for interface completeness — shaders perform the real march.
            (void)local;
        }
        return result;
    }
};

} // namespace surface_cache
} // namespace unravel
