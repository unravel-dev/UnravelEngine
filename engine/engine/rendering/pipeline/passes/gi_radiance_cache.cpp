#include "gi_radiance_cache.h"

#include "gi_pass_common.h"
#include "gi_surface_cache_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_pass_constants.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace unravel
{
using namespace gi;

namespace
{
constexpr uint32_t clipmaps = uint32_t(GI_RADIANCE_CACHE_CLIPMAPS);
constexpr uint32_t grid = uint32_t(GI_RADIANCE_CACHE_GRID);
/// Mirror of GI_RC_INDIRECTION_SIZE (gi_radiance_cache_common.sh).
constexpr uint32_t indirection_size = grid * clipmaps * grid * grid;
constexpr uint32_t max_probes = uint32_t(GI_RADIANCE_CACHE_MAX_PROBES);
constexpr uint32_t atlas_probes_x = uint32_t(GI_RADIANCE_CACHE_ATLAS_PROBES_X);
constexpr uint32_t atlas_probes_y = max_probes / atlas_probes_x;
/// Mirror of u_gi_rc_final_res: a final atlas tile is the probe map plus a one-texel octahedral border.
constexpr uint32_t final_border_texels = 2u;
/// Mirror of GI_RC_MAX_TILES_PER_PROBE (the largest probe resolution's).
constexpr uint32_t max_tiles_per_probe = 16u;
/// Mirror of GI_RC_COUNTER_COUNT.
constexpr uint32_t counter_count = 32u;
/// Threads per group of the passes that run over the indirection (NUM_THREADS(64, 1, 1)).
constexpr uint32_t indirection_group = 64u;
/// The probe-state buffer holds LastUsed, LastTraced and the free list.
constexpr uint32_t probe_state_arrays = 3u;
/// Indirect dispatch slots: tile generation, trace, filter, and one group per traced probe (the SH).
constexpr uint16_t indirect_slots = 4;
/// A trace budget no frame reaches: a frame that does not continue the cache traces every probe.
constexpr float unlimited_budget = 1.0e9f;
/// The cache's frame counter travels as a float (u_gi_rc_params.x): it restarts below 2^24, where floats stop
/// holding every integer, and the cache rebuilds then (its probes' frame stamps would not compare across the restart).
constexpr uint32_t frame_counter_limit = 1u << 24u;
constexpr uint64_t storage_flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                   BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
/// The final atlas is read bilinearly; its octahedral border makes the filtering seamless.
constexpr uint64_t final_flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

/// vec4s per probe in the SH buffer: the red, green and blue L1 coefficients, and the cell they were projected at.
constexpr uint32_t sh_vec4s_per_probe = 4u;

using gi_pass::destroy_handle;
using gi_pass::get_vec4_layout;
using gi_pass::make_uint_buffer;
} // namespace

void gi_radiance_cache::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_gi_rc_clipmaps, "u_gi_rc_clipmaps", bgfx::UniformType::Vec4, uint16_t(clipmaps));
    cache_uniform(nullptr,
                  u_gi_rc_prev_clipmaps,
                  "u_gi_rc_prev_clipmaps",
                  bgfx::UniformType::Vec4,
                  uint16_t(clipmaps));
    cache_uniform(nullptr, u_gi_rc_params, "u_gi_rc_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_rc_camera, "u_gi_rc_camera", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_rc_layout, "u_gi_rc_layout", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_frame, "u_gi_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_probes, "u_gi_probes", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_view, "u_gi_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_settings, "u_gi_settings", bgfx::UniformType::Vec4);
    cache_uniform(nullptr,
                  u_sdf_clipmap_levels,
                  "u_sdf_clipmap_levels",
                  bgfx::UniformType::Vec4,
                  global_sdf_clipmap::level_count);
    cache_uniform(nullptr, u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_hit_lighting, "u_gi_hit_lighting", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_gi_probe_records, "s_gi_probe_records", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_env_sh, "s_gi_env_sh", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_rc_radiance, "s_gi_rc_radiance", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_rc_final, "s_gi_rc_final", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_coverage, "s_sdf_clipmap_coverage", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_mip, "s_sdf_clipmap_mip", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_gi_rc_points, "u_gi_rc_points", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_rc_sh_params, "u_gi_rc_sh_params", bgfx::UniformType::Vec4);
}

gi_radiance_cache::~gi_radiance_cache()
{
    release_resources();
}

auto gi_radiance_cache::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/gi/") + name + ".eshader");
        return std::make_shared<gpu_program>(shader);
    };
    clear_program_ = load("cs_gi_rc_clear");
    mark_program_ = load("cs_gi_rc_mark");
    update_program_ = load("cs_gi_rc_update");
    bookkeeping_program_ = load("cs_gi_rc_bookkeeping");
    allocate_program_ = load("cs_gi_rc_allocate");
    select_program_ = load("cs_gi_rc_select");
    tiles_program_ = load("cs_gi_rc_tiles");
    trace_program_ = load("cs_gi_rc_trace");
    filter_program_ = load("cs_gi_rc_filter");
    mark_points_program_ = load("cs_gi_rc_mark_points");
    sh_program_ = load("cs_gi_rc_sh");
    if(gi_pass::is_supported())
    {
        if(!has_programs())
        {
            APPLOG_WARNING("[GI] Radiance cache programs failed to load; the gather traces without it.");
        }
        else if(!has_surface_programs())
        {
            APPLOG_WARNING("[GI] The radiance cache programs of see-through surfaces failed to load; those surfaces take "
                           "their diffuse light from the sky.");
        }
    }
    return has_programs();
}

namespace
{
/// Whether every program of a list loaded.
auto are_gi_cache_programs_valid(std::initializer_list<const gpu_program::ptr*> programs) -> bool
{
    for(const auto* program : programs)
    {
        if(!*program || !(*program)->is_valid())
        {
            return false;
        }
    }
    return true;
}
} // namespace

auto gi_radiance_cache::has_programs() const -> bool
{
    return are_gi_cache_programs_valid({&clear_program_,
                                        &mark_program_,
                                        &update_program_,
                                        &bookkeeping_program_,
                                        &allocate_program_,
                                        &select_program_,
                                        &tiles_program_,
                                        &trace_program_,
                                        &filter_program_});
}

auto gi_radiance_cache::has_surface_programs() const -> bool
{
    return are_gi_cache_programs_valid({&mark_points_program_, &sh_program_});
}

auto gi_radiance_cache::ensure_resources(uint32_t probe_res) -> bool
{
    if(final_atlas_ && probe_res == probe_res_)
    {
        return true;
    }
    if(!bgfx::isValid(counters_))
    {
        indirection_[0] = make_uint_buffer(indirection_size);
        indirection_[1] = make_uint_buffer(indirection_size);
        probe_state_ = make_uint_buffer(probe_state_arrays * max_probes);
        counters_ = make_uint_buffer(counter_count);
        // Sized for a rebuild, which traces every probe of the pool (u_gi_rc_trace_cap).
        traces_ = make_uint_buffer(2u * max_probes);
        tiles_ = make_uint_buffer(2u * max_probes * max_tiles_per_probe);
        args_ = bgfx::createIndirectBuffer(indirect_slots);
        // Zeroed: a probe's SH reads as never projected (cs_gi_rc_sh.eshader's key) until it is.
        const std::vector<math::vec4> zero(size_t(sh_vec4s_per_probe) * max_probes, math::vec4(0.0f));
        sh_ = bgfx::createDynamicVertexBuffer(bgfx::copy(zero.data(), uint32_t(zero.size() * sizeof(math::vec4))),
                                              get_vec4_layout(),
                                              BGFX_BUFFER_COMPUTE_READ_WRITE);
    }
    // New atlases at another resolution start the cache over: no probe carries over.
    probe_res_ = probe_res;
    has_surfaces_ = false;
    const uint32_t final_res = probe_res + final_border_texels;
    radiance_atlas_ = std::make_shared<gfx::texture>(uint16_t(atlas_probes_x * probe_res),
                                                     uint16_t(atlas_probes_y * probe_res),
                                                     false,
                                                     1,
                                                     bgfx::TextureFormat::RGBA16F,
                                                     storage_flags);
    final_atlas_ = std::make_shared<gfx::texture>(uint16_t(atlas_probes_x * final_res),
                                                  uint16_t(atlas_probes_y * final_res),
                                                  false,
                                                  1,
                                                  bgfx::TextureFormat::RGBA16F,
                                                  final_flags);
    persistent_ = false;
    return true;
}

void gi_radiance_cache::release_resources()
{
    destroy_handle(indirection_[0]);
    destroy_handle(indirection_[1]);
    destroy_handle(probe_state_);
    destroy_handle(counters_);
    destroy_handle(traces_);
    destroy_handle(tiles_);
    destroy_handle(args_);
    destroy_handle(sh_);
    destroy_handle(surface_points_);
    has_surfaces_ = false;
    radiance_atlas_.reset();
    final_atlas_.reset();
}

namespace
{
/// A lattice cube of a clipmap, by the probe at its low corner: probes sit on the multiples of the cell size, and a
/// lookup inside the cube interpolates the eight at its corners.
using rc_lattice_cube = std::array<int64_t, 3>;

/// The cell size of @p clipmap as place_clipmaps snaps the grid by it.
auto get_rc_snap_cell_size(uint32_t clipmap) -> double
{
    return 2.0 * double(GI_RADIANCE_CACHE_EXTENT) / double(grid) * double(1u << clipmap);
}

/// The lattice cube of @p clipmap that holds @p position. A position past any world stays a countable cube.
auto get_rc_lattice_cube(const math::vec3& position, uint32_t clipmap) -> rc_lattice_cube
{
    constexpr double farthest_cube = 1.0e15;
    const double cell = get_rc_snap_cell_size(clipmap);
    const auto get_cube = [&](float along)
    {
        return int64_t(std::clamp(std::floor(double(along) / cell), -farthest_cube, farthest_cube));
    };
    return {get_cube(position.x), get_cube(position.y), get_cube(position.z)};
}

/// The first and last lattice cube along an axis that a clipmap covers when the camera is in cube @p camera_cube of
/// it: the grid holds the probes from half a grid below the camera's cube, and a cube needs the probe after its own
/// too (GiRcClipmapOf).
auto get_rc_first_covered_cube(int64_t camera_cube) -> int64_t
{
    return camera_cube - int64_t(grid) / 2;
}

auto get_rc_last_covered_cube(int64_t camera_cube) -> int64_t
{
    return camera_cube + int64_t(grid) / 2 - 2;
}

/// Whether a lookup inside @p cube reads its clipmap's probes, with the camera in @p camera_cube of that clipmap.
auto is_rc_cube_covered(const rc_lattice_cube& cube, const rc_lattice_cube& camera_cube) -> bool
{
    for(size_t axis = 0; axis < cube.size(); ++axis)
    {
        if(cube[axis] < get_rc_first_covered_cube(camera_cube[axis]) || cube[axis] > get_rc_last_covered_cube(camera_cube[axis]))
        {
            return false;
        }
    }
    return true;
}

/// The middle of @p cube of @p clipmap, with the clipmap @p marked_clipmap whose probes a point there marks as w
/// (make_volume_points).
auto get_rc_cube_middle(const rc_lattice_cube& cube, uint32_t clipmap, uint32_t marked_clipmap) -> math::vec4
{
    const double cell = get_rc_snap_cell_size(clipmap);
    return math::vec4(float((double(cube[0]) + 0.5) * cell),
                      float((double(cube[1]) + 0.5) * cell),
                      float((double(cube[2]) + 0.5) * cell),
                      float(marked_clipmap));
}

/**
 * @brief A point inside @p cube of @p clipmap that a lookup reads in that clipmap: the middle of the cube in the
 * finest clipmap, and in the others the middle of an eighth of it that the finer clipmap does not cover, the camera in
 * @p finer_camera_cube of that one (a point the finer one covers would mark the finer one's probes). False when the
 * finer clipmap covers the whole cube: no lookup inside reads this one.
 */
auto find_rc_volume_point(const rc_lattice_cube& cube, uint32_t clipmap, const rc_lattice_cube& finer_camera_cube, math::vec4& point) -> bool
{
    if(clipmap == 0)
    {
        point = get_rc_cube_middle(cube, 0, 0);
        return true;
    }
    for(int64_t eighth = 0; eighth < 8; ++eighth)
    {
        const rc_lattice_cube finer = {2 * cube[0] + (eighth & 1), 2 * cube[1] + ((eighth >> 1) & 1), 2 * cube[2] + (eighth >> 2)};
        if(!is_rc_cube_covered(finer, finer_camera_cube))
        {
            point = get_rc_cube_middle(finer, clipmap - 1, clipmap);
            return true;
        }
    }
    return false;
}
} // namespace

void gi_radiance_cache::volume_marks::clear()
{
    marked_.clear();
}

auto gi_radiance_cache::volume_marks::collect_new_probes(const math::vec4& point) -> bool
{
    // The point sits well inside its cube of the clipmap it marks (make_volume_points).
    const auto clipmap = uint32_t(point.w);
    const rc_lattice_cube cube = get_rc_lattice_cube(math::vec3(point), clipmap);
    bool has_new = false;
    for(int64_t corner = 0; corner < 8; ++corner)
    {
        const probe marked = {int64_t(clipmap), cube[0] + (corner & 1), cube[1] + ((corner >> 1) & 1), cube[2] + (corner >> 2)};
        if(!std::binary_search(marked_.begin(), marked_.end(), marked))
        {
            fresh_.push_back(marked);
            has_new = true;
        }
    }
    return has_new;
}

auto gi_radiance_cache::volume_marks::add(const std::vector<math::vec4>& candidates,
                                          size_t max_points,
                                          size_t max_probes,
                                          std::vector<math::vec4>& points) -> bool
{
    const size_t first_point = points.size();
    fresh_.clear();
    for(const auto& candidate : candidates)
    {
        if(collect_new_probes(candidate))
        {
            points.push_back(candidate);
        }
    }
    // Neighbouring cubes of the box share corners.
    std::sort(fresh_.begin(), fresh_.end());
    fresh_.erase(std::unique(fresh_.begin(), fresh_.end()), fresh_.end());
    if(points.size() - first_point > max_points || marked_.size() + fresh_.size() > max_probes)
    {
        points.resize(first_point);
        return false;
    }
    merged_.resize(marked_.size() + fresh_.size());
    std::merge(marked_.begin(), marked_.end(), fresh_.begin(), fresh_.end(), merged_.begin());
    marked_.swap(merged_);
    return true;
}

auto gi_radiance_cache::get_cell_size(uint32_t clipmap) -> float
{
    return float(get_rc_snap_cell_size(clipmap));
}

auto gi_radiance_cache::get_reach(uint32_t clipmap) -> float
{
    // The grid holds the probes from half a grid below the camera's lattice cube, and the camera sits up to a cell
    // into that cube: a lookup reads the clipmap up to half a grid and a cell away on that side.
    return (0.5f * float(grid) + 1.0f) * get_cell_size(clipmap);
}

auto gi_radiance_cache::get_inner_reach(uint32_t clipmap) -> float
{
    return (0.5f * float(grid) - 2.0f) * get_cell_size(clipmap);
}

auto gi_radiance_cache::make_surface_points(const math::bbox& bounds,
                                            const math::vec3& eye,
                                            size_t budget,
                                            float spacing,
                                            std::vector<math::vec4>& points) -> bool
{
    // Each clipmap's share of the box, a cell apart in that clipmap: a point marks the eight probes around it, so
    // neighbours a cell apart mark every probe a surface between them interpolates. The shares overlap where the
    // snapped clipmaps' edges may fall, so the finer clipmap's points reach as far as it may cover. A box several cells
    // wide on every axis keeps the points near its faces: its inside is mostly empty.
    const math::vec3 size = bounds.max - bounds.min;
    for(uint32_t clipmap = 0; clipmap < uint32_t(GI_RADIANCE_CACHE_CLIPMAPS); ++clipmap)
    {
        const float cell = get_cell_size(clipmap) * spacing;
        const float reach = get_reach(clipmap);
        const float inner = clipmap == 0 ? 0.0f : get_inner_reach(clipmap - 1);
        const math::vec3 low = math::max(bounds.min, eye - reach);
        const math::vec3 high = math::min(bounds.max, eye + reach);
        if(math::any(math::greaterThan(low, high)))
        {
            continue;
        }
        const bool faces_only = math::all(math::greaterThan(size, math::vec3(4.0f * cell)));
        const math::vec3 extent = high - low;
        const math::ivec3 counts = math::ivec3(math::floor(extent / cell)) + 1;
        const math::vec3 last = math::max(math::vec3(counts - 1), math::vec3(1.0f));
        const math::vec3 is_spread = math::vec3(math::greaterThan(counts, math::ivec3(1)));
        for(int z = 0; z < counts.z; ++z)
        {
            for(int y = 0; y < counts.y; ++y)
            {
                for(int x = 0; x < counts.x; ++x)
                {
                    const math::vec3 along = math::mix(math::vec3(0.5f), math::vec3{float(x), float(y), float(z)} / last, is_spread);
                    const math::vec3 point = low + extent * along;
                    const bool is_finer = clipmap > 0 && math::all(math::lessThan(math::abs(point - eye), math::vec3(inner)));
                    const math::vec3 to_faces = math::min(point - bounds.min, bounds.max - point);
                    const bool is_inside = faces_only && math::all(math::greaterThan(to_faces, math::vec3(cell)));
                    if(is_finer || is_inside)
                    {
                        continue;
                    }
                    if(points.size() >= budget)
                    {
                        return false;
                    }
                    points.emplace_back(point, 1.0f);
                }
            }
        }
    }
    return true;
}

auto gi_radiance_cache::make_volume_points(const math::bbox& bounds, const math::vec3& eye, size_t budget, std::vector<math::vec4>& points)
    -> bool
{
    for(uint32_t clipmap = 0; clipmap < clipmaps; ++clipmap)
    {
        // The lattice cubes of this clipmap that the box reaches and the clipmap covers.
        const rc_lattice_cube camera_cube = get_rc_lattice_cube(eye, clipmap);
        const rc_lattice_cube finer_camera_cube = clipmap > 0 ? get_rc_lattice_cube(eye, clipmap - 1) : rc_lattice_cube{};
        rc_lattice_cube first = get_rc_lattice_cube(bounds.min, clipmap);
        rc_lattice_cube last = get_rc_lattice_cube(bounds.max, clipmap);
        uint64_t cube_count = 1;
        for(size_t axis = 0; axis < first.size(); ++axis)
        {
            first[axis] = std::max(first[axis], get_rc_first_covered_cube(camera_cube[axis]));
            last[axis] = std::min(last[axis], get_rc_last_covered_cube(camera_cube[axis]));
            cube_count *= uint64_t(std::max<int64_t>(last[axis] - first[axis] + 1, 0));
        }
        // Every cube of the finest clipmap takes a point: a box of more cubes than the budget is refused unwalked.
        if(clipmap == 0 && points.size() + cube_count > budget)
        {
            return false;
        }
        for(int64_t z = first[2]; z <= last[2]; ++z)
        {
            for(int64_t y = first[1]; y <= last[1]; ++y)
            {
                for(int64_t x = first[0]; x <= last[0]; ++x)
                {
                    math::vec4 point;
                    if(!find_rc_volume_point({x, y, z}, clipmap, finer_camera_cube, point))
                    {
                        continue;
                    }
                    if(points.size() >= budget)
                    {
                        return false;
                    }
                    points.push_back(point);
                }
            }
        }
    }
    return true;
}

void gi_radiance_cache::place_clipmaps(const math::vec3& camera)
{
    prev_clipmaps_ = clipmaps_;
    const double half_grid = 0.5 * double(grid);
    for(uint32_t clipmap = 0; clipmap < clipmaps; ++clipmap)
    {
        const double cell = get_rc_snap_cell_size(clipmap);
        for(int axis = 0; axis < 3; ++axis)
        {
            // Probes sit on multiples of the cell: the lattice point nearest below the camera is cell 24.
            const double snapped = std::floor(double(camera[axis]) / cell) * cell;
            clipmaps_[clipmap * 4u + uint32_t(axis)] = float(snapped - 0.5 * cell - half_grid * cell);
        }
        clipmaps_[clipmap * 4u + 3u] = float(cell);
    }
}

void gi_radiance_cache::bind_counters(uint8_t stage) const
{
    bgfx::setBuffer(stage, counters_, bgfx::Access::Read);
}

auto gi_radiance_cache::get_trace_cost_budget() const -> float
{
    return persistent_ ? float(trace_budget_ * uint32_t(GI_RADIANCE_CACHE_COST_NORMAL)) : unlimited_budget;
}

void gi_radiance_cache::set_cache_uniforms(bookkeeping mode) const
{
    gfx::set_uniform(uniforms_.u_gi_rc_clipmaps, clipmaps_.data(), uint16_t(clipmaps));
    gfx::set_uniform(uniforms_.u_gi_rc_prev_clipmaps, prev_clipmaps_.data(), uint16_t(clipmaps));
    const float budget = get_trace_cost_budget();
    const float params[4] = {float(frame_), budget, persistent_ ? 1.0f : 0.0f, float(int(mode))};
    gfx::set_uniform(uniforms_.u_gi_rc_params, params);
    gfx::set_uniform(uniforms_.u_gi_rc_camera, camera_.data());
    set_layout_uniform();
}

void gi_radiance_cache::set_layout_uniform() const
{
    const math::vec4 layout(float(probe_res_), 0.0f, 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_gi_rc_layout, layout);
}

void gi_radiance_cache::run_bookkeeping(bookkeeping mode) const
{
    gfx::render_pass pass("GI/Cache Bookkeeping");
    bookkeeping_program_->begin();
    bgfx::setBuffer(0, counters_, bgfx::Access::ReadWrite);
    bgfx::setBuffer(1, args_, bgfx::Access::ReadWrite);
    set_cache_uniforms(mode);
    gfx::dispatch(pass.id, bookkeeping_program_->native_handle(), 1, 1, 1);
    bookkeeping_program_->end();
}

void gi_radiance_cache::run_over_indirection(gpu_program& program, const char* name) const
{
    gfx::render_pass pass(name);
    program.begin();
    const auto& current = indirection_[current_];
    const auto& previous = indirection_[current_ ^ 1u];
    // Every indirection pass sees the same five buffers; each shader declares the ones it uses.
    bgfx::setBuffer(0, current, bgfx::Access::ReadWrite);
    if(&program == clear_program_.get() || &program == update_program_.get())
    {
        bgfx::setBuffer(1, previous, bgfx::Access::ReadWrite);
        bgfx::setBuffer(2, probe_state_, bgfx::Access::ReadWrite);
        bgfx::setBuffer(3, counters_, bgfx::Access::ReadWrite);
    }
    else
    {
        bgfx::setBuffer(1, probe_state_, bgfx::Access::ReadWrite);
        bgfx::setBuffer(2, counters_, bgfx::Access::ReadWrite);
        bgfx::setBuffer(3, traces_, bgfx::Access::ReadWrite);
    }
    set_cache_uniforms(bookkeeping::frame_start);
    gfx::dispatch(pass.id, program.native_handle(), (indirection_size + indirection_group - 1u) / indirection_group, 1, 1);
    program.end();
}

void gi_radiance_cache::run_mark(const frame_inputs& inputs) const
{
    gfx::render_pass pass("GI/Cache Mark");
    pass.set_view_proj(inputs.params->cam->get_view(), inputs.params->cam->get_projection_unjittered());
    mark_program_->begin();
    gfx::set_texture(uniforms_.s_gi_probe_records, 0, inputs.probe_records);
    bgfx::setBuffer(1, indirection_[current_], bgfx::Access::ReadWrite);
    gfx::set_uniform(uniforms_.u_gi_frame, inputs.frame);
    gfx::set_uniform(uniforms_.u_gi_probes, inputs.probes);
    gfx::set_uniform(uniforms_.u_gi_view, inputs.view);
    set_cache_uniforms(bookkeeping::frame_start);
    gfx::dispatch(pass.id, mark_program_->native_handle(), (inputs.probes_x + 7u) / 8u, (inputs.probe_rows + 7u) / 8u, 1);
    mark_program_->end();
}

auto gi_radiance_cache::has_surface_points(const frame_inputs& inputs) -> bool
{
    return inputs.surface_points != nullptr && !inputs.surface_points->empty();
}

void gi_radiance_cache::run_mark_surfaces(const frame_inputs& inputs)
{
    if(!has_surface_points(inputs))
    {
        return;
    }
    const auto& points = *inputs.surface_points;
    const uint32_t count = std::min(uint32_t(points.size()), max_surface_points);
    gi_pass::upload_vec4_table(surface_points_, points);
    gfx::render_pass pass("GI/Cache Mark Surfaces");
    mark_points_program_->begin();
    bgfx::setBuffer(0, surface_points_, bgfx::Access::Read);
    bgfx::setBuffer(1, indirection_[current_], bgfx::Access::ReadWrite);
    gfx::set_uniform(uniforms_.u_gi_rc_points, math::vec4(float(count), 0.0f, 0.0f, 0.0f));
    set_cache_uniforms(bookkeeping::frame_start);
    gfx::dispatch(pass.id, mark_points_program_->native_handle(), (count + indirection_group - 1u) / indirection_group, 1, 1);
    mark_points_program_->end();
}

void gi_radiance_cache::run_trace(const frame_inputs& inputs) const
{
    const auto& params = *inputs.params;
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    {
        gfx::render_pass pass("GI/Cache Tiles");
        tiles_program_->begin();
        bgfx::setBuffer(0, counters_, bgfx::Access::ReadWrite);
        bgfx::setBuffer(1, traces_, bgfx::Access::Read);
        bgfx::setBuffer(2, tiles_, bgfx::Access::Write);
        set_cache_uniforms(bookkeeping::frame_start);
        gfx::dispatch_indirect(pass.id, tiles_program_->native_handle(), args_, 0, 1);
        tiles_program_->end();
    }
    run_bookkeeping(bookkeeping::trace_args);
    gfx::render_pass pass("GI/Cache Trace");
    trace_program_->begin();
    bgfx::setBuffer(0, traces_, bgfx::Access::Read);
    bgfx::setBuffer(1, tiles_, bgfx::Access::Read);
    bgfx::setBuffer(2, counters_, bgfx::Access::Read);
    gfx::set_texture(uniforms_.s_gi_env_sh,
                     3,
                     params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture());
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    bgfx::setImage(5, radiance_atlas_->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    const uint64_t experiments = params.surface_cache ? params.surface_cache->get_experiment_flags() : 0u;
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, gi_pass::get_sdf_coverage(clipmap_gpu, experiments));
    gfx::set_texture(uniforms_.s_sdf_clipmap_mip, 9, clipmap_gpu.get_mip_texture());
    if(params.gi_surface_cache != nullptr)
    {
        params.gi_surface_cache->bind_for_sampling(13, 14, 15, true);
    }
    else
    {
        const math::vec4 no_cards(0.0f);
        gfx::set_uniform(uniforms_.u_gi_hit_lighting, no_cards);
    }
    set_cache_uniforms(bookkeeping::frame_start);
    // The trace's dithered transparency reads the frame (u_gi_frame.y), its length the maximum trace distance.
    gfx::set_uniform(uniforms_.u_gi_frame, inputs.frame);
    gfx::set_uniform(uniforms_.u_gi_settings,
                     gi_pass::make_settings_uniform(inputs.params->settings, inputs.params->is_being_edited).data());
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    gfx::dispatch_indirect(pass.id, trace_program_->native_handle(), args_, 1, 1);
    trace_program_->end();
}

void gi_radiance_cache::run_sh(bool is_full) const
{
    gfx::render_pass pass("GI/Cache SH");
    sh_program_->begin();
    gfx::set_texture(uniforms_.s_gi_rc_final, 0, final_atlas_);
    bgfx::setBuffer(3, traces_, bgfx::Access::Read);
    bgfx::setBuffer(4, sh_, bgfx::Access::ReadWrite);
    set_cache_uniforms(bookkeeping::frame_start);
    gfx::set_uniform(uniforms_.u_gi_rc_sh_params, math::vec4(is_full ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f));
    if(is_full)
    {
        gfx::dispatch(pass.id, sh_program_->native_handle(), 1, 1, max_probes);
    }
    else
    {
        gfx::dispatch_indirect(pass.id, sh_program_->native_handle(), args_, 3, 1);
    }
    sh_program_->end();
}

void gi_radiance_cache::run_filter() const
{
    gfx::render_pass pass("GI/Cache Filter");
    filter_program_->begin();
    gfx::set_texture(uniforms_.s_gi_rc_radiance, 0, radiance_atlas_);
    bgfx::setBuffer(2, indirection_[current_], bgfx::Access::Read);
    bgfx::setBuffer(3, traces_, bgfx::Access::Read);
    bgfx::setImage(5, final_atlas_->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    set_cache_uniforms(bookkeeping::frame_start);
    gfx::dispatch_indirect(pass.id, filter_program_->native_handle(), args_, 2, 1);
    filter_program_->end();
}

auto gi_radiance_cache::update(const frame_inputs& inputs) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Radiance Cache");
    if(!has_programs() || !inputs.params || !inputs.params->cam || !inputs.params->view_cache || !inputs.probe_records)
    {
        return false;
    }
    const auto& clipmap_gpu = inputs.params->view_cache->get_clipmap_gpu();
    if(!clipmap_gpu.is_valid() || !ensure_resources(inputs.probe_resolution))
    {
        return false;
    }
    if(inputs.params->global_lighting_change)
    {
        persistent_ = false;
    }
    current_ ^= 1u;
    if(++frame_ >= frame_counter_limit)
    {
        frame_ = 1;
        persistent_ = false;
    }
    trace_budget_ = gi_pass::get_radiance_cache_trace_budget(inputs.params->settings.diffuse.update_speed,
                                                             inputs.params->is_being_edited,
                                                             inputs.params->gi_quality);
    place_clipmaps(inputs.params->cam->get_position());
    if(!persistent_)
    {
        // Nothing to carry over: last frame's clipmaps are this frame's.
        prev_clipmaps_ = clipmaps_;
    }
    const auto camera = inputs.params->cam->get_position();
    camera_ = {camera.x, camera.y, camera.z, 0.0f};
    run_over_indirection(*clear_program_, "GI/Cache Clear");
    if(!persistent_)
    {
        run_bookkeeping(bookkeeping::reset);
    }
    run_mark(inputs);
    const bool serves_surfaces = has_surface_programs();
    if(serves_surfaces)
    {
        run_mark_surfaces(inputs);
    }
    run_over_indirection(*update_program_, "GI/Cache Update");
    run_bookkeeping(bookkeeping::frame_start);
    run_over_indirection(*allocate_program_, "GI/Cache Allocate");
    run_bookkeeping(bookkeeping::budget);
    run_over_indirection(*select_program_, "GI/Cache Select");
    run_bookkeeping(bookkeeping::trace_list);
    run_trace(inputs);
    run_filter();
    // The SH runs from the first frame surfaces read it: the whole pool that frame (its probes were traced while nothing
    // projected them), then the traced probes every frame, so surfaces coming back into view find it current.
    if(serves_surfaces && (has_surface_points(inputs) || has_surfaces_))
    {
        run_sh(!has_surfaces_);
        has_surfaces_ = true;
    }
    persistent_ = true;
    return true;
}

void gi_radiance_cache::bind_for_surfaces(uint8_t indirection_stage, uint8_t sh_stage) const
{
    bgfx::setBuffer(indirection_stage, indirection_[current_], bgfx::Access::Read);
    bgfx::setBuffer(sh_stage, sh_, bgfx::Access::Read);
    gfx::set_uniform(uniforms_.u_gi_rc_clipmaps, clipmaps_.data(), uint16_t(clipmaps));
    set_layout_uniform();
}

void gi_radiance_cache::bind_for_sampling(uint8_t indirection_stage, uint8_t final_stage) const
{
    bgfx::setBuffer(indirection_stage, indirection_[current_], bgfx::Access::Read);
    gfx::set_texture(uniforms_.s_gi_rc_final, final_stage, final_atlas_);
    gfx::set_uniform(uniforms_.u_gi_rc_clipmaps, clipmaps_.data(), uint16_t(clipmaps));
    set_layout_uniform();
}

} // namespace unravel
