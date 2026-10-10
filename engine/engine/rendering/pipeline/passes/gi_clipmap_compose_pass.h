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
    gi_clipmap_compose_pass() = default;
    ~gi_clipmap_compose_pass();
    gi_clipmap_compose_pass(const gi_clipmap_compose_pass&) = delete;
    auto operator=(const gi_clipmap_compose_pass&) -> gi_clipmap_compose_pass& = delete;

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
        gfx::program::uniform_ptr u_clipmap_compose_scale;
        /// The dispatch's boxes in the brick table (gi/brick_dispatch.sh).
        gfx::program::uniform_ptr u_brick_dispatch;
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
            cache_uniform(program.get(), u_clipmap_compose_scale, "u_clipmap_compose_scale", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_brick_dispatch, "u_brick_dispatch", bgfx::UniformType::Vec4);
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

    /// cs_gi_clipmap_mip: one propagation pass of a level's coarse mip.
    struct mip_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_clipmap_mip_params;
        gfx::program::uniform_ptr u_clipmap_mip_mode;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_clipmap_mip_prev;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_clipmap_mip_params, "u_clipmap_mip_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_clipmap_mip_mode, "u_clipmap_mip_mode", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_clipmap_mip_prev, "s_clipmap_mip_prev", bgfx::UniformType::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } mip_program_;

    /**
     * @brief Rebuilds @p level's coarse mip from the level just composed (cs_gi_clipmap_mip passes): the first pass
     *        reads the level, the rest propagate the distance through the level's mip slab and the scratch, ending
     *        in the slab.
     */
    void build_level_mip(const global_sdf_clipmap_gpu& clipmap_gpu, uint32_t level, gfx::render_pass& pass);

    /**
     * @brief The voxel boxes one dirty level composes: a partial recompose copies the overlap of the old and new windows
     *        through @ref scroll_scratch_ when its origin moved and composes the exposed slabs and its partial boxes;
     *        anything else composes the whole level.
     */
    auto get_level_compose_boxes(const global_sdf_clipmap& clipmap,
                                 const global_sdf_clipmap_gpu& clipmap_gpu,
                                 uint32_t level,
                                 gfx::render_pass& scroll_copy_pass,
                                 gfx::render_pass& scroll_place_pass) -> std::vector<global_sdf_clipmap::voxel_box>;

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

    /// One level's share of the frame's brick table (@ref brick_boxes_).
    struct level_bricks
    {
        uint32_t level = 0;
        uint32_t first_box = 0;
        uint32_t box_count = 0;
        uint32_t bricks = 0;
    };

    /// Dispatches the compose kernel over the bricks of one level's boxes.
    void dispatch_compose_bricks(gfx::render_pass& pass,
                                 const global_sdf_clipmap& clipmap,
                                 const global_sdf_clipmap_gpu& clipmap_gpu,
                                 surface_cache_system& surface_cache,
                                 const level_bricks& bricks);

    /// The frame's brick table (global_sdf_clipmap::append_brick_boxes), every dirty level's boxes in one upload: bgfx
    /// applies buffer updates before the frame's dispatches, so one buffer cannot change between them.
    bgfx::DynamicVertexBufferHandle brick_boxes_{bgfx::kInvalidHandle};

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
