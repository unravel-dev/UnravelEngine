#pragma once

#include <engine/rendering/gi/surface_cache_service.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>

namespace unravel
{

/**
 * @brief Traces and convolves the world probe cascades (GI v2 plan 3.3, revised design).
 *
 * Every probe, every frame, one direction stratum: the 16x16 radiance atlas is a 16-frame
 * windowed mean with zero steady-state variance, and the convolution materialises its
 * irradiance + Chebyshev depth moments each frame. Camera rotation is a no-op on all of it;
 * translation re-claims only the slots whose world cell changed.
 */
class gi_world_probe_pass
{
public:
    struct run_params
    {
        surface_cache_service* surface_cache = nullptr;
        surface_cache_view* view_cache = nullptr;
        math::vec3 camera_position{0.0f};
        /// The lighting pass's environment SH probe (sky at ray miss); black when absent.
        gfx::texture::ptr irradiance_sh;
        uint32_t frame = 0;
        /// Hash of the resident light set; a change halves the probe refresh window for one
        /// full window (the DDGI event pattern, plan section 8).
        uint64_t light_hash = 0;
    };

    auto init(rtti::context& ctx) -> bool;

    auto run(gfx::render_view& rview, const run_params& params) -> bool;

    auto is_valid() const -> bool
    {
        return trace_program_.is_valid() && convolve_program_.is_valid();
    }

private:
    struct trace_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_world_probe_params;
        gfx::program::uniform_ptr u_gi_world_probe_window;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_light_voxels;
        gfx::program::uniform_ptr s_gi_env_sh;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_world_probe_params, "u_gi_world_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_gi_world_probe_window,
                          "u_gi_world_probe_window",
                          gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_gi_light_voxel_params, "u_gi_light_voxel_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_sdf_clipmap_levels,
                          "u_sdf_clipmap_levels",
                          gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_light_voxels, "s_light_voxels", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_env_sh, "s_gi_env_sh", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } trace_program_;

    struct convolve_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_world_probe_params;
        gfx::program::uniform_ptr s_world_probe_radiance;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_world_probe_params, "u_gi_world_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_world_probe_radiance, "s_world_probe_radiance", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } convolve_program_;

    /// Light-change reactivity state: while frames remain, the trace covers two strata per
    /// frame (window halves to 8), then settles back to one.
    uint64_t last_light_hash_ = 0;
    uint64_t last_content_epoch_ = 0;
    uint32_t fast_frames_ = 0;
};

} // namespace unravel
