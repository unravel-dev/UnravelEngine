#pragma once

#include <engine/rendering/gi/surface_cache_service.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>

namespace unravel
{

/**
 * @brief Lights the cascade's surface voxels with direct lighting and traced shadows
 *        (GI v2 plan 3.2).
 *
 * One thread per surface-list entry, a quarter of the list per frame
 * (GI_LIGHT_VOXEL_UPDATE_DENOM), radiance written straight to the light volume - no temporal
 * state, because direct lighting with traced shadows is deterministic and the stochastic
 * machinery belongs to the world probes. This replaces the unbudgeted whole-table sweep the
 * radiance-hash update pass performed: cost is proportional to resident SURFACE, not to table
 * capacity, and bounded by the rotation denominator.
 */
class gi_light_voxel_pass
{
public:
    struct run_params
    {
        surface_cache_service* surface_cache = nullptr;
        surface_cache_view* view_cache = nullptr;
        uint32_t frame = 0;
        /// The world-probe windows are centred here; the bounce term must agree with the trace.
        math::vec3 camera_position{0.0f};
    };

    auto init(rtti::context& ctx) -> bool;

    auto run(gfx::render_view& rview, const run_params& params) -> bool;

    auto is_valid() const -> bool
    {
        return program_.is_valid();
    }

private:
    struct light_voxel_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_gpu_light_params;
        gfx::program::uniform_ptr u_gi_shadow_params;
        gfx::program::uniform_ptr u_gi_shadow_params2;
        gfx::program::uniform_ptr u_gi_light_voxel_camera;
        gfx::program::uniform_ptr u_gi_world_probe_params;
        gfx::program::uniform_ptr u_gi_world_probe_atlas;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_attr_albedo;
        gfx::program::uniform_ptr s_attr_emissive;
        gfx::program::uniform_ptr s_world_probe_irradiance;
        gfx::program::uniform_ptr s_world_probe_depth;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_light_voxel_camera, "u_gi_light_voxel_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_params, "u_gi_world_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_atlas, "u_gi_world_probe_atlas", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          s_world_probe_irradiance,
                          "s_world_probe_irradiance",
                          gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_world_probe_depth, "s_world_probe_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(),
                          u_gi_light_voxel_params,
                          "u_gi_light_voxel_params",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_sdf_clipmap_levels,
                          "u_sdf_clipmap_levels",
                          gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_gpu_light_params, "u_gpu_light_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_shadow_params, "u_gi_shadow_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_shadow_params2, "u_gi_shadow_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_attr_albedo, "s_attr_albedo", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_attr_emissive, "s_attr_emissive", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } program_;
};

} // namespace unravel
