#include "lumen_reflection_pass.h"

#include "lumen_pass_common.h"
#include "lumen_surface_cache_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <graphics/graphics.h>
#include <logging/logging.h>

namespace unravel
{
using namespace gi::lumen;

namespace
{

using lumen_pass::bind_image;
using lumen_pass::divide_round_up;
using lumen_pass::ensure_texture;
using lumen_pass::group_edge;
using lumen_pass::has_view_size;

/// The noise sequences' frame period (UE ReflectionsStateFrameIndexMod8).
constexpr uint32_t reflection_state_frame_period = 8;
} // namespace

void lumen_reflection_pass::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_frame, "u_lumen_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_view, "u_lumen_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_reflection, "u_lumen_reflection", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_prev_view_proj, "u_lumen_prev_view_proj", bgfx::UniformType::Mat4);
    cache_uniform(nullptr, u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
    cache_uniform(nullptr,
                  u_sdf_clipmap_levels,
                  "u_sdf_clipmap_levels",
                  bgfx::UniformType::Vec4,
                  global_sdf_clipmap::level_count);
    cache_uniform(nullptr, u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_depth, "s_lumen_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_normal, "s_lumen_normal", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_hiz, "s_lumen_hiz", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_prev_color, "s_lumen_prev_color", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_prev_depth, "s_lumen_prev_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_env_sh, "s_lumen_env_sh", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_probe_layer, "s_lumen_probe_layer", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_coverage, "s_sdf_clipmap_coverage", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_reflection_ray, "s_lumen_reflection_ray", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_reflection_radiance, "s_lumen_reflection_radiance", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_reflection_hit, "s_lumen_reflection_hit", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_reflection_resolved, "s_lumen_reflection_resolved", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_reflection_history, "s_lumen_reflection_history", bgfx::UniformType::Sampler);
    cache_uniform(nullptr,
                  s_lumen_reflection_frames_history,
                  "s_lumen_reflection_frames_history",
                  bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_reflection_specular, "s_lumen_reflection_specular", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_reflection_frames, "s_lumen_reflection_frames", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_rough_specular, "s_lumen_rough_specular", bgfx::UniformType::Sampler);
}

auto lumen_reflection_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/lumen/") + name + ".sc");
        return std::make_shared<gpu_program>(shader);
    };
    screen_program_ = load("cs_lumen_reflection_screen");
    world_program_ = load("cs_lumen_reflection_world");
    resolve_program_ = load("cs_lumen_reflection_resolve");
    temporal_program_ = load("cs_lumen_reflection_temporal");
    spatial_program_ = load("cs_lumen_reflection_spatial");
    if(!has_programs())
    {
        APPLOG_WARNING("[Lumen] Reflection programs failed to load; Lumen reflections are unavailable for this run.");
    }
    return has_programs();
}

auto lumen_reflection_pass::has_programs() const -> bool
{
    const auto valid = [](const gpu_program::ptr& program)
    {
        return program && program->is_valid();
    };
    return valid(screen_program_) && valid(world_program_) && valid(resolve_program_) && valid(temporal_program_) &&
           valid(spatial_program_);
}

auto lumen_reflection_pass::get_max_roughness_to_trace(uint32_t experiments) -> float
{
    return (experiments & experiment_trace_all_roughness) != 0u ? 1.0f : LUMEN_MAX_ROUGHNESS_TO_TRACE;
}

auto lumen_reflection_pass::acquire_targets(gfx::render_view& rview, const usize32_t& size) -> frame_targets
{
    // The history ping-pong continues only from the frame right before this one (the previous depth it is
    // validated against is always that frame's).
    auto& parity = rview.data_get_or_emplace("LUMEN_REFLECTION_PARITY", 0u);
    const bool even_frame = (parity & 1u) == 0u;
    ++parity;
    auto& written_frame = rview.data_get_or_emplace("LUMEN_REFLECTION_FRAME", 0u);
    const uint32_t render_frame = gfx::get_render_frame();
    const bool continuous = written_frame != 0u && render_frame == written_frame + 1u;
    written_frame = render_frame;
    const std::string write_set = even_frame ? "_A" : "_B";
    const std::string read_set = even_frame ? "_B" : "_A";
    frame_targets targets;
    targets.ray = ensure_texture(rview, "LUMEN_REFLECTION_RAY", size, bgfx::TextureFormat::RGBA16F);
    targets.radiance = ensure_texture(rview, "LUMEN_REFLECTION_RADIANCE", size, bgfx::TextureFormat::RGBA16F);
    targets.hit = ensure_texture(rview, "LUMEN_REFLECTION_HIT", size, bgfx::TextureFormat::R32F);
    targets.resolved = ensure_texture(rview, "LUMEN_REFLECTION_RESOLVED", size, bgfx::TextureFormat::RGBA16F);
    targets.history_write =
        ensure_texture(rview, "LUMEN_REFLECTION_HISTORY" + write_set, size, bgfx::TextureFormat::RGBA16F);
    targets.frames_write = ensure_texture(rview, "LUMEN_REFLECTION_FRAMES" + write_set, size, bgfx::TextureFormat::R32F);
    targets.history_read = rview.tex_safe_get("LUMEN_REFLECTION_HISTORY" + read_set);
    targets.frames_read = rview.tex_safe_get("LUMEN_REFLECTION_FRAMES" + read_set);
    targets.has_history =
        continuous && has_view_size(targets.history_read, size) && has_view_size(targets.frames_read, size);
    return targets;
}

void lumen_reflection_pass::set_frame_uniforms(const run_params& params, const frame_targets& targets) const
{
    const auto& gather = *params.gather;
    const uint32_t frame = gfx::get_render_frame();
    const float frame_values[4] = {float(frame), float(frame % reflection_state_frame_period), 0.0f, 0.0f};
    gfx::set_uniform(uniforms_.u_lumen_frame, frame_values);
    const float view[4] = {float(view_size_.width),
                           float(view_size_.height),
                           1.0f / float(view_size_.width),
                           1.0f / float(view_size_.height)};
    gfx::set_uniform(uniforms_.u_lumen_view, view);
    const bool has_hiz = gather.hiz && gather.hiz->is_valid();
    const bool has_prev_color = gather.prev_color && gather.prev_color->is_valid();
    // Lumen traces the screen first (r.Lumen.Reflections.ScreenTraces 1) whenever last frame is available.
    const bool screen_traces =
        has_hiz && has_prev_color && gather.prev_depth && (experiments_ & experiment_no_screen_traces) == 0u;
    const uint32_t reflection_flags = (screen_traces ? 1u : 0u) |
                                      ((experiments_ & experiment_show_trace_types) != 0u ? 2u : 0u) |
                                      ((experiments_ & experiment_reflection_hash_noise) != 0u ? 4u : 0u);
    const float reflection[4] = {has_hiz ? float(gather.hiz->info.numMips) : 1.0f,
                                 float(reflection_flags),
                                 get_max_roughness_to_trace(experiments_),
                                 targets.has_history && gather.prev_depth ? 1.0f : 0.0f};
    gfx::set_uniform(uniforms_.u_lumen_reflection, reflection);
    gfx::set_uniform(uniforms_.u_lumen_prev_view_proj, gather.cam->get_prev_view_projection_unjittered().get_matrix());
    gfx::set_uniform(uniforms_.u_pre_exposure, gather.pre_exposure.to_uniform().data());
}

void lumen_reflection_pass::run_screen(const run_params& params, const frame_targets& targets) const
{
    const auto& gather = *params.gather;
    const auto& black = default_textures::get().black_texture();
    const auto& depth = gather.g_buffer->get_texture(4);
    const bool has_hiz = gather.hiz && gather.hiz->is_valid();
    const bool has_prev_color = gather.prev_color && gather.prev_color->is_valid();
    gfx::render_pass pass("GI/Lumen Reflections Screen Trace");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    screen_program_->begin();
    bind_image(0, targets.ray, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(1, targets.radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(2, targets.hit, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    gfx::set_texture(uniforms_.s_lumen_depth, 8, depth);
    gfx::set_texture(uniforms_.s_lumen_normal, 9, gather.g_buffer->get_texture(1));
    gfx::set_texture(uniforms_.s_lumen_hiz, 10, has_hiz ? gather.hiz : depth);
    gfx::set_texture(uniforms_.s_lumen_prev_color, 11, has_prev_color ? gather.prev_color : black);
    gfx::set_texture(uniforms_.s_lumen_prev_depth, 12, gather.prev_depth ? gather.prev_depth : black);
    set_frame_uniforms(params, targets);
    bgfx::dispatch(pass.id,
                   screen_program_->native_handle(),
                   divide_round_up(view_size_.width, group_edge),
                   divide_round_up(view_size_.height, group_edge),
                   1);
    screen_program_->end();
}

void lumen_reflection_pass::run_world(const run_params& params, const frame_targets& targets) const
{
    const auto& gather = *params.gather;
    const auto& black = default_textures::get().black_texture();
    const auto& clipmap_gpu = gather.view_cache->get_clipmap_gpu();
    const bool has_prev_color = gather.prev_color && gather.prev_color->is_valid();
    gfx::render_pass pass("GI/Lumen Reflections Distance Field");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    world_program_->begin();
    bind_image(0, targets.radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(1, targets.hit, bgfx::Access::ReadWrite, bgfx::TextureFormat::R32F);
    gfx::set_texture(uniforms_.s_lumen_reflection_ray, 2, targets.ray);
    gfx::set_texture(uniforms_.s_lumen_depth, 3, gather.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, lumen_pass::get_sdf_coverage(clipmap_gpu, experiments_));
    gfx::set_texture(uniforms_.s_lumen_prev_color, 5, has_prev_color ? gather.prev_color : black);
    gfx::set_texture(uniforms_.s_lumen_prev_depth, 6, gather.prev_depth ? gather.prev_depth : black);
    gfx::set_texture(uniforms_.s_lumen_env_sh, 7, gather.irradiance_sh ? gather.irradiance_sh : black);
    gfx::set_texture(uniforms_.s_lumen_probe_layer, 8, params.probe_output);
    gather.lumen_surface_cache->bind_for_sampling(13, 14, 15, true);
    set_frame_uniforms(params, targets);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    bgfx::dispatch(pass.id,
                   world_program_->native_handle(),
                   divide_round_up(view_size_.width, group_edge),
                   divide_round_up(view_size_.height, group_edge),
                   1);
    world_program_->end();
}

void lumen_reflection_pass::run_resolve(const run_params& params, const frame_targets& targets) const
{
    const auto& gather = *params.gather;
    gfx::render_pass pass("GI/Lumen Reflections Resolve");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    resolve_program_->begin();
    bind_image(0, targets.resolved, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_reflection_radiance, 8, targets.radiance);
    gfx::set_texture(uniforms_.s_lumen_reflection_hit, 9, targets.hit);
    gfx::set_texture(uniforms_.s_lumen_reflection_ray, 10, targets.ray);
    gfx::set_texture(uniforms_.s_lumen_depth, 11, gather.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 12, gather.g_buffer->get_texture(1));
    set_frame_uniforms(params, targets);
    bgfx::dispatch(pass.id,
                   resolve_program_->native_handle(),
                   divide_round_up(view_size_.width, group_edge),
                   divide_round_up(view_size_.height, group_edge),
                   1);
    resolve_program_->end();
}

void lumen_reflection_pass::run_temporal(const run_params& params, const frame_targets& targets) const
{
    const auto& gather = *params.gather;
    const auto& black = default_textures::get().black_texture();
    gfx::render_pass pass("GI/Lumen Reflections Temporal");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    temporal_program_->begin();
    bind_image(0, targets.history_write, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(1, targets.frames_write, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    gfx::set_texture(uniforms_.s_lumen_reflection_resolved, 8, targets.resolved);
    gfx::set_texture(uniforms_.s_lumen_reflection_history, 9, targets.has_history ? targets.history_read : black);
    gfx::set_texture(uniforms_.s_lumen_reflection_frames_history, 10, targets.has_history ? targets.frames_read : black);
    gfx::set_texture(uniforms_.s_lumen_prev_depth, 11, gather.prev_depth ? gather.prev_depth : black);
    gfx::set_texture(uniforms_.s_lumen_depth, 12, gather.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 13, gather.g_buffer->get_texture(1));
    set_frame_uniforms(params, targets);
    bgfx::dispatch(pass.id,
                   temporal_program_->native_handle(),
                   divide_round_up(view_size_.width, group_edge),
                   divide_round_up(view_size_.height, group_edge),
                   1);
    temporal_program_->end();
}

void lumen_reflection_pass::run_spatial(const run_params& params, const frame_targets& targets) const
{
    const auto& gather = *params.gather;
    gfx::render_pass pass("GI/Lumen Reflections Spatial+Composite");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    spatial_program_->begin();
    bind_image(0, params.traced_output, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(1, params.probe_output, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_reflection_specular, 8, targets.history_write);
    gfx::set_texture(uniforms_.s_lumen_reflection_frames, 9, targets.frames_write);
    gfx::set_texture(uniforms_.s_lumen_depth, 10, gather.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 11, gather.g_buffer->get_texture(1));
    // Without the gather's rough specular the untraced layer is left uncovered: the environment fills it.
    gfx::set_texture(uniforms_.s_lumen_rough_specular,
                     12,
                     params.rough_specular ? params.rough_specular : default_textures::get().transparent_texture());
    set_frame_uniforms(params, targets);
    bgfx::dispatch(pass.id,
                   spatial_program_->native_handle(),
                   divide_round_up(view_size_.width, group_edge),
                   divide_round_up(view_size_.height, group_edge),
                   1);
    spatial_program_->end();
}

auto lumen_reflection_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Lumen Reflections");
    const auto* gather = params.gather;
    if(!has_programs() || gather == nullptr || !gather->g_buffer || !gather->cam || !gather->view_cache ||
       gather->lumen_surface_cache == nullptr || !params.traced_output || !params.probe_output)
    {
        return false;
    }
    const auto& clipmap_gpu = gather->view_cache->get_clipmap_gpu();
    if(!clipmap_gpu.is_valid())
    {
        return false;
    }
    experiments_ = gather->surface_cache ? gather->surface_cache->get_experiment_flags() : 0u;
    view_size_ = gather->g_buffer->get_size();
    if(!has_view_size(params.traced_output, view_size_) || !has_view_size(params.probe_output, view_size_))
    {
        return false;
    }
    const auto targets = acquire_targets(rview, view_size_);
    run_screen(params, targets);
    run_world(params, targets);
    run_resolve(params, targets);
    run_temporal(params, targets);
    run_spatial(params, targets);
    bgfx::discard();
    return true;
}

} // namespace unravel
