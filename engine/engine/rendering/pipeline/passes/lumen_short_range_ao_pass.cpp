#include "lumen_short_range_ao_pass.h"

#include "lumen_pass_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

namespace unravel
{

namespace
{

using lumen_pass::bind_image;
using lumen_pass::divide_round_up;
using lumen_pass::ensure_texture;
using lumen_pass::group_edge;
using lumen_pass::has_view_size;

/// The search's and the accumulation's format: one uint per texel, the AO packed as UE packs it
/// (lumen_short_range_ao.sh LumenPackShortRangeAO).
constexpr bgfx::TextureFormat::Enum packed_ao_format = bgfx::TextureFormat::R32U;

/// The search grid of a view at @p downsample_factor.
auto get_search_size(const usize32_t& view_size, uint32_t downsample_factor) -> usize32_t
{
    return {divide_round_up(view_size.width, downsample_factor), divide_round_up(view_size.height, downsample_factor)};
}

} // namespace

void lumen_short_range_ao_pass::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_frame, "u_lumen_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_probes, "u_lumen_probes", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_view, "u_lumen_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_settings, "u_lumen_settings", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_short_range_ao, "u_lumen_short_range_ao", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_depth, "s_lumen_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_normal, "s_lumen_normal", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_short_range_ao, "s_lumen_short_range_ao", bgfx::UniformType::Sampler);
    cache_uniform(nullptr,
                  s_lumen_short_range_ao_history,
                  "s_lumen_short_range_ao_history",
                  bgfx::UniformType::Sampler);
}

auto lumen_short_range_ao_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/lumen/") + name + ".sc");
        return std::make_shared<gpu_program>(shader);
    };
    search_program_ = load("cs_lumen_short_range_ao");
    if(!has_programs())
    {
        APPLOG_WARNING("[GI] Short-range AO programs failed to load; the views keep the screen-space AO.");
    }
    return has_programs();
}

auto lumen_short_range_ao_pass::has_programs() const -> bool
{
    return search_program_ && search_program_->is_valid();
}

auto lumen_short_range_ao_pass::acquire_targets(gfx::render_view& rview,
                                                const usize32_t& size,
                                                uint32_t downsample_factor) -> frame_targets
{
    // The history ping-pong continues only from the frame right before this one (the previous depth it is
    // validated against is always that frame's).
    auto& parity = rview.data_get_or_emplace("LUMEN_SHORT_RANGE_AO_PARITY", 0u);
    const bool even_frame = (parity & 1u) == 0u;
    ++parity;
    auto& written_frame = rview.data_get_or_emplace("LUMEN_SHORT_RANGE_AO_FRAME", 0u);
    const uint32_t render_frame = gfx::get_render_frame();
    const bool continuous = written_frame != 0u && render_frame == written_frame + 1u;
    written_frame = render_frame;
    frame_targets targets;
    // The search and the accumulation hold the AO packed in one uint (lumen_short_range_ao.sh).
    targets.search = ensure_texture(rview,
                                    "LUMEN_SHORT_RANGE_AO_SEARCH",
                                    get_search_size(size, downsample_factor),
                                    packed_ao_format);
    targets.history_write = ensure_texture(rview,
                                           even_frame ? "LUMEN_SHORT_RANGE_AO_HISTORY_A" : "LUMEN_SHORT_RANGE_AO_HISTORY_B",
                                           size,
                                           packed_ao_format);
    targets.history_read =
        rview.tex_safe_get(even_frame ? "LUMEN_SHORT_RANGE_AO_HISTORY_B" : "LUMEN_SHORT_RANGE_AO_HISTORY_A");
    targets.screen = ensure_texture(rview, "LUMEN_SHORT_RANGE_AO_SCREEN", size, bgfx::TextureFormat::RGBA8);
    targets.has_history = continuous && has_view_size(targets.history_read, size);
    return targets;
}

void lumen_short_range_ao_pass::set_short_range_ao_uniform(const run_params& params, bool has_history) const
{
    const float short_range_ao[4] = {has_history ? 1.0f : 0.0f,
                                     params.r2_noise ? 1.0f : 0.0f,
                                     float(params.layout.downsample_factor),
                                     params.layout.foreground_reject_power};
    gfx::set_uniform(uniforms_.u_lumen_short_range_ao, short_range_ao);
}

auto lumen_short_range_ao_pass::run_search(gfx::render_view& rview, const run_params& params) -> frame_targets
{
    APP_SCOPE_PERF("Rendering/GI/Short Range AO");
    if(!has_programs() || params.gather == nullptr || !params.gather->g_buffer || params.gather->cam == nullptr ||
       params.frame == nullptr || params.probes == nullptr || params.view == nullptr)
    {
        return {};
    }
    const auto& gather = *params.gather;
    const auto size = gather.g_buffer->get_size();
    const auto targets = acquire_targets(rview, size, params.layout.downsample_factor);
    gfx::render_pass pass("GI/Short Range AO");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    search_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_depth, 0, gather.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 1, gather.g_buffer->get_texture(1));
    bind_image(2, targets.search, bgfx::Access::Write, packed_ao_format);
    gfx::set_uniform(uniforms_.u_lumen_frame, params.frame);
    gfx::set_uniform(uniforms_.u_lumen_probes, params.probes);
    gfx::set_uniform(uniforms_.u_lumen_view, params.view);
    gfx::set_uniform(uniforms_.u_lumen_settings,
                     lumen_pass::make_settings_uniform(gather.settings, gather.is_being_edited).data());
    set_short_range_ao_uniform(params, false);
    const auto search_size = get_search_size(size, params.layout.downsample_factor);
    bgfx::dispatch(pass.id,
                   search_program_->native_handle(),
                   divide_round_up(search_size.width, group_edge),
                   divide_round_up(search_size.height, group_edge),
                   1);
    search_program_->end();
    return targets;
}

void lumen_short_range_ao_pass::bind_accumulation(const frame_targets& targets,
                                                  const run_params& params,
                                                  bool has_gather_history) const
{
    const bool has_history = targets.has_history && has_gather_history;
    bind_image(5, targets.history_write, bgfx::Access::Write, packed_ao_format);
    bind_image(7, targets.screen, bgfx::Access::Write, bgfx::TextureFormat::RGBA8);
    gfx::set_texture(uniforms_.s_lumen_short_range_ao, 14, targets.search);
    gfx::set_texture(uniforms_.s_lumen_short_range_ao_history,
                     15,
                     // Without a history the accumulation reads none; a uint texture stands in for the uint sampler.
                     has_history ? targets.history_read : targets.search);
    set_short_range_ao_uniform(params, has_history);
}

} // namespace unravel
