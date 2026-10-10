#include "lumen_gather_pass.h"

#include "lumen_pass_common.h"
#include "lumen_surface_cache_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
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

/// The tracing resolutions of the program sets, in order, and the suffixes of their programs' files.
constexpr std::array<uint32_t, 3> probe_trace_resolutions = {4u, 8u, 16u};
constexpr std::array<const char*, 3> probe_program_suffixes = {"_res4", "", "_res16"};
/// The range of the screen probe spacing in pixels.
constexpr uint32_t min_probe_downsample = 4;
constexpr uint32_t max_probe_downsample = 64;
/// The bordered probe radiance is read with hardware bilinear filtering.
constexpr uint64_t bilinear_texture_flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
/// The rough specular history (cs_lumen_integrate.sc s_lumen_rough_history_out, rg11b10f).
constexpr bgfx::TextureFormat::Enum rough_history_format = bgfx::TextureFormat::RG11B10F;
/// The fixed jitter index while the traces are visualized: the placement and the rays hold still, and so do the lines.
constexpr uint32_t visualize_traces_jitter_index = 6;
/// The trace programs' output stage: the trace radiance, or the visualize variant's rays.
constexpr uint8_t trace_output_stage = 5;
/// The stage of the rays the screen pass leaves to the far-field pass, in each (cs_lumen_probe_trace.sc).
constexpr uint8_t trace_rays_screen_stage = 3;
constexpr uint8_t trace_rays_far_field_stage = 1;
constexpr uint8_t trace_hits_stage = 2;
constexpr uint8_t trace_args_count_stage = 0;
constexpr uint8_t trace_args_output_stage = 1;
/// The uints per ray of b_lumen_trace_rays (cs_lumen_probe_trace.sc LUMEN_TRACE_RAY_STRIDE).
constexpr uint32_t trace_ray_stride = 4;

/// Hammersley16(index, count, 0) of sampling.sh: (index / count, 16-bit radical inverse of index).
auto hammersley16(uint32_t index, uint32_t count) -> std::array<float, 2>
{
    uint32_t reversed = 0;
    for(uint32_t bit = 0; bit < 16u; ++bit)
    {
        reversed |= ((index >> bit) & 1u) << (15u - bit);
    }
    return {float(index) / float(count), float(reversed) / 65536.0f};
}

/// Texels per axis of a probe's bordered radiance (cs_lumen_probe_border.sc).
auto get_probe_border_resolution(uint32_t trace_resolution) -> uint32_t
{
    return trace_resolution + 2u * uint32_t(LUMEN_PROBE_RADIANCE_BORDER);
}

/// Clamps the probe spacing: @p downsample grows to the power of two (4 at least) at
/// which the bordered probe atlas of a @p view_size view, adaptive rows included, fits the largest texture.
auto clamp_probe_downsample(uint32_t downsample, const usize32_t& view_size, uint32_t trace_resolution) -> uint32_t
{
    const uint32_t max_texture_size = bgfx::getCaps()->limits.maxTextureSize;
    const uint32_t probe_resolution = get_probe_border_resolution(trace_resolution);
    const auto atlas_height =
        view_size.height + uint32_t(float(view_size.height) * float(LUMEN_ADAPTIVE_ALLOCATION_FRACTION));
    const uint32_t min_x = divide_round_up(view_size.width * probe_resolution, max_texture_size);
    const uint32_t min_y = divide_round_up(atlas_height * probe_resolution, max_texture_size);
    const uint32_t min_downsample = std::max(min_probe_downsample, std::bit_ceil(std::max(min_x, min_y)));
    return std::clamp(downsample, min_downsample, max_probe_downsample);
}

} // namespace

void lumen_gather_pass::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_frame, "u_lumen_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_probes, "u_lumen_probes", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_view, "u_lumen_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_trace, "u_lumen_trace", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_temporal, "u_lumen_temporal", bgfx::UniformType::Vec4);
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
    cache_uniform(nullptr, s_sdf_clipmap_mip, "s_sdf_clipmap_mip", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_rc_final, "s_lumen_rc_final", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_lumen_options, "u_lumen_options", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_settings, "u_lumen_settings", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_ray_gen, "u_lumen_ray_gen", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_prev_probe, "u_lumen_prev_probe", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_ray_info, "s_lumen_ray_info", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_screen_data, "s_lumen_screen_data", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_history_records, "s_lumen_history_records", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_history_radiance, "s_lumen_history_radiance", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_rough_history, "s_lumen_rough_history", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_probe_border, "s_lumen_probe_border", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_lumen_visualize_traces, "u_lumen_visualize_traces", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_probe_moving, "s_lumen_probe_moving", bgfx::UniformType::Sampler);
    motion.cache_uniforms();
}

lumen_gather_pass::~lumen_gather_pass()
{
    lumen_pass::destroy_handle(visualized_traces_);
    lumen_pass::destroy_handle(trace_rays_);
    lumen_pass::destroy_handle(trace_hits_);
    lumen_pass::destroy_handle(far_field_args_);
    lumen_pass::destroy_handle(hit_args_);
}

auto lumen_gather_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time. The short-range AO's
    // uniforms too (the integrate accumulates the AO), and the radiance cache's (the probe trace samples it).
    uniforms_.cache_uniforms();
    short_range_ao_.init(ctx);
    radiance_cache_.init(ctx);
    const auto load = [&](const std::string& name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/" + name + ".sc");
        return std::make_shared<gpu_program>(shader);
    };
    place_program_ = load("cs_lumen_probe_place");
    far_field_args_program_ = load("cs_lumen_trace_far_field_args");
    for(size_t i = 0; i < probe_programs_.size(); ++i)
    {
        const std::string suffix = probe_program_suffixes[i];
        auto& programs = probe_programs_[i];
        programs.generate_rays = load("cs_lumen_probe_generate_rays" + suffix);
        programs.trace = load("cs_lumen_probe_trace" + suffix);
        programs.trace_far_field = load("cs_lumen_probe_trace_far_field" + suffix);
        programs.trace_hit_shade = load("cs_lumen_probe_trace_hit_shade" + suffix);
        programs.composite = load("cs_lumen_probe_composite" + suffix);
        programs.filter = load("cs_lumen_probe_filter" + suffix);
        programs.sh = load("cs_lumen_probe_sh" + suffix);
        programs.border = load("cs_lumen_probe_border" + suffix);
        programs.integrate = load("cs_lumen_integrate" + suffix);
        programs.integrate_short_range_ao = load("cs_lumen_integrate_ao" + suffix);
        programs.trace_visualize = load("cs_lumen_probe_trace_visualize" + suffix);
    }
    adaptive_probes_.init(ctx);
    ray_count_clear_.init(ctx);
    if(!has_programs())
    {
        APPLOG_WARNING("[GI] Screen probe gather programs failed to load; the screen probe gather is "
                       "unavailable for this run.");
    }
    return has_programs();
}

auto lumen_gather_pass::probe_programs::is_valid() const -> bool
{
    const auto valid = [](const gpu_program::ptr& program)
    {
        return program && program->is_valid();
    };
    return valid(generate_rays) && valid(trace) && valid(trace_far_field) && valid(trace_hit_shade) &&
           valid(composite) && valid(filter) &&
           valid(sh) && valid(border) && valid(integrate) && valid(integrate_short_range_ao);
}

auto lumen_gather_pass::has_programs() const -> bool
{
    const bool has_probe_programs = std::all_of(probe_programs_.begin(),
                                                probe_programs_.end(),
                                                [](const probe_programs& programs)
                                                {
                                                    return programs.is_valid();
                                                });
    return place_program_ && place_program_->is_valid() && far_field_args_program_ &&
           far_field_args_program_->is_valid() && has_probe_programs && adaptive_probes_.has_programs() &&
           ray_count_clear_.is_ready();
}

auto lumen_gather_pass::get_probe_programs(uint32_t trace_resolution) const -> const probe_programs&
{
    // lumen_pass::get_probe_trace_resolution returns one of the compiled resolutions; any other takes the default's.
    auto found = std::find(probe_trace_resolutions.begin(), probe_trace_resolutions.end(), trace_resolution);
    if(found == probe_trace_resolutions.end())
    {
        found = std::find(probe_trace_resolutions.begin(),
                          probe_trace_resolutions.end(),
                          uint32_t(LUMEN_PROBE_TRACE_RES));
    }
    return probe_programs_[size_t(found - probe_trace_resolutions.begin())];
}

auto lumen_gather_pass::uses_short_range_ao(const gi_settings::ambient_occlusion_settings& settings) -> bool
{
    return settings.enabled && settings.intensity > 0.0f;
}

auto lumen_gather_pass::has_short_range_ao() const -> bool
{
    return short_range_ao_.has_programs();
}

auto lumen_gather_pass::make_frame_layout(const usize32_t& view_size,
                                          float quality,
                                          gi_project_settings::quality_level tier,
                                          bool is_jitter_fixed) -> frame_layout
{
    frame_layout layout;
    layout.view_size = view_size;
    layout.trace_resolution = lumen_pass::get_probe_trace_resolution(quality);
    layout.downsample = clamp_probe_downsample(lumen_pass::get_probe_downsample_factor(quality, tier),
                                               view_size,
                                               layout.trace_resolution);
    const uint32_t probe_downsample = layout.downsample;
    layout.probes_x = divide_round_up(view_size.width, probe_downsample);
    layout.probes_y = divide_round_up(view_size.height, probe_downsample);
    layout.adaptive_capacity = lumen_adaptive_probes::get_capacity(layout.probes_x, layout.probes_y);
    // Rows for every adaptive probe the capacity allows (rounded up: trunc(rows x fraction) can fall one row short).
    layout.atlas_rows = layout.probes_y + divide_round_up(layout.adaptive_capacity, layout.probes_x);
    const uint32_t frame = gfx::get_render_frame();
    const uint32_t view_frame_mod = frame % uint32_t(LUMEN_PROBE_JITTER_PERIOD);
    const uint32_t frame_mod =
        is_jitter_fixed ? visualize_traces_jitter_index % uint32_t(LUMEN_PROBE_JITTER_PERIOD) : view_frame_mod;
    // One screen-wide placement offset per frame, period 8.
    const auto offset = hammersley16(frame_mod, uint32_t(LUMEN_PROBE_JITTER_PERIOD));
    const uint32_t prev_frame_mod =
        is_jitter_fixed ? frame_mod
                        : (frame + uint32_t(LUMEN_PROBE_JITTER_PERIOD) - 1u) % uint32_t(LUMEN_PROBE_JITTER_PERIOD);
    const auto prev_offset = hammersley16(prev_frame_mod, uint32_t(LUMEN_PROBE_JITTER_PERIOD));
    layout.prev_probe = {float(uint32_t(prev_offset[0] * float(probe_downsample))),
                         float(uint32_t(prev_offset[1] * float(probe_downsample))),
                         0.0f,
                         0.0f};
    layout.frame = {lumen_pass::get_frame_index(frame),
                    float(frame_mod),
                    float(uint32_t(offset[0] * float(probe_downsample))),
                    float(uint32_t(offset[1] * float(probe_downsample)))};
    const auto view_offset = hammersley16(view_frame_mod, uint32_t(LUMEN_PROBE_JITTER_PERIOD));
    layout.view_frame = {lumen_pass::get_frame_index(frame),
                         float(view_frame_mod),
                         float(uint32_t(view_offset[0] * float(probe_downsample))),
                         float(uint32_t(view_offset[1] * float(probe_downsample)))};
    layout.probes = {float(layout.probes_x), float(layout.probes_y), float(probe_downsample), float(layout.atlas_rows)};
    layout.view = {float(view_size.width),
                   float(view_size.height),
                   1.0f / float(view_size.width),
                   1.0f / float(view_size.height)};
    layout.is_interpolation_stochastic = lumen_pass::has_stochastic_probe_interpolation(tier);
    return layout;
}

auto lumen_gather_pass::acquire_probe_targets(gfx::render_view& rview, const frame_layout& layout) const -> probe_targets
{
    const usize32_t record_size{layout.probes_x, layout.atlas_rows};
    const usize32_t atlas_size{layout.probes_x * layout.trace_resolution, layout.atlas_rows * layout.trace_resolution};
    const usize32_t sh_size{layout.probes_x * uint32_t(LUMEN_SH_TEXELS_PER_PROBE), layout.atlas_rows};
    const uint32_t border_resolution = get_probe_border_resolution(layout.trace_resolution);
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
    targets.filtered = ensure_texture(rview, "LUMEN_PROBE_FILTERED" + set, atlas_size, bgfx::TextureFormat::RGBA16F);
    targets.sh = ensure_texture(rview, "LUMEN_PROBE_SH", sh_size, bgfx::TextureFormat::RGBA16F);
    targets.probe_border = ensure_texture(rview,
                                          "LUMEN_PROBE_BORDER",
                                          {layout.probes_x * border_resolution, layout.atlas_rows * border_resolution},
                                          bgfx::TextureFormat::RGBA16F,
                                          bilinear_texture_flags);
    targets.ray_info = ensure_texture(rview, "LUMEN_RAY_INFO", atlas_size, bgfx::TextureFormat::R32F);
    targets.screen_data = ensure_texture(rview, "LUMEN_SCREEN_DATA", record_size, bgfx::TextureFormat::RGBA16F);
    targets.probe_moving = ensure_texture(rview, "LUMEN_PROBE_MOVING", record_size, bgfx::TextureFormat::R8);
    targets.history_records = rview.tex_safe_get("LUMEN_PROBE_RECORDS" + prev_set);
    targets.history_radiance = rview.tex_safe_get("LUMEN_PROBE_FILTERED" + prev_set);
    // Last frame's probes are history only in this frame's layout: the same probe count and tracing resolution.
    targets.has_probe_history = continuous && (experiments_ & experiment_no_history) == 0u && !starts_history_over_ &&
                                lumen_pass::has_view_size(targets.history_records, record_size) &&
                                lumen_pass::has_view_size(targets.history_radiance, atlas_size);
    return targets;
}

auto lumen_gather_pass::get_current_history(gfx::render_view& rview) -> gfx::texture::ptr
{
    const uint32_t parity = rview.data_get("LUMEN_HISTORY_PARITY");
    if(parity == 0u || rview.data_get("LUMEN_HISTORY_FRAME") != gfx::get_render_frame())
    {
        return nullptr;
    }
    // acquire_history advanced the parity past the half it wrote.
    const bool wrote_even = ((parity - 1u) & 1u) == 0u;
    return rview.tex_safe_get(wrote_even ? "LUMEN_HISTORY_A" : "LUMEN_HISTORY_B");
}

auto lumen_gather_pass::acquire_history(gfx::render_view& rview,
                                        const lumen_run_params& params,
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
    // R11G11B10: no reader takes the rough specular's alpha.
    history.rough_write = ensure_texture(rview, rough_write_name, size, rough_history_format);
    history.rough_read = rview.tex_safe_get(rough_read_name);
    history.has_history = continuous && (experiments_ & experiment_no_history) == 0u && !starts_history_over_ && history.read &&
                          history.rough_read && params.prev_depth && history.read->get_size().width == size.width &&
                          history.read->get_size().height == size.height &&
                          history.rough_read->get_size().width == size.width &&
                          history.rough_read->get_size().height == size.height;
    if(history.has_history)
    {
        frames_without_history_ = 0;
    }
    else if(++frames_without_history_ == history_warning_frames)
    {
        APPLOG_WARNING("[GI] The gather has had no temporal history for {} frames (read target {}, "
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
                              float(((experiments_ & experiment_voxel_screen_hits) != 0u ? 1u : 0u) |
                                    ((experiments_ & experiment_jitter_slice_dither) != 0u ? 2u : 0u))};
    gfx::set_uniform(uniforms_.u_lumen_options, options);
    gfx::set_uniform(uniforms_.u_lumen_settings, settings_uniform_.data());
}

void lumen_gather_pass::bind_surface_cache(const lumen_run_params& params) const
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
        radiance_cache_.bind_for_sampling(7, 8);
        return;
    }
    // Never read without the cache (the readers' cache flag); bound for OpenGL's benefit.
    const auto& black = default_textures::get().black_texture();
    gfx::set_texture(uniforms_.s_lumen_rc_final, 8, black);
}

void lumen_gather_pass::bind_motion(const lumen_run_params& params, uint8_t velocity_stage) const
{
    uniforms_.motion.bind(velocity_stage,
                          params.velocity,
                          params.cam->get_prev_view_projection_unjittered().get_matrix(),
                          experiments_);
}

void lumen_gather_pass::run_generate_rays(const lumen_run_params& params,
                                          const frame_layout& layout,
                                          const probe_targets& targets,
                                          const history_targets& history,
                                          bool radiance_cache_ready)
{
    const auto& black = default_textures::get().black_texture();
    gfx::render_pass pass("GI/Probe Generate Rays");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    programs_->generate_rays->begin();
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
    bind_motion(params, 9);
    gfx::set_uniform(uniforms_.u_pre_exposure, params.pre_exposure.to_uniform().data());
    adaptive_probes_.dispatch(pass.id, *programs_->generate_rays, lumen_adaptive_probes::args_group_per_probe);
    programs_->generate_rays->end();
}

void lumen_gather_pass::run_place(const lumen_run_params& params,
                                  const frame_layout& layout,
                                  const probe_targets& targets)
{
    gfx::render_pass pass("GI/Probe Place");
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

void lumen_gather_pass::run_adaptive_probes(const lumen_run_params& params,
                                            const frame_layout& layout,
                                            const probe_targets& targets)
{
    lumen_adaptive_probes::frame_inputs inputs;
    inputs.params = &params;
    inputs.probe_records = targets.records;
    inputs.probes_x = layout.probes_x;
    inputs.probes_y = layout.probes_y;
    inputs.capacity = layout.adaptive_capacity;
    inputs.border_resolution = get_probe_border_resolution(layout.trace_resolution);
    inputs.frame = layout.frame.data();
    inputs.probes = layout.probes.data();
    inputs.view = layout.view.data();
    inputs.place = (experiments_ & experiment_no_adaptive_probes) == 0u;
    const bool is_epic_layout = (experiments_ & lumen_pass::experiment_epic_adaptive_probes) != 0u;
    inputs.layout =
        lumen_pass::get_adaptive_probe_layout(is_epic_layout ? lumen_pass::quality_level::epic : params.gi_quality);
    adaptive_probes_.run(inputs);
}

void lumen_gather_pass::ensure_trace_rays(uint32_t rays)
{
    const uint32_t count = 1u + trace_ray_stride * rays;
    if(bgfx::isValid(trace_rays_) && trace_rays_capacity_ >= count)
    {
        return;
    }
    lumen_pass::destroy_handle(trace_rays_);
    lumen_pass::destroy_handle(trace_hits_);
    trace_rays_ = lumen_pass::make_uint_buffer(count);
    trace_hits_ = lumen_pass::make_uint_buffer(1u + rays);
    trace_rays_capacity_ = count;
}

void lumen_gather_pass::dispatch_trace_args(uint16_t view_id,
                                            bgfx::DynamicIndexBufferHandle rays,
                                            bgfx::IndirectBufferHandle args) const
{
    far_field_args_program_->begin();
    bgfx::setBuffer(trace_args_count_stage, rays, bgfx::Access::Read);
    bgfx::setBuffer(trace_args_output_stage, args, bgfx::Access::ReadWrite);
    bgfx::dispatch(view_id, far_field_args_program_->native_handle(), 1, 1, 1);
    far_field_args_program_->end();
}

void lumen_gather_pass::run_trace(const lumen_run_params& params,
                                  const frame_layout& layout,
                                  const probe_targets& targets,
                                  bool radiance_cache_ready)
{
    const uint32_t rays_per_probe = layout.trace_resolution * layout.trace_resolution;
    ensure_trace_rays(layout.probes_x * layout.atlas_rows * rays_per_probe);
    if(!bgfx::isValid(far_field_args_))
    {
        far_field_args_ = bgfx::createIndirectBuffer(1);
        hit_args_ = bgfx::createIndirectBuffer(1);
    }
    {
        gfx::render_pass pass("GI/Probe Trace Screen");
        pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
        // The ray count starts at zero, cleared ahead of the trace in its own view.
        ray_count_clear_.dispatch(pass.id, trace_rays_, 0, 1);
        programs_->trace->begin();
        bind_trace_inputs(params, layout, targets, radiance_cache_ready);
        bind_image(trace_output_stage, targets.trace_radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        bgfx::setBuffer(trace_rays_screen_stage, trace_rays_, bgfx::Access::ReadWrite);
        adaptive_probes_.dispatch(pass.id, *programs_->trace, lumen_adaptive_probes::args_group_per_probe);
        programs_->trace->end();
    }
    gfx::render_pass pass("GI/Probe Trace Far Field");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    ray_count_clear_.dispatch(pass.id, trace_hits_, 0, 1);
    dispatch_trace_args(pass.id, trace_rays_, far_field_args_);
    programs_->trace_far_field->begin();
    bind_trace_inputs(params, layout, targets, radiance_cache_ready);
    bind_image(trace_output_stage, targets.trace_radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bgfx::setBuffer(trace_rays_far_field_stage, trace_rays_, bgfx::Access::ReadWrite);
    bgfx::setBuffer(trace_hits_stage, trace_hits_, bgfx::Access::ReadWrite);
    bgfx::dispatch(pass.id, programs_->trace_far_field->native_handle(), far_field_args_, 0, 1);
    programs_->trace_far_field->end();
    // One thread per distance-field hit the far-field pass compacted, not per far ray.
    gfx::render_pass hit_pass("GI/Probe Trace Hits");
    hit_pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    dispatch_trace_args(hit_pass.id, trace_hits_, hit_args_);
    programs_->trace_hit_shade->begin();
    bind_trace_inputs(params, layout, targets, radiance_cache_ready);
    bind_image(trace_output_stage, targets.trace_radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bgfx::setBuffer(trace_rays_far_field_stage, trace_rays_, bgfx::Access::Read);
    bgfx::setBuffer(trace_hits_stage, trace_hits_, bgfx::Access::Read);
    bgfx::dispatch(hit_pass.id, programs_->trace_hit_shade->native_handle(), hit_args_, 0, 1);
    programs_->trace_hit_shade->end();
}

void lumen_gather_pass::run_visualize_traces(const lumen_run_params& params,
                                             const frame_layout& layout,
                                             const probe_targets& targets,
                                             bool radiance_cache_ready)
{
    if(!programs_->trace_visualize || !programs_->trace_visualize->is_valid())
    {
        return;
    }
    if(!bgfx::isValid(visualized_traces_))
    {
        visualized_traces_ = bgfx::createDynamicVertexBuffer(max_visualized_trace_count * visualized_trace_stride,
                                                             lumen_pass::get_vec4_layout(),
                                                             BGFX_BUFFER_COMPUTE_READ_WRITE);
    }
    gfx::render_pass pass("GI/Visualize Traces");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    programs_->trace_visualize->begin();
    bind_trace_inputs(params, layout, targets, radiance_cache_ready);
    bgfx::setBuffer(trace_output_stage, visualized_traces_, bgfx::Access::Write);
    const auto& cursor = params.visualize_traces.cursor;
    const math::vec4 query(cursor.x, cursor.y, 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_visualize_traces, query);
    // One group: the probe's rays.
    bgfx::dispatch(pass.id, programs_->trace_visualize->native_handle(), 1, 1, 1);
    programs_->trace_visualize->end();
    has_visualized_traces_ = true;
    visualized_trace_resolution_ = layout.trace_resolution;
}

void lumen_gather_pass::bind_trace_inputs(const lumen_run_params& params,
                                          const frame_layout& layout,
                                          const probe_targets& targets,
                                          bool radiance_cache_ready)
{
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    const auto& black = default_textures::get().black_texture();
    const bool has_hiz = params.hiz && params.hiz->is_valid();
    const bool has_prev_color = params.prev_color && params.prev_color->is_valid();
    const bool screen_traces = has_hiz && has_prev_color && params.prev_depth && params.settings.diffuse.screen_traces;
    gfx::set_texture(uniforms_.s_lumen_probe_records, 0, targets.records);
    gfx::set_texture(uniforms_.s_lumen_hiz, 1, has_hiz ? params.hiz : params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_prev_color, 2, has_prev_color ? params.prev_color : black);
    gfx::set_texture(uniforms_.s_lumen_env_sh, 3, params.irradiance_sh ? params.irradiance_sh : black);
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    gfx::set_texture(uniforms_.s_lumen_prev_depth, 6, params.prev_depth ? params.prev_depth : black);
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, lumen_pass::get_sdf_coverage(clipmap_gpu, experiments_));
    gfx::set_texture(uniforms_.s_sdf_clipmap_mip, 9, clipmap_gpu.get_mip_texture());
    bind_radiance_cache(radiance_cache_ready);
    gfx::set_texture(uniforms_.s_lumen_ray_info, 11, targets.ray_info);
    bind_motion(params, 12);
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
    gfx::set_uniform(uniforms_.u_pre_exposure, params.pre_exposure.to_uniform().data());
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
}

void lumen_gather_pass::run_composite(const frame_layout& layout, const probe_targets& targets)
{
    gfx::render_pass pass("GI/Probe Composite");
    programs_->composite->begin();
    gfx::set_texture(uniforms_.s_lumen_trace_radiance, 0, targets.trace_radiance);
    gfx::set_texture(uniforms_.s_lumen_probe_records, 1, targets.records);
    bind_image(2, targets.probe_radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_ray_info, 3, targets.ray_info);
    bind_image(4, targets.probe_moving, bgfx::Access::Write, bgfx::TextureFormat::R8);
    set_layout_uniforms(layout);
    adaptive_probes_.dispatch(pass.id, *programs_->composite, lumen_adaptive_probes::args_group_per_probe);
    programs_->composite->end();
}

auto lumen_gather_pass::run_filter(const lumen_run_params& params,
                                   const frame_layout& layout,
                                   const probe_targets& targets) -> gfx::texture::ptr
{
    auto source = targets.probe_radiance;
    for(int filter_pass = 0; filter_pass < int(LUMEN_FILTER_PASSES); ++filter_pass)
    {
        // The last pass writes the filtered radiance (the next frame's history); the ones before alternate with the
        // trace radiance, free once composited.
        const bool is_scratch = ((int(LUMEN_FILTER_PASSES) - 1 - filter_pass) & 1) != 0;
        const auto& target = is_scratch ? targets.trace_radiance : targets.filtered;
        gfx::render_pass pass("GI/Probe Filter");
        pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
        programs_->filter->begin();
        gfx::set_texture(uniforms_.s_lumen_probe_radiance, 0, source);
        gfx::set_texture(uniforms_.s_lumen_probe_records, 1, targets.records);
        bind_image(2, target, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        gfx::set_texture(uniforms_.s_lumen_screen_data, 3, targets.screen_data);
        gfx::set_texture(uniforms_.s_lumen_probe_moving, 4, targets.probe_moving);
        set_layout_uniforms(layout);
        adaptive_probes_.dispatch(pass.id, *programs_->filter, lumen_adaptive_probes::args_group_per_probe);
        programs_->filter->end();
        source = target;
    }
    return source;
}

void lumen_gather_pass::run_sh(const frame_layout& layout, const probe_targets& targets, const gfx::texture::ptr& filtered)
{
    gfx::render_pass pass("GI/Probe SH");
    programs_->sh->begin();
    gfx::set_texture(uniforms_.s_lumen_probe_filtered, 0, filtered);
    gfx::set_texture(uniforms_.s_lumen_probe_records, 1, targets.records);
    bind_image(2, targets.sh, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_probe_moving, 3, targets.probe_moving);
    set_layout_uniforms(layout);
    adaptive_probes_.dispatch(pass.id, *programs_->sh, lumen_adaptive_probes::args_group_per_probe);
    programs_->sh->end();
}

void lumen_gather_pass::run_border(const frame_layout& layout,
                                   const probe_targets& targets,
                                   const gfx::texture::ptr& filtered)
{
    gfx::render_pass pass("GI/Probe Border");
    programs_->border->begin();
    gfx::set_texture(uniforms_.s_lumen_probe_filtered, 0, filtered);
    gfx::set_texture(uniforms_.s_lumen_probe_records, 1, targets.records);
    bind_image(2, targets.probe_border, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    set_layout_uniforms(layout);
    adaptive_probes_.dispatch(pass.id, *programs_->border, lumen_adaptive_probes::args_border_threads);
    programs_->border->end();
}

void lumen_gather_pass::run_integrate(const lumen_run_params& params,
                                      const frame_layout& layout,
                                      const probe_targets& targets,
                                      const history_targets& history,
                                      const lumen_short_range_ao_pass::run_params& short_range_ao_params,
                                      const lumen_short_range_ao_pass::frame_targets& short_range_ao)
{
    const auto& black = default_textures::get().black_texture();
    const bool has_short_range_ao = short_range_ao.search != nullptr;
    const auto& program = has_short_range_ao ? programs_->integrate_short_range_ao : programs_->integrate;
    gfx::render_pass pass("GI/Integrate+Temporal");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    program->begin();
    gfx::set_texture(uniforms_.s_lumen_depth, 0, params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(uniforms_.s_lumen_probe_records, 2, targets.records);
    gfx::set_texture(uniforms_.s_lumen_probe_sh, 3, targets.sh);
    bind_image(4, history.rough_write, bgfx::Access::Write, rough_history_format);
    bind_image(6, history.write, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_history, 8, history.has_history ? history.read : black);
    gfx::set_texture(uniforms_.s_lumen_prev_depth, 9, params.prev_depth ? params.prev_depth : black);
    gfx::set_texture(uniforms_.s_lumen_rough_history, 10, history.has_history ? history.rough_read : black);
    gfx::set_texture(uniforms_.s_lumen_probe_border, 11, targets.probe_border);
    adaptive_probes_.bind_state(12, bgfx::Access::Read);
    bind_motion(params, 13);
    if(has_short_range_ao)
    {
        short_range_ao_.bind_accumulation(short_range_ao, short_range_ao_params, history.has_history);
    }
    set_layout_uniforms(layout);
    // Only the GI reflections read the rough specular (lumen_run_params::has_traced_reflections).
    const bool rough_specular_enabled =
        (experiments_ & experiment_no_rough_specular) == 0u && params.has_traced_reflections;
    const float temporal[4] = {history.has_history ? 1.0f : 0.0f,
                               layout.is_interpolation_stochastic ? 1.0f : 0.0f,
                               rough_specular_enabled ? 1.0f : 0.0f,
                               (experiments_ & experiment_show_interpolation_fallback) != 0u ? 1.0f : 0.0f};
    gfx::set_uniform(uniforms_.u_lumen_temporal, temporal);
    gfx::set_uniform(uniforms_.u_pre_exposure, params.pre_exposure.to_uniform().data());
    bgfx::dispatch(pass.id,
                   program->native_handle(),
                   divide_round_up(layout.view_size.width, group_edge),
                   divide_round_up(layout.view_size.height, group_edge),
                   1);
    program->end();
}

auto lumen_gather_pass::make_short_range_ao_params(const lumen_run_params& params, const frame_layout& layout) const
    -> lumen_short_range_ao_pass::run_params
{
    lumen_short_range_ao_pass::run_params ao_params;
    ao_params.gather = &params;
    ao_params.frame = layout.view_frame.data();
    ao_params.probes = layout.probes.data();
    ao_params.view = layout.view.data();
    ao_params.r2_noise = (experiments_ & experiment_short_range_ao_hash_noise) == 0u;
    ao_params.layout = lumen_pass::get_short_range_ao_layout(params.gi_quality);
    if((experiments_ & lumen_pass::experiment_full_res_short_range_ao) != 0u)
    {
        ao_params.layout.downsample_factor = 1u;
    }
    return ao_params;
}

void lumen_gather_pass::publish_short_range_ao(gfx::render_view& rview,
                                               const lumen_run_params& params,
                                               const lumen_short_range_ao_pass::frame_targets& short_range_ao)
{
    rview.tex_get_or_emplace(screen_ao_texture) = short_range_ao.screen;
    rview.data_get_or_emplace(screen_ao_frame, 0u) = uint32_t(gfx::get_render_frame());
    const float intensity = std::clamp(params.settings.ambient_occlusion.intensity, 0.0f, 1.0f);
    rview.data().get_or_emplace<float>(screen_ao_intensity, 1.0f) = intensity;
}

auto lumen_gather_pass::run(gfx::render_view& rview, const lumen_run_params& params) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/GI/Gather");
    rview.tex_remove("GI_ROUGH_SPECULAR");
    if(!has_programs() || !params.g_buffer || !params.cam || !params.view_cache)
    {
        return {};
    }
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    if(!clipmap_gpu.is_valid())
    {
        return {};
    }
    experiments_ = params.surface_cache ? params.surface_cache->get_experiment_flags() : 0u;
    starts_history_over_ = params.camera_cut || params.global_lighting_change;
    settings_uniform_ = lumen_pass::make_settings_uniform(params.settings, params.is_being_edited);
    auto layout = make_frame_layout(params.g_buffer->get_size(),
                                    params.settings.diffuse.quality,
                                    params.gi_quality,
                                    params.visualize_traces.enabled);
    if((experiments_ & lumen_pass::experiment_blended_probe_interpolation) != 0u)
    {
        layout.is_interpolation_stochastic = false;
    }
    // The expanded bilinear interpolation reads a 2x2 probe neighbourhood; probe records pack their pixel below
    // LUMEN_PROBE_MAX_VIEW_EXTENT per axis.
    const auto max_view_extent = uint32_t(LUMEN_PROBE_MAX_VIEW_EXTENT);
    const bool is_view_too_large =
        layout.view_size.width > max_view_extent || layout.view_size.height > max_view_extent;
    if(is_view_too_large && !has_logged_view_too_large_)
    {
        APPLOG_WARNING("[GI] The view is {} x {} px; global illumination serves views up to {} px per axis.",
                       layout.view_size.width,
                       layout.view_size.height,
                       max_view_extent);
        has_logged_view_too_large_ = true;
    }
    if(layout.probes_x < 2u || layout.probes_y < 2u || is_view_too_large ||
       !adaptive_probes_.ensure_resources(layout.probes_x * layout.probes_y))
    {
        return {};
    }
    programs_ = &get_probe_programs(layout.trace_resolution);
    const auto targets = acquire_probe_targets(rview, layout);
    probe_placement_ = {targets.records,
                        layout.frame,
                        layout.probes,
                        layout.view,
                        layout.adaptive_capacity,
                        gfx::get_render_frame()};
    const auto history = acquire_history(rview, params, layout.view_size);
    run_place(params, layout, targets);
    run_adaptive_probes(params, layout, targets);
    lumen_radiance_cache::frame_inputs cache_inputs;
    cache_inputs.params = &params;
    cache_inputs.probe_records = targets.records;
    cache_inputs.probes_x = layout.probes_x;
    cache_inputs.probe_rows = layout.atlas_rows;
    cache_inputs.frame = layout.view_frame.data();
    cache_inputs.probes = layout.probes.data();
    cache_inputs.view = layout.view.data();
    const auto cache_tier = (experiments_ & lumen_pass::experiment_epic_radiance_cache_probes) != 0u
                                ? lumen_pass::quality_level::epic
                                : params.gi_quality;
    cache_inputs.probe_resolution = lumen_pass::get_radiance_cache_probe_resolution(cache_tier);
    const bool radiance_cache_ready =
        (experiments_ & experiment_no_radiance_cache) == 0u && radiance_cache_.update(cache_inputs);
    is_radiance_cache_ready_ = radiance_cache_ready;
    radiance_cache_frame_ = gfx::get_render_frame();
    if((experiments_ & experiment_uniform_rays) == 0u)
    {
        run_generate_rays(params, layout, targets, history, radiance_cache_ready);
    }
    run_trace(params, layout, targets, radiance_cache_ready);
    // The recorded rays are kept while frozen, unless none were recorded yet.
    const auto& visualize_traces = params.visualize_traces;
    if(visualize_traces.enabled && (!visualize_traces.freeze || !has_visualized_traces_))
    {
        run_visualize_traces(params, layout, targets, radiance_cache_ready);
    }
    run_composite(layout, targets);
    const auto filtered = (experiments_ & experiment_no_spatial_filter) != 0u ? targets.probe_radiance
                                                                              : run_filter(params, layout, targets);
    run_sh(layout, targets, filtered);
    run_border(layout, targets, filtered);
    const auto short_range_ao_params = make_short_range_ao_params(params, layout);
    lumen_short_range_ao_pass::frame_targets short_range_ao;
    if(uses_short_range_ao(params.settings.ambient_occlusion))
    {
        short_range_ao = short_range_ao_.run_search(rview, short_range_ao_params);
    }
    run_integrate(params, layout, targets, history, short_range_ao_params, short_range_ao);
    if(short_range_ao.search)
    {
        publish_short_range_ao(rview, params, short_range_ao);
    }
    // The reflections read the gather's rough specular history.
    rview.tex_get_or_emplace("GI_ROUGH_SPECULAR") = history.rough_write;
    bgfx::discard();
    return history.write;
}

} // namespace unravel
