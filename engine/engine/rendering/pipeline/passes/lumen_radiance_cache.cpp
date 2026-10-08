#include "lumen_radiance_cache.h"

#include "lumen_pass_common.h"
#include "lumen_surface_cache_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

#include <cmath>

namespace unravel
{
using namespace gi::lumen;

namespace
{
constexpr uint32_t clipmaps = uint32_t(LUMEN_RADIANCE_CACHE_CLIPMAPS);
constexpr uint32_t grid = uint32_t(LUMEN_RADIANCE_CACHE_GRID);
/// Mirror of LUMEN_RC_INDIRECTION_SIZE (lumen_radiance_cache_common.sh).
constexpr uint32_t indirection_size = grid * clipmaps * grid * grid;
constexpr uint32_t max_probes = uint32_t(LUMEN_RADIANCE_CACHE_MAX_PROBES);
constexpr uint32_t atlas_probes_x = uint32_t(LUMEN_RADIANCE_CACHE_ATLAS_PROBES_X);
constexpr uint32_t atlas_probes_y = max_probes / atlas_probes_x;
/// Mirror of u_lumen_rc_final_res: a final atlas tile is the probe map plus a one-texel octahedral border.
constexpr uint32_t final_border_texels = 2u;
/// Mirror of LUMEN_RC_MAX_TILES_PER_PROBE (the largest probe resolution's).
constexpr uint32_t max_tiles_per_probe = 16u;
/// Mirror of LUMEN_RC_COUNTER_COUNT.
constexpr uint32_t counter_count = 32u;
/// Threads per group of the passes that run over the indirection (NUM_THREADS(64, 1, 1)).
constexpr uint32_t indirection_group = 64u;
/// The probe-state buffer holds LastUsed, LastTraced and the free list.
constexpr uint32_t probe_state_arrays = 3u;
/// Indirect dispatch slots: tile generation, trace, filter.
constexpr uint16_t indirect_slots = 3;
/// A trace budget no frame reaches: a frame that does not continue the cache traces every probe.
constexpr float unlimited_budget = 1.0e9f;
/// The cache's frame counter travels as a float (u_lumen_rc_params.x): it restarts below 2^24, where floats stop
/// holding every integer, and the cache rebuilds then (its probes' frame stamps would not compare across the restart).
constexpr uint32_t frame_counter_limit = 1u << 24u;
constexpr uint64_t storage_flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                   BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
/// The final atlas is read bilinearly; its octahedral border makes the filtering seamless.
constexpr uint64_t final_flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

using lumen_pass::destroy_handle;
using lumen_pass::make_uint_buffer;
} // namespace

void lumen_radiance_cache::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_rc_clipmaps, "u_lumen_rc_clipmaps", bgfx::UniformType::Vec4, uint16_t(clipmaps));
    cache_uniform(nullptr,
                  u_lumen_rc_prev_clipmaps,
                  "u_lumen_rc_prev_clipmaps",
                  bgfx::UniformType::Vec4,
                  uint16_t(clipmaps));
    cache_uniform(nullptr, u_lumen_rc_params, "u_lumen_rc_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_rc_camera, "u_lumen_rc_camera", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_rc_layout, "u_lumen_rc_layout", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_frame, "u_lumen_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_probes, "u_lumen_probes", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_view, "u_lumen_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_settings, "u_lumen_settings", bgfx::UniformType::Vec4);
    cache_uniform(nullptr,
                  u_sdf_clipmap_levels,
                  "u_sdf_clipmap_levels",
                  bgfx::UniformType::Vec4,
                  global_sdf_clipmap::level_count);
    cache_uniform(nullptr, u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_hit_lighting, "u_lumen_hit_lighting", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_probe_records, "s_lumen_probe_records", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_env_sh, "s_lumen_env_sh", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_rc_radiance, "s_lumen_rc_radiance", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_rc_final, "s_lumen_rc_final", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_coverage, "s_sdf_clipmap_coverage", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_mip, "s_sdf_clipmap_mip", bgfx::UniformType::Sampler);
}

lumen_radiance_cache::~lumen_radiance_cache()
{
    release_resources();
}

auto lumen_radiance_cache::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/lumen/") + name + ".sc");
        return std::make_shared<gpu_program>(shader);
    };
    clear_program_ = load("cs_lumen_rc_clear");
    mark_program_ = load("cs_lumen_rc_mark");
    update_program_ = load("cs_lumen_rc_update");
    bookkeeping_program_ = load("cs_lumen_rc_bookkeeping");
    allocate_program_ = load("cs_lumen_rc_allocate");
    select_program_ = load("cs_lumen_rc_select");
    tiles_program_ = load("cs_lumen_rc_tiles");
    trace_program_ = load("cs_lumen_rc_trace");
    filter_program_ = load("cs_lumen_rc_filter");
    if(!has_programs())
    {
        APPLOG_WARNING("[GI] Radiance cache programs failed to load; the gather traces without it.");
    }
    return has_programs();
}

auto lumen_radiance_cache::has_programs() const -> bool
{
    for(const auto* program : {&clear_program_,
                               &mark_program_,
                               &update_program_,
                               &bookkeeping_program_,
                               &allocate_program_,
                               &select_program_,
                               &tiles_program_,
                               &trace_program_,
                               &filter_program_})
    {
        if(!*program || !(*program)->is_valid())
        {
            return false;
        }
    }
    return true;
}

auto lumen_radiance_cache::ensure_resources(uint32_t probe_res) -> bool
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
        // Sized for a rebuild, which traces every probe of the pool (u_lumen_rc_trace_cap).
        traces_ = make_uint_buffer(2u * max_probes);
        tiles_ = make_uint_buffer(2u * max_probes * max_tiles_per_probe);
        args_ = bgfx::createIndirectBuffer(indirect_slots);
    }
    // New atlases at another resolution start the cache over: no probe carries over.
    probe_res_ = probe_res;
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

void lumen_radiance_cache::release_resources()
{
    destroy_handle(indirection_[0]);
    destroy_handle(indirection_[1]);
    destroy_handle(probe_state_);
    destroy_handle(counters_);
    destroy_handle(traces_);
    destroy_handle(tiles_);
    destroy_handle(args_);
    radiance_atlas_.reset();
    final_atlas_.reset();
}

void lumen_radiance_cache::place_clipmaps(const math::vec3& camera)
{
    prev_clipmaps_ = clipmaps_;
    const double cell_0 = 2.0 * double(LUMEN_RADIANCE_CACHE_EXTENT) / double(grid);
    const double half_grid = 0.5 * double(grid);
    for(uint32_t clipmap = 0; clipmap < clipmaps; ++clipmap)
    {
        const double cell = cell_0 * double(1u << clipmap);
        for(int axis = 0; axis < 3; ++axis)
        {
            // Probes sit on multiples of the cell: the lattice point nearest below the camera is cell 24.
            const double snapped = std::floor(double(camera[axis]) / cell) * cell;
            clipmaps_[clipmap * 4u + uint32_t(axis)] = float(snapped - 0.5 * cell - half_grid * cell);
        }
        clipmaps_[clipmap * 4u + 3u] = float(cell);
    }
}

void lumen_radiance_cache::bind_counters(uint8_t stage) const
{
    bgfx::setBuffer(stage, counters_, bgfx::Access::Read);
}

auto lumen_radiance_cache::get_trace_cost_budget() const -> float
{
    return persistent_ ? float(trace_budget_ * uint32_t(LUMEN_RADIANCE_CACHE_COST_NORMAL)) : unlimited_budget;
}

void lumen_radiance_cache::set_cache_uniforms(bookkeeping mode) const
{
    gfx::set_uniform(uniforms_.u_lumen_rc_clipmaps, clipmaps_.data(), uint16_t(clipmaps));
    gfx::set_uniform(uniforms_.u_lumen_rc_prev_clipmaps, prev_clipmaps_.data(), uint16_t(clipmaps));
    const float budget = get_trace_cost_budget();
    const float params[4] = {float(frame_), budget, persistent_ ? 1.0f : 0.0f, float(int(mode))};
    gfx::set_uniform(uniforms_.u_lumen_rc_params, params);
    gfx::set_uniform(uniforms_.u_lumen_rc_camera, camera_.data());
    set_layout_uniform();
}

void lumen_radiance_cache::set_layout_uniform() const
{
    const math::vec4 layout(float(probe_res_), 0.0f, 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_rc_layout, layout);
}

void lumen_radiance_cache::run_bookkeeping(bookkeeping mode) const
{
    gfx::render_pass pass("GI/Cache Bookkeeping");
    bookkeeping_program_->begin();
    bgfx::setBuffer(0, counters_, bgfx::Access::ReadWrite);
    bgfx::setBuffer(1, args_, bgfx::Access::ReadWrite);
    set_cache_uniforms(mode);
    bgfx::dispatch(pass.id, bookkeeping_program_->native_handle(), 1, 1, 1);
    bookkeeping_program_->end();
}

void lumen_radiance_cache::run_over_indirection(gpu_program& program, const char* name) const
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
    bgfx::dispatch(pass.id, program.native_handle(), (indirection_size + indirection_group - 1u) / indirection_group, 1, 1);
    program.end();
}

void lumen_radiance_cache::run_mark(const frame_inputs& inputs) const
{
    gfx::render_pass pass("GI/Cache Mark");
    pass.set_view_proj(inputs.params->cam->get_view(), inputs.params->cam->get_projection_unjittered());
    mark_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_probe_records, 0, inputs.probe_records);
    bgfx::setBuffer(1, indirection_[current_], bgfx::Access::ReadWrite);
    gfx::set_uniform(uniforms_.u_lumen_frame, inputs.frame);
    gfx::set_uniform(uniforms_.u_lumen_probes, inputs.probes);
    gfx::set_uniform(uniforms_.u_lumen_view, inputs.view);
    set_cache_uniforms(bookkeeping::frame_start);
    bgfx::dispatch(pass.id, mark_program_->native_handle(), (inputs.probes_x + 7u) / 8u, (inputs.probe_rows + 7u) / 8u, 1);
    mark_program_->end();
}

void lumen_radiance_cache::run_trace(const frame_inputs& inputs) const
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
        bgfx::dispatch(pass.id, tiles_program_->native_handle(), args_, 0, 1);
        tiles_program_->end();
    }
    run_bookkeeping(bookkeeping::trace_args);
    gfx::render_pass pass("GI/Cache Trace");
    trace_program_->begin();
    bgfx::setBuffer(0, traces_, bgfx::Access::Read);
    bgfx::setBuffer(1, tiles_, bgfx::Access::Read);
    bgfx::setBuffer(2, counters_, bgfx::Access::Read);
    gfx::set_texture(uniforms_.s_lumen_env_sh,
                     3,
                     params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture());
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    bgfx::setImage(5, radiance_atlas_->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    const uint64_t experiments = params.surface_cache ? params.surface_cache->get_experiment_flags() : 0u;
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, lumen_pass::get_sdf_coverage(clipmap_gpu, experiments));
    gfx::set_texture(uniforms_.s_sdf_clipmap_mip, 9, clipmap_gpu.get_mip_texture());
    if(params.lumen_surface_cache != nullptr)
    {
        params.lumen_surface_cache->bind_for_sampling(13, 14, 15, true);
    }
    else
    {
        const math::vec4 no_cards(0.0f);
        gfx::set_uniform(uniforms_.u_lumen_hit_lighting, no_cards);
    }
    set_cache_uniforms(bookkeeping::frame_start);
    // The trace's dithered transparency reads the frame (u_lumen_frame.y), its length the maximum trace distance.
    gfx::set_uniform(uniforms_.u_lumen_frame, inputs.frame);
    gfx::set_uniform(uniforms_.u_lumen_settings,
                     lumen_pass::make_settings_uniform(inputs.params->settings, inputs.params->is_being_edited).data());
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    bgfx::dispatch(pass.id, trace_program_->native_handle(), args_, 1, 1);
    trace_program_->end();
}

void lumen_radiance_cache::run_filter() const
{
    gfx::render_pass pass("GI/Cache Filter");
    filter_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_rc_radiance, 0, radiance_atlas_);
    bgfx::setBuffer(2, indirection_[current_], bgfx::Access::Read);
    bgfx::setBuffer(3, traces_, bgfx::Access::Read);
    bgfx::setImage(5, final_atlas_->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    set_cache_uniforms(bookkeeping::frame_start);
    bgfx::dispatch(pass.id, filter_program_->native_handle(), args_, 2, 1);
    filter_program_->end();
}

auto lumen_radiance_cache::update(const frame_inputs& inputs) -> bool
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
    trace_budget_ = lumen_pass::get_radiance_cache_trace_budget(inputs.params->settings.diffuse.update_speed,
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
    run_over_indirection(*update_program_, "GI/Cache Update");
    run_bookkeeping(bookkeeping::frame_start);
    run_over_indirection(*allocate_program_, "GI/Cache Allocate");
    run_bookkeeping(bookkeeping::budget);
    run_over_indirection(*select_program_, "GI/Cache Select");
    run_bookkeeping(bookkeeping::trace_list);
    run_trace(inputs);
    run_filter();
    persistent_ = true;
    return true;
}

void lumen_radiance_cache::bind_for_sampling(uint8_t indirection_stage, uint8_t final_stage) const
{
    bgfx::setBuffer(indirection_stage, indirection_[current_], bgfx::Access::Read);
    gfx::set_texture(uniforms_.s_lumen_rc_final, final_stage, final_atlas_);
    gfx::set_uniform(uniforms_.u_lumen_rc_clipmaps, clipmaps_.data(), uint16_t(clipmaps));
    set_layout_uniform();
}

} // namespace unravel
