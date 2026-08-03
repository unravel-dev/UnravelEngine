#pragma once

#include <engine/rendering/gi/surface_cache_service.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>

namespace unravel
{

/**
 * @brief Composes the stale cascade levels on the GPU, one thread per voxel.
 *
 * Replaces the per-voxel loop in @c global_sdf_clipmap::compose_level, which measured 4.20 ms of
 * WALL time on the main thread (87% of it blocked on the pool it dispatches to) -- very nearly the
 * entire GI GPU cost for a frame, landing as a stutter whenever the camera moved far enough to
 * re-snap a level.
 *
 * The CPU composer is NOT deleted. It remains the reference implementation that @c sample,
 * @c sample_ex and @c resolve_surface_point read, which is what the bake tests check this dispatch
 * against, and it remains the fallback when the compute program fails to load.
 *
 * Only the voxels move. Deciding WHICH levels to rebuild -- snapping, fingerprinting, staleness
 * ageing, the budget -- stays on the CPU in @c global_sdf_clipmap::update, because that logic is
 * subtle, tested, and identical either way.
 */
class gi_clipmap_compose_pass
{
public:
    struct run_params
    {
        surface_cache_service* surface_cache = nullptr;
        surface_cache_view* view_cache = nullptr;
    };

    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Dispatches one compose per level marked dirty by the last cascade update.
     *
     * @return true when the dispatch ran and the caller must NOT fall back to the CPU composer.
     */
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

    auto is_valid() const -> bool
    {
        return compose_program_.is_valid();
    }

private:
    struct compose_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_clipmap_compose_params;
        gfx::program::uniform_ptr u_clipmap_compose_origin;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_sdf_atlas;

        void cache_uniforms()
        {
            cache_uniform(program.get(),
                          u_clipmap_compose_params,
                          "u_clipmap_compose_params",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_clipmap_compose_origin,
                          "u_clipmap_compose_origin",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } compose_program_;
};

} // namespace unravel
