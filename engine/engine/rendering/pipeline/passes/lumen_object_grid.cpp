#include "lumen_object_grid.h"

#include "lumen_pass_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/lumen_constants.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

#include <algorithm>

namespace unravel
{
namespace
{

/// Cells of 2 x 2 x 2 clipmap voxels, 4 x 4 x 4 threads per group.
constexpr uint32_t object_grid_downsample = 2;
constexpr uint32_t object_grid_group = 4;
/// Reach: the cell's half diagonal (1.44 x its half extent) plus 3 voxel extents.
constexpr float object_grid_diagonal = 1.44f;
constexpr float object_grid_range_voxel_extents = 3.0f;
/// Levels rebuilt in full per frame at most.
constexpr uint32_t object_grid_levels_per_frame = 2;
/// Groups per row of a brick dispatch: below the 65535 groups an axis takes.
constexpr uint32_t object_grid_max_bricks_per_row = 32768u;

} // namespace

void lumen_object_grid::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_object_grid, "u_lumen_object_grid", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_object_grid_origin, "u_lumen_object_grid_origin", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_brick_dispatch, "u_brick_dispatch", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_object_grid_levels, "u_lumen_object_grid_levels", bgfx::UniformType::Vec4, 4);
    cache_uniform(nullptr, u_lumen_object_grid_params, "u_lumen_object_grid_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_object_grid, "s_lumen_object_grid", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_atlas, "s_sdf_atlas", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_sdf_params, "u_sdf_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr,
                  u_sdf_grid_params,
                  "u_sdf_grid_params",
                  bgfx::UniformType::Vec4,
                  uint16_t(gi::GI_SDF_GRID_PARAMS_VEC4));
    cache_uniform(nullptr, s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
}

lumen_object_grid::~lumen_object_grid()
{
    lumen_pass::destroy_handle(boxes_);
}

auto lumen_object_grid::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    uniforms_.cache_uniforms();
    auto shader = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/cs_lumen_object_grid.sc");
    program_ = std::make_shared<gpu_program>(shader);
    dummy_ = std::make_shared<gfx::texture>(uint16_t(1),
                                            uint16_t(1),
                                            uint16_t(1),
                                            false,
                                            bgfx::TextureFormat::RGBA32F,
                                            BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    return is_ready();
}

auto lumen_object_grid::is_ready() const -> bool
{
    return program_ && program_->is_valid();
}

void lumen_object_grid::release()
{
    texture_.reset();
    resolution_ = 0;
    built_ = {};
}

auto lumen_object_grid::get_params(float card_bias_scale) const -> math::vec4
{
    return {float(resolution_), card_bias_scale, 0.0f, 0.0f};
}

void lumen_object_grid::bind(uint8_t stage, float card_bias_scale) const
{
    gfx::set_texture(uniforms_.s_lumen_object_grid, stage, texture_ ? texture_ : dummy_);
    gfx::set_uniform(uniforms_.u_lumen_object_grid_levels, levels_.data(), 4);
    gfx::set_uniform(uniforms_.u_lumen_object_grid_params, get_params(card_bias_scale));
}

void lumen_object_grid::bind_sdf_instances(const surface_cache_system& gi_scene) const
{
    auto& atlas = const_cast<surface_cache_system&>(gi_scene).get_atlas();
    gfx::set_texture(uniforms_.s_sdf_atlas, 0, atlas.get_atlas_texture());
    bgfx::setBuffer(1, atlas.get_header_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(2, atlas.get_indirection_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(3, gi_scene.get_instance_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(12, gi_scene.get_grid_buffer(), bgfx::Access::Read);
    gfx::set_uniform(uniforms_.u_sdf_grid_params, gi_scene.get_grid_params(), gi::GI_SDF_GRID_PARAMS_VEC4);
    const math::vec4 sdf_params(float(atlas.get_atlas_brick_dim()),
                                float(atlas.get_atlas_voxel_dim()),
                                float(gi_scene.get_instances().size()),
                                0.0f);
    gfx::set_uniform(uniforms_.u_sdf_params, sdf_params);
}

void lumen_object_grid::update(const surface_cache_system& gi_scene, const surface_cache_view& view_cache)
{
    if(!is_ready() || !view_cache.get_clipmap_gpu().is_valid())
    {
        return;
    }
    const auto& clipmap = view_cache.get_clipmap();
    const uint32_t resolution = view_cache.get_clipmap_gpu().get_resolution() / object_grid_downsample;
    if(gi_scene.get_instances().size() >= size_t(gi::lumen::LUMEN_OBJECT_GRID_MAX_ID) && !has_warned_ids_)
    {
        APPLOG_WARNING("[GI] {} GI instances: the object grid names {} at most; hits on the others read no cards.",
                       gi_scene.get_instances().size(),
                       gi::lumen::LUMEN_OBJECT_GRID_MAX_ID - 1);
        has_warned_ids_ = true;
    }
    if(resolution != resolution_ || !texture_)
    {
        resolution_ = resolution;
        texture_ = std::make_shared<gfx::texture>(uint16_t(resolution),
                                                  uint16_t(resolution),
                                                  uint16_t(resolution * global_sdf_clipmap::level_count),
                                                  false,
                                                  bgfx::TextureFormat::RGBA16,
                                                  BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_POINT |
                                                      BGFX_SAMPLER_UVW_CLAMP);
        built_ = {};
    }
    gfx::render_pass pass("GI/Object Grid");
    // The cells hold instance indices, positions in a list rebuilt every frame: when the order moved every level is
    // stale at once and is rebuilt this frame, whatever the per-frame budget.
    const uint64_t instance_order = gi_scene.get_instance_order_hash();
    const bool order_moved = std::any_of(built_.begin(),
                                         built_.end(),
                                         [&](const built_level& level)
                                         {
                                             return level.is_built && level.instance_order != instance_order;
                                         });
    const uint32_t budget = order_moved ? global_sdf_clipmap::level_count : object_grid_levels_per_frame;
    uint32_t rebuilt = 0;
    // Every rebuilt level's cell boxes go in one brick table, one dispatch per level.
    struct level_bricks
    {
        uint32_t level = 0;
        uint32_t first_box = 0;
        uint32_t box_count = 0;
        uint32_t bricks = 0;
    };
    std::vector<math::vec4> table;
    std::vector<level_bricks> levels;
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        const auto& lvl = clipmap.get_level(level);
        auto& built = built_[level];
        const bool is_same_layout = built.is_built && built.origin == lvl.origin && built.voxel_size == lvl.voxel_size &&
                                    built.instance_order == instance_order;
        if(lvl.voxel_size <= 0.0f || (is_same_layout && built.compose_serial == lvl.compose_serial))
        {
            continue;
        }
        // A level the clipmap recomposed partially in place since this grid mirrored it changed only in its boxes
        // (global_sdf_clipmap::level::partial_boxes): the cells within reach of an instance are inside them, as the
        // grid's reach is below the clipmap's. Those cells are rebuilt alone, outside the budget.
        const bool is_partial = is_same_layout && lvl.is_partial && lvl.scroll_shift == math::ivec3(0) &&
                                built.compose_serial + 1u == lvl.compose_serial;
        if(!is_partial && rebuilt >= budget)
        {
            continue;
        }
        std::vector<global_sdf_clipmap::voxel_box> cells;
        if(is_partial)
        {
            // Component-wise: glm's SIMD integer vector division needs an intrinsic this toolchain lacks.
            const int downsample = int(object_grid_downsample);
            const auto to_cells = [downsample](const math::ivec3& voxels, int round_up) -> math::ivec3
            {
                return {(voxels.x + round_up) / downsample,
                        (voxels.y + round_up) / downsample,
                        (voxels.z + round_up) / downsample};
            };
            for(const auto& box : lvl.partial_boxes)
            {
                cells.push_back({to_cells(box.min, 0), to_cells(box.size, downsample - 1)});
            }
        }
        else
        {
            cells.push_back({math::ivec3(0), math::ivec3(int(resolution))});
        }
        level_bricks entry;
        entry.level = level;
        entry.first_box = uint32_t(table.size() / 2u);
        entry.bricks = global_sdf_clipmap::append_brick_boxes(cells, int(object_grid_group), 0u, table);
        entry.box_count = uint32_t(table.size() / 2u) - entry.first_box;
        levels.push_back(entry);
        built = {lvl.origin, lvl.voxel_size, lvl.compose_serial, instance_order, true};
        levels_[level] = math::vec4(lvl.origin, lvl.voxel_size * float(object_grid_downsample));
        rebuilt += is_partial ? 0u : 1u;
    }
    if(levels.empty())
    {
        return;
    }
    lumen_pass::upload_vec4_table(boxes_, table);
    for(const auto& entry : levels)
    {
        if(entry.bricks == 0u)
        {
            continue;
        }
        const auto& lvl = clipmap.get_level(entry.level);
        const float cell_size = lvl.voxel_size * float(object_grid_downsample);
        const float voxel_extent = 0.5f * lvl.voxel_size;
        const float reach = object_grid_diagonal * 0.5f * cell_size + object_grid_range_voxel_extents * voxel_extent;
        program_->begin();
        bind_sdf_instances(gi_scene);
        // The level just composed: cells far from every surface skip their instance walk.
        const auto& clipmap_gpu = view_cache.get_clipmap_gpu();
        gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
        gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        gfx::set_image_3d(5, texture_->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16);
        bgfx::setBuffer(7, boxes_, bgfx::Access::Read);
        const math::vec4 params(float(entry.level), float(resolution), cell_size, reach);
        gfx::set_uniform(uniforms_.u_lumen_object_grid, params);
        // The objects the level's composition kept (global_sdf_clipmap::settings::object_radius_scale).
        const math::vec4 origin(lvl.origin, clipmap.get_settings().object_radius_scale);
        gfx::set_uniform(uniforms_.u_lumen_object_grid_origin, origin);
        const uint32_t row = std::min(entry.bricks, object_grid_max_bricks_per_row);
        const math::vec4 dispatch(float(entry.first_box), float(entry.box_count), float(entry.bricks), float(row));
        gfx::set_uniform(uniforms_.u_brick_dispatch, dispatch);
        bgfx::dispatch(pass.id, program_->native_handle(), row, (entry.bricks + row - 1u) / row, 1);
        program_->end();
    }
}

} // namespace unravel
