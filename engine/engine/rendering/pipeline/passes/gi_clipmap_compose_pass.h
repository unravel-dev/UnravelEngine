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
        /// The voxel box one dispatch composes (a scroll-only recompose composes the exposed
        /// slabs, a full one the whole level): xyz = min corner, w = reset the surface-list
        /// cursor (the level's first dispatch); size in xyz of the second.
        gfx::program::uniform_ptr u_clipmap_compose_range;
        gfx::program::uniform_ptr u_clipmap_compose_range_size;
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
            cache_uniform(program.get(),
                          u_clipmap_compose_range,
                          "u_clipmap_compose_range",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_clipmap_compose_range_size,
                          "u_clipmap_compose_range_size",
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
        /// xyz = the window's shift in attribute cells since the level's previous compose,
        /// w > 0.5 = scroll-only (surviving interior cells keep their attributes).
        gfx::program::uniform_ptr u_clipmap_attr_scroll;
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
            cache_uniform(program.get(), u_clipmap_attr_scroll, "u_clipmap_attr_scroll", gfx::uniform_type::Vec4);
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

    /// One-time zero of the light-voxel volume (cs_gi_volume_clear.sc), dispatched with the
    /// buffer seed: never-claimed texels are read (filtered neighbourhoods, level fallback)
    /// and fresh allocations are only zero where the driver zeroes them.
    struct volume_clear_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_volume_clear_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(),
                          u_gi_volume_clear_params,
                          "u_gi_volume_clear_params",
                          gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } volume_clear_program_;

    /// One-time zero of the world-probe atlases (cs_gi_atlas_clear.sc), same reasoning.
    struct atlas_clear_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_atlas_clear_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(),
                          u_gi_atlas_clear_params,
                          "u_gi_atlas_clear_params",
                          gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } atlas_clear_program_;

    /// One-time zero of the bounce visibility memo (cs_gi_vis_memo_clear.sc): generation 0
    /// means "never stamped", which only holds if the texels actually start at 0. Its own
    /// program because the volume clear writes rgba16f and the memo is an R16U uint image.
    struct vis_memo_clear_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_vis_memo_clear_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(),
                          u_gi_vis_memo_clear_params,
                          "u_gi_vis_memo_clear_params",
                          gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } vis_memo_clear_program_;

    /**
     * @brief Composes one dirty level's distance voxels: a scroll-only recompose copies the
     *        overlap of the old and new windows through @ref scroll_scratch_ and composes the
     *        exposed slabs; anything else composes the whole level.
     */
    void compose_level_voxels(const global_sdf_clipmap& clipmap,
                              const global_sdf_clipmap_gpu& clipmap_gpu,
                              surface_cache_system& surface_cache,
                              uint32_t level);

    /// Dispatches the compose kernel over one voxel box of @p level.
    void dispatch_compose_box(gfx::render_pass& pass,
                              const global_sdf_clipmap& clipmap,
                              const global_sdf_clipmap_gpu& clipmap_gpu,
                              surface_cache_system& surface_cache,
                              uint32_t level,
                              const global_sdf_clipmap::voxel_box& box,
                              bool reset_cursor);

    /// The staging copy of one level slab for a scroll-only recompose (R8, resolution^3): a
    /// blit cannot move voxels within one texture, so the slab goes out and the overlap
    /// comes back shifted. Recreated when the resolution changes.
    gfx::texture::ptr scroll_scratch_;

    /// One-time diagnostics: captures flowing is the positive signal, helper shaders failing
    /// to compile is the silent-failure mode worth a loud line.
    bool capture_log_emitted_ = false;
    bool helper_warning_emitted_ = false;
};

} // namespace unravel
