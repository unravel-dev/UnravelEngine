#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/surface_cache_service.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>

namespace unravel
{

/**
 * @brief Populates and lights the world-space radiance cache.
 *
 * Two dispatches with different natural parallelism. Insertion runs per PIXEL, because that is
 * where surfaces are discovered; lighting runs per ENTRY, because a cell must be lit once
 * however many pixels resolve to it, and because one thread per entry makes the accumulation
 * race-free without atomics.
 *
 * Lighting per entry also means an entry keeps being lit after its surface leaves the screen,
 * so it stays valid and can be read back instantly when the surface returns -- instead of
 * converging from nothing, which is what a screen-space history has to do.
 */
class gi_cache_pass
{
public:
    struct settings
    {
        /// Pixel stride for insertion. Cells are far larger than a pixel, so sampling every
        /// pixel would resolve the same handful of cells thousands of times over.
        float insert_stride = 4.0f;
        /// Surfaces beyond this distance are not registered. Their cells would be enormous and
        /// contribute almost nothing, while still costing an entry each.
        float insert_max_distance = 200.0f;
        /// How far a cell centre is lifted along its normal before lighting. The centre can sit
        /// inside the geometry it represents, and shading from inside makes every shadow ray
        /// start occluded, converging the entry to black.
        float surface_offset = 0.05f;
        /// Floor on the accumulation blend weight, so a mature entry keeps following change.
        float min_alpha = 0.05f;
        /// Cap on accumulated samples, which sets how fast a converged entry can still move.
        float max_samples = 32.0f;
        /// Bounce rays cast per entry per frame.
        ///
        /// One is enough. The result feeds a running mean, so successive frames explore different
        /// directions and the estimate converges over time rather than within a frame -- cost
        /// stays fixed while the effective sample count grows. These rays are also what create
        /// entries for geometry the camera has never seen.
        float bounce_rays = 1.0f;
        /// Albedo assumed for cells no on-screen pixel has registered, whose material is unknown
        /// because the fields carry geometry only. Real albedo replaces it the moment the camera
        /// looks at the surface. Below one either way, which is what keeps the bounce feedback
        /// loop convergent -- each bounce carries less energy than the last.
        float default_albedo = 0.5f;
        /// How far a bounce ray travels. Shorter than a gather ray on purpose: this runs for every
        /// resident entry, and the near field is where the bounce actually matters.
        float bounce_distance = 40.0f;
        float bounce_near_field = 30.0f;
        float bounce_max_steps = 48.0f;
        /// Hit acceptance as a fraction of a voxel of whichever field answered.
        float bounce_surface_bias = 0.5f;
        // The cell size, base distance and level cap that KEYS are derived from deliberately do
        // not live here. Every pass touching the cache has to agree on them exactly, so they are
        // owned by the cache itself (radiance_cache_gpu::get_settings).
    };

    struct run_params
    {
        gfx::frame_buffer::ptr g_buffer;
        const camera* cam{};
        surface_cache_service* surface_cache{};
        settings settings;
    };

    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Registers visible surfaces, then lights every resident entry.
     * @return false when the pass could not run.
     */
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

private:
    struct insert_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_cache_params;
        gfx::program::uniform_ptr u_gi_cache_params2;
        gfx::program::uniform_ptr u_gi_insert_params;
        gfx::program::uniform_ptr u_gi_camera_position;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_gi_base_color;
        gfx::program::uniform_ptr s_gi_emissive;
        // The field itself: insertion resolves the G-buffer surface onto the isosurface, because
        // that is the only surface a reader arriving along a ray can address.
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_cache_params, "u_gi_cache_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params2, "u_gi_cache_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_insert_params, "u_gi_insert_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_camera_position, "u_gi_camera_position", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_base_color, "s_gi_base_color", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_emissive, "s_gi_emissive", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } insert_program_;

    struct update_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_cache_params;
        gfx::program::uniform_ptr u_gi_cache_params2;
        gfx::program::uniform_ptr u_gi_update_params;
        gfx::program::uniform_ptr u_gi_update_bounce;
        gfx::program::uniform_ptr u_gi_update_camera;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_gpu_light_params;
        gfx::program::uniform_ptr u_gi_shadow_params;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_cache_params, "u_gi_cache_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params2, "u_gi_cache_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_update_params, "u_gi_update_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_update_bounce, "u_gi_update_bounce", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_update_camera, "u_gi_update_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gpu_light_params, "u_gpu_light_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_shadow_params, "u_gi_shadow_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } update_program_;
};

} // namespace unravel
