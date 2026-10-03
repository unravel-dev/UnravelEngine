#include "lumen_gather_pass.h"

#include "lumen_pass_common.h"
#include "lumen_surface_cache_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <graphics/graphics.h>
#include <logging/logging.h>

#include <algorithm>

namespace unravel
{
using namespace gi::lumen;

namespace
{

using lumen_pass::bind_image;
using lumen_pass::divide_round_up;
using lumen_pass::ensure_texture;
using lumen_pass::group_edge;

/// Texel edge of a probe's octahedral tile, as an unsigned dispatch quantity.
constexpr uint32_t probe_trace_res = uint32_t(LUMEN_PROBE_TRACE_RES);
constexpr uint32_t probe_downsample = uint32_t(LUMEN_PROBE_DOWNSAMPLE_FACTOR);
/// The bordered probe radiance is read with hardware bilinear filtering.
constexpr uint64_t bilinear_texture_flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
/// Texels per side of a bordered probe (cs_lumen_probe_border.sc).
constexpr uint32_t probe_border_res = uint32_t(LUMEN_PROBE_TRACE_RES + 2 * LUMEN_PROBE_RADIANCE_BORDER);

/// Hammersley16(index, count, 0) of UE MonteCarlo.ush: (index / count, 16-bit radical inverse of index).
auto hammersley16(uint32_t index, uint32_t count) -> std::array<float, 2>
{
    uint32_t reversed = 0;
    for(uint32_t bit = 0; bit < 16u; ++bit)
    {
        reversed |= ((index >> bit) & 1u) << (15u - bit);
    }
    return {float(index) / float(count), float(reversed) / 65536.0f};
}

} // namespace

void lumen_gather_pass::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_frame, "u_lumen_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_probes, "u_lumen_probes", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_view, "u_lumen_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_trace, "u_lumen_trace", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_temporal, "u_lumen_temporal", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_prev_view_proj, "u_lumen_prev_view_proj", bgfx::UniformType::Mat4);
    cache_uniform(nullptr, u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
    cache_uniform(nullptr,
                  u_sdf_clipmap_levels,
                  "u_sdf_clipmap_levels",
                  bgfx::UniformType::Vec4,
                  global_sdf_clipmap::level_count);
    cache_uniform(nullptr, u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_hit_lighting, "u_lumen_hit_lighting", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_depth, "s_lumen_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_normal, "s_lumen_normal", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_probe_records, "s_lumen_probe_records", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_hiz, "s_lumen_hiz", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_prev_color, "s_lumen_prev_color", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_prev_depth, "s_lumen_prev_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_env_sh, "s_lumen_env_sh", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_trace_radiance, "s_lumen_trace_radiance", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_probe_radiance, "s_lumen_probe_radiance", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_probe_filtered, "s_lumen_probe_filtered", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_probe_sh, "s_lumen_probe_sh", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_history, "s_lumen_history", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_coverage, "s_sdf_clipmap_coverage", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_rc_final, "s_lumen_rc_final", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_rc_depth, "s_lumen_rc_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_lumen_options, "u_lumen_options", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_ray_gen, "u_lumen_ray_gen", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_prev_probe, "u_lumen_prev_probe", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_prev_inv_view_proj, "u_lumen_prev_inv_view_proj", bgfx::UniformType::Mat4);
    cache_uniform(nullptr, s_lumen_ray_info, "s_lumen_ray_info", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_screen_data, "s_lumen_screen_data", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_history_records, "s_lumen_history_records", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_history_radiance, "s_lumen_history_radiance", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_rough_history, "s_lumen_rough_history", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_probe_border, "s_lumen_probe_border", bgfx::UniformType::Sampler);
}

auto lumen_gather_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/lumen/") + name + ".sc");
        return std::make_shared<gpu_program>(shader);
    };
    place_program_ = load("cs_lumen_probe_place");
    generate_rays_program_ = load("cs_lumen_probe_generate_rays");
    trace_program_ = load("cs_lumen_probe_trace");
    composite_program_ = load("cs_lumen_probe_composite");
    filter_program_ = load("cs_lumen_probe_filter");
    sh_program_ = load("cs_lumen_probe_sh");
    border_program_ = load("cs_lumen_probe_border");
    integrate_program_ = load("cs_lumen_integrate");
    adaptive_probes_.init(ctx);
    radiance_cache_.init(ctx);
    short_range_ao_.init(ctx);
    if(!has_programs())
    {
        APPLOG_WARNING("[Lumen] Screen probe gather programs failed to load; the Lumen gather is "
                       "unavailable for this run.");
    }
    return has_programs();
}

auto lumen_gather_pass::has_programs() const -> bool
{
    const auto valid = [](const gpu_program::ptr& program)
    {
        return program && program->is_valid();
    };
    return valid(place_program_) && valid(generate_rays_program_) && valid(trace_program_) &&
           valid(composite_program_) && valid(filter_program_) && valid(sh_program_) && valid(border_program_) &&
           valid(integrate_program_) && adaptive_probes_.has_programs();
}

auto lumen_gather_pass::uses_short_range_ao(uint32_t experiments) -> bool
{
    return (experiments & experiment_screen_space_ao) == 0u;
}

auto lumen_gather_pass::has_short_range_ao() const -> bool
{
    return short_range_ao_.has_programs();
}

auto lumen_gather_pass::make_frame_layout(const usize32_t& view_size) -> frame_layout
{
    frame_layout layout;
    layout.view_size = view_size;
    layout.probes_x = divide_round_up(view_size.width, probe_downsample);
    layout.probes_y = divide_round_up(view_size.height, probe_downsample);
    layout.adaptive_capacity = lumen_adaptive_probes::get_capacity(layout.probes_x, layout.probes_y);
    // Rows for every adaptive probe the capacity allows (UE's trunc(rows x fraction) can fall one row short).
    layout.atlas_rows = layout.probes_y + divide_round_up(layout.adaptive_capacity, layout.probes_x);
    const uint32_t frame = gfx::get_render_frame();
    const uint32_t frame_mod = frame % uint32_t(LUMEN_PROBE_JITTER_PERIOD);
    // One screen-wide placement offset per frame, period 8 (UE LumenScreenProbeCommon.ush:67-76).
    const auto offset = hammersley16(frame_mod, uint32_t(LUMEN_PROBE_JITTER_PERIOD));
    const uint32_t prev_frame_mod = (frame + uint32_t(LUMEN_PROBE_JITTER_PERIOD) - 1u) % uint32_t(LUMEN_PROBE_JITTER_PERIOD);
    const auto prev_offset = hammersley16(prev_frame_mod, uint32_t(LUMEN_PROBE_JITTER_PERIOD));
    layout.prev_probe = {float(uint32_t(prev_offset[0] * float(probe_downsample))),
                         float(uint32_t(prev_offset[1] * float(probe_downsample))),
                         0.0f,
                         0.0f};
    layout.frame = {float(frame),
                    float(frame_mod),
                    float(uint32_t(offset[0] * float(probe_downsample))),
                    float(uint32_t(offset[1] * float(probe_downsample)))};
    layout.probes = {float(layout.probes_x), float(layout.probes_y), float(probe_downsample), float(layout.atlas_rows)};
    layout.view = {float(view_size.width),
                   float(view_size.height),
                   1.0f / float(view_size.width),
                   1.0f / float(view_size.height)};
    return layout;
}

auto lumen_gather_pass::acquire_probe_targets(gfx::render_view& rview, const frame_layout& layout) const -> probe_targets
{
    const usize32_t record_size{layout.probes_x, layout.atlas_rows};
    const usize32_t atlas_size{layout.probes_x * probe_trace_res, layout.atlas_rows * probe_trace_res};
    const usize32_t sh_size{layout.probes_x * uint32_t(LUMEN_SH_TEXELS_PER_PROBE), layout.atlas_rows};
    // The probe-side ping-pong: this frame writes one set, last frame's set is the sampler's history.
    auto& parity = rview.data_get_or_emplace("LUMEN_PROBE_PARITY", 0u);
    const std::string set = (parity & 1u) == 0u ? "_A" : "_B";
    const std::string prev_set = (parity & 1u) == 0u ? "_B" : "_A";
    ++parity;
    auto& written_frame = rview.data_get_or_emplace("LUMEN_PROBE_FRAME", 0u);
    const uint32_t render_frame = gfx::get_render_frame();
    const bool continuous = written_frame != 0u && render_frame == written_frame + 1u;
    written_frame = render_frame;
    probe_targets targets;
    targets.records = ensure_texture(rview, "LUMEN_PROBE_RECORDS" + set, record_size, bgfx::TextureFormat::RGBA32F);
    targets.trace_radiance = ensure_texture(rview, "LUMEN_TRACE_RADIANCE", atlas_size, bgfx::TextureFormat::RGBA16F);
    targets.probe_radiance = ensure_texture(rview, "LUMEN_PROBE_RADIANCE", atlas_size, bgfx::TextureFormat::RGBA16F);
    targets.filtered[0] = ensure_texture(rview, "LUMEN_PROBE_FILTERED_0" + set, atlas_size, bgfx::TextureFormat::RGBA16F);
    targets.filtered[1] = ensure_texture(rview, "LUMEN_PROBE_FILTERED_1" + set, atlas_size, bgfx::TextureFormat::RGBA16F);
    targets.sh = ensure_texture(rview, "LUMEN_PROBE_SH", sh_size, bgfx::TextureFormat::RGBA16F);
    targets.probe_border = ensure_texture(rview,
                                          "LUMEN_PROBE_BORDER",
                                          {layout.probes_x * probe_border_res, layout.atlas_rows * probe_border_res},
                                          bgfx::TextureFormat::RGBA16F,
                                          bilinear_texture_flags);
    targets.ray_info = ensure_texture(rview, "LUMEN_RAY_INFO", atlas_size, bgfx::TextureFormat::R32F);
    targets.screen_data = ensure_texture(rview, "LUMEN_SCREEN_DATA", record_size, bgfx::TextureFormat::RGBA16F);
    targets.history_records = rview.tex_safe_get("LUMEN_PROBE_RECORDS" + prev_set);
    targets.history_radiance =
        rview.tex_safe_get("LUMEN_PROBE_FILTERED_" + std::to_string(final_filter_index()) + prev_set);
    targets.has_probe_history = continuous && (experiments_ & experiment_no_history) == 0u && targets.history_records &&
                                targets.history_radiance &&
                                targets.history_records->get_size().width == record_size.width &&
                                targets.history_records->get_size().height == record_size.height;
    return targets;
}

auto lumen_gather_pass::final_filter_index() -> size_t
{
    return size_t(uint32_t(LUMEN_FILTER_PASSES - 1) & 1u);
}

auto lumen_gather_pass::acquire_history(gfx::render_view& rview,
                                        const gi_resolve_pass::run_params& params,
                                        const usize32_t& size) -> history_targets
{
    // Ping-pong per render view; the read half is history only when it was written on the frame
    // right before this one (the previous depth it is validated against is always that frame's).
    auto& parity = rview.data_get_or_emplace("LUMEN_HISTORY_PARITY", 0u);
    const bool even_frame = (parity & 1u) == 0u;
    ++parity;
    auto& written_frame = rview.data_get_or_emplace("LUMEN_HISTORY_FRAME", 0u);
    const uint32_t render_frame = gfx::get_render_frame();
    const bool continuous = written_frame != 0u && render_frame == written_frame + 1u;
    written_frame = render_frame;
    const char* write_name = even_frame ? "LUMEN_HISTORY_A" : "LUMEN_HISTORY_B";
    const char* read_name = even_frame ? "LUMEN_HISTORY_B" : "LUMEN_HISTORY_A";
    const char* rough_write_name = even_frame ? "LUMEN_ROUGH_HISTORY_A" : "LUMEN_ROUGH_HISTORY_B";
    const char* rough_read_name = even_frame ? "LUMEN_ROUGH_HISTORY_B" : "LUMEN_ROUGH_HISTORY_A";
    history_targets history;
    history.write = ensure_texture(rview, write_name, size, bgfx::TextureFormat::RGBA16F);
    history.read = rview.tex_safe_get(read_name);
    history.rough_write = ensure_texture(rview, rough_write_name, size, bgfx::TextureFormat::RGBA16F);
    history.rough_read = rview.tex_safe_get(rough_read_name);
    history.has_history = continuous && (experiments_ & experiment_no_history) == 0u && history.read &&
                          history.rough_read && params.prev_depth &&
                          params.settings.enable_temporal && history.read->get_size().width == size.width &&
                          history.read->get_size().height == size.height &&
                          history.rough_read->get_size().width == size.width &&
                          history.rough_read->get_size().height == size.height;
    if(history.has_history)
    {
        frames_without_history_ = 0;
    }
    else if(++frames_without_history_ == history_warning_frames)
    {
        APPLOG_WARNING("[Lumen] The gather has had no temporal history for {} frames (read target {}, "
                       "previous depth {}, continuity {}).",
                       history_warning_frames,
                       history.read ? "present" : "MISSING",
                       params.prev_depth ? "present" : "MISSING",
                       continuous ? "intact" : "BROKEN");
    }
    return history;
}

void lumen_gather_pass::set_layout_uniforms(const frame_layout& layout)
{
    gfx::set_uniform(uniforms_.u_lumen_frame, layout.frame.data());
    gfx::set_uniform(uniforms_.u_lumen_probes, layout.probes.data());
    gfx::set_uniform(uniforms_.u_lumen_view, layout.view.data());
    const float options[4] = {(experiments_ & experiment_uniform_rays) != 0u ? 0.0f : 1.0f,
                              (experiments_ & experiment_no_full_res_jitter) != 0u ? 1.0f : 0.0f,
                              (experiments_ & experiment_reject_near_screen_hits) != 0u ? 1.0f : 0.0f,
                              (experiments_ & experiment_voxel_screen_hits) != 0u ? 1.0f : 0.0f};
    gfx::set_uniform(uniforms_.u_lumen_options, options);
}

void lumen_gather_pass::bind_surface_cache(const gi_resolve_pass::run_params& params) const
{
    if(params.lumen_surface_cache != nullptr)
    {
        params.lumen_surface_cache->bind_for_sampling(13, 14, 15, true);
        return;
    }
    const math::vec4 no_cards(0.0f);
    gfx::set_uniform(uniforms_.u_lumen_hit_lighting, no_cards);
}

void lumen_gather_pass::bind_radiance_cache(bool radiance_cache_ready) const
{
    if(radiance_cache_ready)
    {
        radiance_cache_.bind_for_sampling(7, 8, 9);
        return;
    }
    // Never read without the cache (the readers' cache flag); bound for OpenGL's benefit.
    const auto& black = default_textures::get().black_texture();
    gfx::set_texture(uniforms_.s_lumen_rc_final, 8, black);
    gfx::set_texture(uniforms_.s_lumen_rc_depth, 9, black);
}

void lumen_gather_pass::run_generate_rays(const gi_resolve_pass::run_params& params,
                                          const frame_layout& layout,
                                          const probe_targets& targets,
                                          const history_targets& history,
                                          bool radiance_cache_ready)
{
    const auto& black = default_textures::get().black_texture();
    gfx::render_pass pass("GI/Lumen Probe Generate Rays");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    generate_rays_program_->begin();
    bind_image(0, targets.ray_info, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    bind_image(1, targets.screen_data, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_depth, 2, params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 3, params.g_buffer->get_texture(1));
    gfx::set_texture(uniforms_.s_lumen_probe_records, 4, targets.records);
    gfx::set_texture(uniforms_.s_lumen_history_records, 5, targets.has_probe_history ? targets.history_records : black);
    gfx::set_texture(uniforms_.s_lumen_history_radiance, 6, targets.has_probe_history ? targets.history_radiance : black);
    bind_radiance_cache(radiance_cache_ready);
    gfx::set_texture(uniforms_.s_lumen_history, 10, history.has_history ? history.read : black);
    gfx::set_texture(uniforms_.s_lumen_prev_depth, 11, params.prev_depth ? params.prev_depth : black);
    set_layout_uniforms(layout);
    const float ray_gen[4] = {targets.has_probe_history ? 1.0f : 0.0f,
                              history.has_history ? 1.0f : 0.0f,
                              radiance_cache_ready ? 1.0f : 0.0f,
                              0.0f};
    gfx::set_uniform(uniforms_.u_lumen_ray_gen, ray_gen);
    gfx::set_uniform(uniforms_.u_lumen_prev_probe, layout.prev_probe.data());
    const auto prev_view_proj = params.cam->get_prev_view_projection_unjittered().get_matrix();
    gfx::set_uniform(uniforms_.u_lumen_prev_view_proj, prev_view_proj);
    gfx::set_uniform(uniforms_.u_lumen_prev_inv_view_proj, glm::inverse(prev_view_proj));
    gfx::set_uniform(uniforms_.u_pre_exposure, params.pre_exposure.to_uniform().data());
    adaptive_probes_.dispatch(pass.id, *generate_rays_program_, lumen_adaptive_probes::args_group_per_probe);
    generate_rays_program_->end();
}

void lumen_gather_pass::run_place(const gi_resolve_pass::run_params& params,
                                  const frame_layout& layout,
                                  const probe_targets& targets)
{
    gfx::render_pass pass("GI/Lumen Probe Place");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    place_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_depth, 0, params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 1, params.g_buffer->get_texture(1));
    bind_image(2, targets.records, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    adaptive_probes_.bind_state(3, bgfx::Access::ReadWrite);
    set_layout_uniforms(layout);
    bgfx::dispatch(pass.id,
                   place_program_->native_handle(),
                   divide_round_up(layout.probes_x, group_edge),
                   divide_round_up(layout.atlas_rows, group_edge),
                   1);
    place_program_->end();
}

void lumen_gather_pass::run_adaptive_probes(const gi_resolve_pass::run_params& params,
                                            const frame_layout& layout,
                                            const probe_targets& targets)
{
    lumen_adaptive_probes::frame_inputs inputs;
    inputs.params = &params;
    inputs.probe_records = targets.records;
    inputs.probes_x = layout.probes_x;
    inputs.probes_y = layout.probes_y;
    inputs.capacity = layout.adaptive_capacity;
    inputs.frame = layout.frame.data();
    inputs.probes = layout.probes.data();
    inputs.view = layout.view.data();
    inputs.place = (experiments_ & experiment_no_adaptive_probes) == 0u;
    adaptive_probes_.run(inputs);
}

void lumen_gather_pass::run_trace(const gi_resolve_pass::run_params& params,
                                  const frame_layout& layout,
                                  const probe_targets& targets,
                                  bool radiance_cache_ready)
{
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    const auto& black = default_textures::get().black_texture();
    const bool has_hiz = params.hiz && params.hiz->is_valid();
    const bool has_prev_color = params.prev_color && params.prev_color->is_valid();
    // Lumen always traces the screen first (r.Lumen.ScreenProbeGather.ScreenTraces 1): the old gather's
    // enable_screen_trace switch does not apply here.
    const bool screen_traces = has_hiz && has_prev_color && params.prev_depth &&
                               (experiments_ & experiment_no_screen_traces) == 0u;
    gfx::render_pass pass("GI/Lumen Probe Trace");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    trace_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_probe_records, 0, targets.records);
    gfx::set_texture(uniforms_.s_lumen_hiz, 1, has_hiz ? params.hiz : params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_prev_color, 2, has_prev_color ? params.prev_color : black);
    gfx::set_texture(uniforms_.s_lumen_env_sh, 3, params.irradiance_sh ? params.irradiance_sh : black);
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    bind_image(5, targets.trace_radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_prev_depth, 6, params.prev_depth ? params.prev_depth : black);
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, lumen_pass::get_sdf_coverage(clipmap_gpu, experiments_));
    bind_radiance_cache(radiance_cache_ready);
    gfx::set_texture(uniforms_.s_lumen_ray_info, 11, targets.ray_info);
    gfx::set_texture(uniforms_.s_lumen_normal, 12, params.g_buffer->get_texture(1));
    bind_surface_cache(params);
    set_layout_uniforms(layout);
    const float trace[4] = {has_hiz ? float(params.hiz->info.numMips) : 1.0f,
                            screen_traces ? 1.0f : 0.0f,
                            radiance_cache_ready ? 1.0f : 0.0f,
                            float(((experiments_ & experiment_skip_near_field) != 0u ? 2u : 0u) |
                                  ((experiments_ & experiment_show_sdf_bias) != 0u ? 4u : 0u) |
                                  ((experiments_ & experiment_show_ray_sources) != 0u ? 8u : 0u) |
                                  ((experiments_ & experiment_keep_stage_low) != 0u ? 16u : 0u) |
                                  ((experiments_ & experiment_keep_stage_high) != 0u ? 32u : 0u) |
                                  ((experiments_ & experiment_rejected_hits_vouch_nothing) != 0u ? 64u : 0u) |
                                  ((experiments_ & experiment_show_sdf_start) != 0u ? 128u : 0u))};
    gfx::set_uniform(uniforms_.u_lumen_trace, trace);
    gfx::set_uniform(uniforms_.u_lumen_prev_view_proj, params.cam->get_prev_view_projection_unjittered().get_matrix());
    gfx::set_uniform(uniforms_.u_pre_exposure, params.pre_exposure.to_uniform().data());
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    adaptive_probes_.dispatch(pass.id, *trace_program_, lumen_adaptive_probes::args_group_per_probe);
    trace_program_->end();
}

void lumen_gather_pass::run_composite(const frame_layout& layout, const probe_targets& targets)
{
    gfx::render_pass pass("GI/Lumen Probe Composite");
    composite_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_trace_radiance, 0, targets.trace_radiance);
    gfx::set_texture(uniforms_.s_lumen_probe_records, 1, targets.records);
    bind_image(2, targets.probe_radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_ray_info, 3, targets.ray_info);
    set_layout_uniforms(layout);
    adaptive_probes_.dispatch(pass.id, *composite_program_, lumen_adaptive_probes::args_group_per_probe);
    composite_program_->end();
}

auto lumen_gather_pass::run_filter(const gi_resolve_pass::run_params& params,
                                   const frame_layout& layout,
                                   const probe_targets& targets) -> gfx::texture::ptr
{
    auto source = targets.probe_radiance;
    for(int filter_pass = 0; filter_pass < int(LUMEN_FILTER_PASSES); ++filter_pass)
    {
        const auto& target = targets.filtered[size_t(filter_pass & 1)];
        gfx::render_pass pass("GI/Lumen Probe Filter");
        pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
        filter_program_->begin();
        gfx::set_texture(uniforms_.s_lumen_probe_radiance, 0, source);
        gfx::set_texture(uniforms_.s_lumen_probe_records, 1, targets.records);
        bind_image(2, target, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        gfx::set_texture(uniforms_.s_lumen_screen_data, 3, targets.screen_data);
        set_layout_uniforms(layout);
        adaptive_probes_.dispatch(pass.id, *filter_program_, lumen_adaptive_probes::args_group_per_probe);
        filter_program_->end();
        source = target;
    }
    return source;
}

void lumen_gather_pass::run_sh(const frame_layout& layout, const probe_targets& targets, const gfx::texture::ptr& filtered)
{
    gfx::render_pass pass("GI/Lumen Probe SH");
    sh_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_probe_filtered, 0, filtered);
    gfx::set_texture(uniforms_.s_lumen_probe_records, 1, targets.records);
    bind_image(2, targets.sh, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    set_layout_uniforms(layout);
    adaptive_probes_.dispatch(pass.id, *sh_program_, lumen_adaptive_probes::args_group_per_probe);
    sh_program_->end();
}

void lumen_gather_pass::run_border(const frame_layout& layout,
                                   const probe_targets& targets,
                                   const gfx::texture::ptr& filtered)
{
    gfx::render_pass pass("GI/Lumen Probe Border");
    border_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_probe_filtered, 0, filtered);
    gfx::set_texture(uniforms_.s_lumen_probe_records, 1, targets.records);
    bind_image(2, targets.probe_border, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    set_layout_uniforms(layout);
    adaptive_probes_.dispatch(pass.id, *border_program_, lumen_adaptive_probes::args_border_threads);
    border_program_->end();
}

void lumen_gather_pass::run_integrate(const gi_resolve_pass::run_params& params,
                                      const frame_layout& layout,
                                      const probe_targets& targets,
                                      const history_targets& history,
                                      const gfx::texture::ptr& resolve,
                                      const gfx::texture::ptr& rough_specular)
{
    const auto& black = default_textures::get().black_texture();
    gfx::render_pass pass("GI/Lumen Integrate+Temporal");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    integrate_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_depth, 0, params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(uniforms_.s_lumen_probe_records, 2, targets.records);
    gfx::set_texture(uniforms_.s_lumen_probe_sh, 3, targets.sh);
    bind_image(4, history.rough_write, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(5, rough_specular, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(6, history.write, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(7, resolve, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_history, 8, history.has_history ? history.read : black);
    gfx::set_texture(uniforms_.s_lumen_prev_depth, 9, params.prev_depth ? params.prev_depth : black);
    gfx::set_texture(uniforms_.s_lumen_rough_history, 10, history.has_history ? history.rough_read : black);
    gfx::set_texture(uniforms_.s_lumen_probe_border, 11, targets.probe_border);
    adaptive_probes_.bind_state(12, bgfx::Access::Read);
    set_layout_uniforms(layout);
    const bool rough_specular_enabled = (experiments_ & experiment_no_rough_specular) == 0u;
    const float temporal[4] = {history.has_history ? 1.0f : 0.0f,
                               std::max(params.settings.intensity, 0.0f),
                               rough_specular_enabled ? 1.0f : 0.0f,
                               (experiments_ & experiment_show_interpolation_fallback) != 0u ? 1.0f : 0.0f};
    gfx::set_uniform(uniforms_.u_lumen_temporal, temporal);
    gfx::set_uniform(uniforms_.u_lumen_prev_view_proj, params.cam->get_prev_view_projection_unjittered().get_matrix());
    gfx::set_uniform(uniforms_.u_pre_exposure, params.pre_exposure.to_uniform().data());
    bgfx::dispatch(pass.id,
                   integrate_program_->native_handle(),
                   divide_round_up(layout.view_size.width, group_edge),
                   divide_round_up(layout.view_size.height, group_edge),
                   1);
    integrate_program_->end();
}

void lumen_gather_pass::run_short_range_ao(gfx::render_view& rview,
                                           const gi_resolve_pass::run_params& params,
                                           const frame_layout& layout,
                                           const history_targets& history)
{
    lumen_short_range_ao_pass::run_params ao_params;
    ao_params.gather = &params;
    ao_params.gather_history = history.read;
    ao_params.has_gather_history = history.has_history;
    ao_params.frame = layout.frame.data();
    ao_params.probes = layout.probes.data();
    ao_params.view = layout.view.data();
    ao_params.r2_noise = (experiments_ & experiment_short_range_ao_hash_noise) == 0u;
    auto screen_ao = short_range_ao_.run(rview, ao_params);
    if(!screen_ao)
    {
        return;
    }
    rview.tex_get_or_emplace(screen_ao_texture) = screen_ao;
    rview.data_get_or_emplace(screen_ao_frame, 0u) = uint32_t(gfx::get_render_frame());
}

auto lumen_gather_pass::run(gfx::render_view& rview, const gi_resolve_pass::run_params& params) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/GI/Lumen Gather");
    rview.tex_remove("GI_ROUGH_SPECULAR");
    if(!has_programs() || !params.g_buffer || !params.cam || !params.view_cache || !adaptive_probes_.ensure_resources())
    {
        return {};
    }
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    if(!clipmap_gpu.is_valid())
    {
        return {};
    }
    experiments_ = params.surface_cache ? params.surface_cache->get_experiment_flags() : 0u;
    const auto layout = make_frame_layout(params.g_buffer->get_size());
    // The expanded bilinear interpolation reads a 2x2 probe neighbourhood; probe records pack their pixel below
    // LUMEN_PROBE_PIXEL_STRIDE per axis.
    const auto max_view_extent = uint32_t(LUMEN_PROBE_PIXEL_STRIDE);
    if(layout.probes_x < 2u || layout.probes_y < 2u || layout.view_size.width > max_view_extent ||
       layout.view_size.height > max_view_extent)
    {
        return {};
    }
    const auto targets = acquire_probe_targets(rview, layout);
    const auto history = acquire_history(rview, params, layout.view_size);
    const auto resolve = ensure_texture(rview, "LUMEN_RESOLVE", layout.view_size, bgfx::TextureFormat::RGBA16F);
    const auto rough_specular =
        ensure_texture(rview, "LUMEN_ROUGH_SPECULAR", layout.view_size, bgfx::TextureFormat::RGBA16F);
    run_place(params, layout, targets);
    run_adaptive_probes(params, layout, targets);
    lumen_radiance_cache::frame_inputs cache_inputs;
    cache_inputs.params = &params;
    cache_inputs.probe_records = targets.records;
    cache_inputs.probes_x = layout.probes_x;
    cache_inputs.probe_rows = layout.atlas_rows;
    cache_inputs.frame = layout.frame.data();
    cache_inputs.probes = layout.probes.data();
    cache_inputs.view = layout.view.data();
    const bool radiance_cache_ready =
        (experiments_ & experiment_no_radiance_cache) == 0u && radiance_cache_.update(cache_inputs);
    if((experiments_ & experiment_uniform_rays) == 0u)
    {
        run_generate_rays(params, layout, targets, history, radiance_cache_ready);
    }
    run_trace(params, layout, targets, radiance_cache_ready);
    run_composite(layout, targets);
    const auto filtered = (experiments_ & experiment_no_spatial_filter) != 0u ? targets.probe_radiance
                                                                              : run_filter(params, layout, targets);
    run_sh(layout, targets, filtered);
    run_border(layout, targets, filtered);
    run_integrate(params, layout, targets, history, resolve, rough_specular);
    if(uses_short_range_ao(experiments_))
    {
        run_short_range_ao(rview, params, layout, history);
    }
    // The rough tier of the reflections reads the gather's rough specular (a = frames + 1).
    rview.tex_get_or_emplace("GI_ROUGH_SPECULAR") = rough_specular;
    bgfx::discard();
    return resolve;
}

} // namespace unravel
