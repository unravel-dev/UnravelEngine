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
 * Takes the per-voxel loop of @c global_sdf_clipmap::compose_level off the main thread: composed on
 * the CPU, a level blocks the frame on the pool it dispatches to, which lands as a stutter whenever
 * the camera moves far enough to re-snap a level.
 *
 * The CPU composer is the reference implementation that @c sample and @c sample_ex read, which is
 * what the bake tests check this dispatch against, and the fallback when the compute program fails
 * to load.
 *
 * Only the voxel work runs here. Deciding WHICH levels to rebuild -- snapping, fingerprinting,
 * staleness ageing, the budget -- stays on the CPU in @c global_sdf_clipmap::update, because that
 * logic is subtle, tested, and identical either way.
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
        /// slabs, a full one the whole level): xyz = min corner; size in xyz of the second.
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
                          bgfx::UniformType::Vec4);
            cache_uniform(program.get(),
                          u_clipmap_compose_origin,
                          "u_clipmap_compose_origin",
                          bgfx::UniformType::Vec4);
            cache_uniform(program.get(),
                          u_clipmap_compose_range,
                          "u_clipmap_compose_range",
                          bgfx::UniformType::Vec4);
            cache_uniform(program.get(),
                          u_clipmap_compose_range_size,
                          "u_clipmap_compose_range_size",
                          bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", bgfx::UniformType::Vec4, gi::GI_SDF_GRID_PARAMS_VEC4);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", bgfx::UniformType::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } compose_program_;

    /**
     * @brief Composes one dirty level's distance voxels: a scroll-only recompose copies the
     *        overlap of the old and new windows through @ref scroll_scratch_ and composes the
     *        exposed slabs; anything else composes the whole level.
     */
    void compose_level_voxels(const global_sdf_clipmap& clipmap,
                              const global_sdf_clipmap_gpu& clipmap_gpu,
                              surface_cache_system& surface_cache,
                              uint32_t level,
                              gfx::render_pass& scroll_copy_pass,
                              gfx::render_pass& scroll_place_pass,
                              gfx::render_pass& compose_pass);

    /// One volume of a scroll-only recompose (the distance, or the coverage at its downsample): the level slab goes
    /// out to @p scratch and the overlap comes back shifted.
    struct scroll_volume
    {
        gfx::texture::ptr* scratch{};
        const gfx::texture::ptr* volume{};
        uint32_t downsample = 1;
    };

    /// Moves the overlap of a scroll-only recompose within @p target (scaled by its downsample). False when the scratch
    /// could not be created: the level then composes in full.
    static auto ensure_scroll_scratch(const scroll_volume& target, uint32_t resolution) -> bool;
    static void blit_scroll_overlap(const scroll_volume& target,
                                    uint32_t resolution,
                                    uint32_t level,
                                    const global_sdf_clipmap::voxel_box& overlap,
                                    const math::ivec3& shift,
                                    gfx::render_pass& copy_pass,
                                    gfx::render_pass& place_pass);

    /// Dispatches the compose kernel over one voxel box of @p level.
    void dispatch_compose_box(gfx::render_pass& pass,
                              const global_sdf_clipmap& clipmap,
                              const global_sdf_clipmap_gpu& clipmap_gpu,
                              surface_cache_system& surface_cache,
                              uint32_t level,
                              const global_sdf_clipmap::voxel_box& box);

    /// The staging copy of one level slab for a scroll-only recompose (R8, resolution^3): a
    /// blit cannot move voxels within one texture, so the slab goes out and the overlap
    /// comes back shifted. Recreated when the resolution changes. One per level, because
    /// every dirty level's copy is issued in the same view before any placement (the stable
    /// view layout in run()), so two scrolling levels must not share a scratch.
    std::array<gfx::texture::ptr, global_sdf_clipmap::level_count> scroll_scratch_{};
    /// The same for the coverage (global_sdf_clipmap_gpu::get_coverage_texture).
    std::array<gfx::texture::ptr, global_sdf_clipmap::level_count> coverage_scroll_scratch_{};

    /// One-time diagnostic: a compose program that failed to load is worth a loud line.
    bool invalid_warning_emitted_ = false;
};

} // namespace unravel
