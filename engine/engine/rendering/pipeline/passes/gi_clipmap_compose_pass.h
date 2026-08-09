#pragma once

#include <engine/rendering/gi/surface_cache_system.h>
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
        surface_cache_system* surface_cache = nullptr;
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

    /// Attribute composer (GI v2 plan 3.1): albedo/emissive voxels + the surface-voxel list,
    /// dispatched per recomposed level after its distance voxels are written.
    struct attributes_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_clipmap_attr_params;
        gfx::program::uniform_ptr u_clipmap_compose_origin;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_light_voxel_params, "u_gi_light_voxel_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_clipmap_attr_params, "u_clipmap_attr_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_clipmap_compose_origin,
                          "u_clipmap_compose_origin",
                          gfx::uniform_type::Vec4);
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
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } attributes_program_;

    /// One-time per-texture mean capture (cs_gi_texture_mean.sc): samples a colour map's mip
    /// tail into the service's mean buffer. Dispatched from the pending queue, a bounded few
    /// per frame, BEFORE the attribute dispatches that read the buffer.
    struct texture_mean_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_texture_mean_params;
        gfx::program::uniform_ptr s_mean_source;

        void cache_uniforms()
        {
            cache_uniform(program.get(),
                          u_gi_texture_mean_params,
                          "u_gi_texture_mean_params",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_mean_source, "s_mean_source", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } texture_mean_program_;

    /// Generic uint-buffer fill (cs_gi_buffer_fill.sc): runs the one-time sentinel/cursor
    /// seeds the compute-writable buffers need, since bgfx forbids CPU updates on those.
    struct fill_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_buffer_fill_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(),
                          u_gi_buffer_fill_params,
                          "u_gi_buffer_fill_params",
                          gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } fill_program_;

    /// One-time diagnostics: captures flowing is the positive signal, helper shaders failing
    /// to compile is the silent-failure mode worth a loud line.
    bool capture_log_emitted_ = false;
    bool helper_warning_emitted_ = false;

    struct reset_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_surface_reset_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_surface_reset_params, "u_surface_reset_params", gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } reset_program_;
};

} // namespace unravel
