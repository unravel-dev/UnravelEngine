#include "gi_reflection_pass.h"

#include "gi_pass_common.h"
#include "gi_surface_cache_pass.h"
#include "pass_helpers.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_pass_constants.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

namespace unravel
{
using namespace gi;

namespace
{

using gi_pass::bind_image;
using gi_pass::divide_round_up;
using gi_pass::ensure_texture;
using gi_pass::group_edge;
using gi_pass::has_view_size;

/// The pixels per reflection tile side (gi_reflection_common.sh GI_REFLECTION_TILE_PIXELS).
constexpr uint32_t reflection_tile_pixels = 8;
/// Trace texels per surface cache feedback tile side (cs_gi_reflection_feedback.eshader GI_FEEDBACK_TILE).
constexpr uint32_t feedback_tile_texels = 16;
/// The stage cs_gi_reflection_temporal.eshader declares the GPU pre-exposure at (PRE_EXPOSURE_STAGE). The screen trace
/// takes pre_exposure.sh's default, the distance-field stage reads it from the environment SH (s_gi_env_sh).
constexpr uint8_t temporal_pre_exposure_stage = 7;
/// The variant of the reflection shaders that traces the front layer (gi_reflection_common.sh).
constexpr const char* front_variant = "front";
/// The prefixes of the render view's names of each run's targets and trace state.
constexpr const char* opaque_target_prefix = "GI_REFLECTION";
constexpr const char* front_target_prefix = "GI_FRONT_REFLECTION";

/// @p value rounded down and up to a multiple of @p step (positive values).
auto round_down(int32_t value, int32_t step) -> int32_t
{
    return (value / step) * step;
}

auto round_up(int32_t value, int32_t step) -> int32_t
{
    return ((value + step - 1) / step) * step;
}

/// Whether @p tex holds at least @p size texels.
auto covers(const gfx::texture::ptr& tex, const math::ivec2& size) -> bool
{
    return tex && int32_t(tex->get_size().width) >= size.x && int32_t(tex->get_size().height) >= size.y;
}

auto get_tile_count(const usize32_t& size) -> usize32_t
{
    return {divide_round_up(size.width, reflection_tile_pixels), divide_round_up(size.height, reflection_tile_pixels)};
}
} // namespace

void gi_reflection_pass::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_gi_frame, "u_gi_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_view, "u_gi_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_reflection, "u_gi_reflection", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_reflection_quality, "u_gi_reflection_quality", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_settings, "u_gi_settings", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_prev_view_proj, "u_gi_prev_view_proj", bgfx::UniformType::Mat4);
    cache_uniform(nullptr, u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
    cache_uniform(nullptr,
                  u_sdf_clipmap_levels,
                  "u_sdf_clipmap_levels",
                  bgfx::UniformType::Vec4,
                  global_sdf_clipmap::level_count);
    cache_uniform(nullptr, u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_gi_depth, "s_gi_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_normal, "s_gi_normal", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_hiz, "s_gi_hiz", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_prev_color, "s_gi_prev_color", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_prev_depth, "s_gi_prev_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_env_sh, "s_gi_env_sh", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_probe_layer, "s_gi_probe_layer", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_coverage, "s_sdf_clipmap_coverage", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_mip, "s_sdf_clipmap_mip", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_reflection_ray, "s_gi_reflection_ray", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_reflection_radiance, "s_gi_reflection_radiance", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_reflection_hit, "s_gi_reflection_hit", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_reflection_resolved, "s_gi_reflection_resolved", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_reflection_history, "s_gi_reflection_history", bgfx::UniformType::Sampler);
    cache_uniform(nullptr,
                  s_gi_reflection_frames_history,
                  "s_gi_reflection_frames_history",
                  bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_reflection_specular, "s_gi_reflection_specular", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_reflection_frames, "s_gi_reflection_frames", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_rough_specular, "s_gi_rough_specular", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_reflection_tiles, "s_gi_reflection_tiles", bgfx::UniformType::Sampler);
    cache_uniform(nullptr,
                  s_gi_reflection_tiles_history,
                  "s_gi_reflection_tiles_history",
                  bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_gi_feedback, "u_gi_feedback", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_reflection_resolve, "u_gi_reflection_resolve", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_reflection_region, "u_gi_reflection_region", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_reflection_history_region, "u_gi_reflection_history_region", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_gi_scene_depth, "s_gi_scene_depth", bgfx::UniformType::Sampler);
    motion.cache_uniforms();
}

auto gi_reflection_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name, const char* variant) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/gi/") + name + ".eshader");
        return std::make_shared<gpu_program>(shader, variant);
    };
    const auto load_set = [&](program_set& set, const char* variant)
    {
        set.screen = load("cs_gi_reflection_screen", variant);
        set.world = load("cs_gi_reflection_world", variant);
        set.resolve = load("cs_gi_reflection_resolve", variant);
        set.temporal = load("cs_gi_reflection_temporal", variant);
        set.spatial = load("cs_gi_reflection_spatial", variant);
    };
    load_set(programs_, gfx::shader::DEFAULT_VARIANT);
    // Optional: without it the reflections read every card's locked mip.
    programs_.feedback = load("cs_gi_reflection_feedback", gfx::shader::DEFAULT_VARIANT);
    load_set(front_programs_, front_variant);
    if(!has_programs() && gi_pass::is_supported())
    {
        APPLOG_WARNING("[GI] Reflection programs failed to load; traced reflections are unavailable for this run.");
    }
    else if(!has_front_programs() && gi_pass::is_supported())
    {
        APPLOG_WARNING("[GI] The front layer's reflection programs failed to load, so see-through surfaces with GI "
                       "Reflections reflect the GI's blurred light for this run.");
    }
    return has_programs();
}

auto gi_reflection_pass::program_set::is_valid() const -> bool
{
    const auto valid = [](const gpu_program::ptr& program)
    {
        return program && program->is_valid();
    };
    return valid(screen) && valid(world) && valid(resolve) && valid(temporal) && valid(spatial);
}

auto gi_reflection_pass::has_programs() const -> bool
{
    return programs_.is_valid();
}

auto gi_reflection_pass::has_front_programs() const -> bool
{
    return front_programs_.is_valid();
}

auto gi_reflection_pass::get_max_roughness_to_trace(const gi_settings::reflection_settings& settings) -> float
{
    return gi_pass::get_max_roughness_to_trace(settings);
}

auto gi_reflection_pass::get_trace_size(const usize32_t& view_size, uint32_t downsample) -> usize32_t
{
    return {divide_round_up(view_size.width, downsample), divide_round_up(view_size.height, downsample)};
}

auto gi_reflection_pass::acquire_targets(gfx::render_view& rview,
                                         const layer_run& layer,
                                         const usize32_t& size,
                                         const usize32_t& trace_size,
                                         bool camera_cut) -> frame_targets
{
    // The history ping-pong continues only from the frame right before this one (the previous depth it is
    // validated against is always that frame's).
    const std::string& prefix = layer.target_prefix;
    auto& state = rview.data().get_or_emplace<trace_state>(layer.is_front ? prefix + "_TRACE_STATE" : trace_state::view_key);
    const auto step = state.history.advance(gfx::get_render_frame());
    const std::string write_set = step.writes_even ? "_A" : "_B";
    const std::string read_set = step.writes_even ? "_B" : "_A";
    frame_targets targets;
    targets.history_origin = state.region_origin;
    targets.history_size = state.region_size;
    state.region_origin = layer.region.pixel_origin;
    state.region_size = layer.region.pixel_size;
    const usize32_t& own_size = layer.target_size;
    // Both runs trace into the same targets: the front run follows the opaque one, whose resolve has read them.
    targets.ray = ensure_texture(rview, ray_texture, trace_size, bgfx::TextureFormat::RGBA16F);
    targets.radiance = ensure_texture(rview, radiance_texture, trace_size, bgfx::TextureFormat::RGBA16F);
    targets.hit = ensure_texture(rview, hit_texture, trace_size, bgfx::TextureFormat::R32F);
    targets.resolved = ensure_texture(rview, gi_pass::view_scratch_rgba16f, size, bgfx::TextureFormat::RGBA16F);
    targets.history_write = ensure_texture(rview, prefix + "_HISTORY" + write_set, own_size, bgfx::TextureFormat::RGBA16F);
    targets.frames_write = ensure_texture(rview, prefix + "_FRAMES" + write_set, own_size, bgfx::TextureFormat::R32F);
    targets.history_read = rview.tex_find(prefix + "_HISTORY" + read_set);
    targets.frames_read = rview.tex_find(prefix + "_FRAMES" + read_set);
    targets.tiles_write = ensure_texture(rview, prefix + "_TILES" + write_set, get_tile_count(own_size), bgfx::TextureFormat::R8);
    targets.tiles_read = rview.tex_find(prefix + "_TILES" + read_set);
    targets.has_history = step.is_continuous && !camera_cut && holds_history(targets, layer);
    return targets;
}

auto gi_reflection_pass::holds_history(const frame_targets& targets, const layer_run& layer) -> bool
{
    if(!layer.is_front)
    {
        const usize32_t& size = layer.target_size;
        return has_view_size(targets.history_read, size) && has_view_size(targets.frames_read, size) &&
               has_view_size(targets.tiles_read, get_tile_count(size));
    }
    // The front layer's targets change size with its region: they hold last frame's region from their first texel.
    const math::ivec2& size = targets.history_size;
    const usize32_t tiles = get_tile_count({uint32_t(std::max(size.x, 0)), uint32_t(std::max(size.y, 0))});
    return size.x > 0 && size.y > 0 && covers(targets.history_read, size) && covers(targets.frames_read, size) &&
           covers(targets.tiles_read, {int32_t(tiles.width), int32_t(tiles.height)});
}

void gi_reflection_pass::set_frame_uniforms(const layer_run& layer, const frame_targets& targets) const
{
    const auto& gather = *layer.gather;
    const uint32_t frame = gfx::get_render_frame();
    const float frame_values[4] = {gi_pass::get_frame_index(frame), float(frame % state_frame_period), 0.0f, 0.0f};
    gfx::set_uniform(uniforms_.u_gi_frame, frame_values);
    const float view[4] = {float(view_size_.width),
                           float(view_size_.height),
                           1.0f / float(view_size_.width),
                           1.0f / float(view_size_.height)};
    gfx::set_uniform(uniforms_.u_gi_view, view);
    const bool has_hiz = gather.hiz && gather.hiz->is_valid();
    const bool has_prev_color = gather.prev_color && gather.prev_color->is_valid();
    // The screen trace needs last frame's colour and depth beside this frame's Hi-Z.
    const bool screen_traces =
        has_hiz && has_prev_color && gather.prev_depth && gather.settings.reflections.screen_traces;
    // The distant screen traces run whenever the screen traces do, unless an experiment turns them off.
    const bool distant_screen_traces =
        screen_traces && (experiments_ & gi_pass::experiment_no_distant_screen_traces) == 0u;
    const uint32_t reflection_flags = (screen_traces ? 1u : 0u) |
                                      ((experiments_ & experiment_show_trace_types) != 0u ? 2u : 0u) |
                                      ((experiments_ & gi_pass::experiment_all_reflection_tiles) != 0u ? 4u : 0u) |
                                      (distant_screen_traces ? 8u : 0u);
    const float reflection[4] = {has_hiz ? float(gather.hiz->info.numMips) : 1.0f,
                                 float(reflection_flags),
                                 get_max_roughness_to_trace(gather.settings.reflections),
                                 targets.has_history && layer.prev_depth ? 1.0f : 0.0f};
    gfx::set_uniform(uniforms_.u_gi_reflection, reflection);
    const float quality[4] = {
        float(downsample_),
        float(gi_pass::get_reflection_reconstruction_samples(gather.settings.reflections, gather.reflection_quality)),
        layer.rough_specular_scale,
        layer.rough_specular ? 1.0f : 0.0f};
    gfx::set_uniform(uniforms_.u_gi_reflection_quality, quality);
    const float resolve[4] = {gi_pass::get_reflection_reconstruction_min_weight(gather.reflection_quality),
                              0.0f,
                              0.0f,
                              0.0f};
    gfx::set_uniform(uniforms_.u_gi_reflection_resolve, resolve);
    gfx::set_uniform(uniforms_.u_gi_settings,
                     gi_pass::make_settings_uniform(gather.settings, gather.is_being_edited).data());
    gfx::set_uniform(uniforms_.u_gi_prev_view_proj, gather.cam->get_prev_view_projection_unjittered().get_matrix());
    gfx::set_uniform(uniforms_.u_pre_exposure, gather.pre_exposure.to_uniform().data());
    const math::vec4 region(float(layer.region.pixel_origin.x),
                            float(layer.region.pixel_origin.y),
                            float(layer.region.pixel_size.x),
                            float(layer.region.pixel_size.y));
    gfx::set_uniform(uniforms_.u_gi_reflection_region, region);
    const math::vec4 history_region(float(targets.history_origin.x),
                                    float(targets.history_origin.y),
                                    float(targets.history_size.x),
                                    float(targets.history_size.y));
    gfx::set_uniform(uniforms_.u_gi_reflection_history_region, history_region);
}

auto gi_reflection_pass::get_trace_groups(const layer_run& layer) -> math::uvec2
{
    return {divide_round_up(uint32_t(layer.region.trace_size.x), group_edge),
            divide_round_up(uint32_t(layer.region.trace_size.y), group_edge)};
}

auto gi_reflection_pass::get_pixel_groups(const layer_run& layer) -> math::uvec2
{
    return {divide_round_up(uint32_t(layer.region.pixel_size.x), group_edge),
            divide_round_up(uint32_t(layer.region.pixel_size.y), group_edge)};
}

void gi_reflection_pass::run_screen(const layer_run& layer, const frame_targets& targets) const
{
    const auto& gather = *layer.gather;
    const auto& black = default_textures::get().black_texture();
    const auto& depth = layer.depth;
    const bool has_hiz = gather.hiz && gather.hiz->is_valid();
    const bool has_prev_color = gather.prev_color && gather.prev_color->is_valid();
    const auto& program = layer.programs->screen;
    gfx::render_pass pass(layer.is_front ? "GI/Front Reflections Screen Trace" : "GI/Reflections Screen Trace");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    program->begin();
    bind_image(0, targets.ray, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(1, targets.radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(2, targets.hit, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    bind_image(3, targets.tiles_write, bgfx::Access::Write, bgfx::TextureFormat::R8);
    gfx::set_texture(uniforms_.s_gi_depth, 8, depth);
    gfx::set_texture(uniforms_.s_gi_normal, 9, layer.normal);
    gfx::set_texture(uniforms_.s_gi_hiz, 10, has_hiz ? gather.hiz : layer.scene_depth);
    gfx::set_texture(uniforms_.s_gi_prev_color, 11, has_prev_color ? gather.prev_color : black);
    gfx::set_texture(uniforms_.s_gi_prev_depth, 12, gather.prev_depth ? gather.prev_depth : black);
    set_frame_uniforms(layer, targets);
    // The hits' motion: the opaque scene's velocity buffer in either run.
    uniforms_.motion.bind(13, gather.velocity, *gather.cam, experiments_);
    gather.pre_exposure.bind();
    const auto groups = get_trace_groups(layer);
    gfx::dispatch(pass.id, program->native_handle(), groups.x, groups.y, 1);
    program->end();
}

void gi_reflection_pass::run_feedback(const layer_run& layer, const frame_targets& targets) const
{
    const auto& gather = *layer.gather;
    auto* feedback = gather.surface_cache_feedback;
    const auto& feedback_program = layer.programs->feedback;
    if(feedback == nullptr || !feedback_program || !begin_program(feedback_program))
    {
        return;
    }
    const auto& clipmap_gpu = gather.view_cache->get_clipmap_gpu();
    gfx::render_pass pass("GI/Reflections Feedback");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    feedback->begin_frame(pass.id, gather.gi_surface_cache->get_card_index_revision());
    gfx::set_texture(uniforms_.s_gi_reflection_ray, 0, targets.ray);
    gfx::set_texture(uniforms_.s_gi_reflection_hit, 1, targets.hit);
    gfx::set_texture(uniforms_.s_gi_depth, 2, layer.depth);
    feedback->bind(3, 5);
    gfx::set_texture(uniforms_.s_gi_reflection_radiance, 6, targets.radiance);
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    gfx::set_texture(uniforms_.s_sdf_clipmap_mip, 9, clipmap_gpu.get_mip_texture());
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, gi_pass::get_sdf_coverage(clipmap_gpu, experiments_));
    gfx::set_texture(uniforms_.s_gi_reflection_tiles, 12, targets.tiles_write);
    gather.gi_surface_cache->bind_for_sampling(13, 14, 15, true);
    set_frame_uniforms(layer, targets);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    const math::uvec2 jitter = gi_surface_cache_feedback::get_tile_jitter(gfx::get_render_frame());
    const math::vec4 feedback_params(float(jitter.x),
                                     float(jitter.y),
                                     float(gi_surface_cache_feedback::hash_slots - 1u),
                                     0.0f);
    gfx::set_uniform(uniforms_.u_gi_feedback, feedback_params);
    const uint32_t tiles_x = divide_round_up(trace_size_.width, feedback_tile_texels);
    const uint32_t tiles_y = divide_round_up(trace_size_.height, feedback_tile_texels);
    gfx::dispatch(pass.id,
                   feedback_program->native_handle(),
                   divide_round_up(tiles_x, group_edge),
                   divide_round_up(tiles_y, group_edge),
                   1);
    feedback_program->end();
    feedback->end_frame(pass.id, tiles_x * tiles_y);
}

void gi_reflection_pass::run_world(const layer_run& layer, const frame_targets& targets) const
{
    const auto& gather = *layer.gather;
    const auto& black = default_textures::get().black_texture();
    const auto& clipmap_gpu = gather.view_cache->get_clipmap_gpu();
    const bool has_prev_color = gather.prev_color && gather.prev_color->is_valid();
    const auto& program = layer.programs->world;
    gfx::render_pass pass(layer.is_front ? "GI/Front Reflections Distance Field" : "GI/Reflections Distance Field");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    program->begin();
    bind_image(0, targets.radiance, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(1, targets.hit, bgfx::Access::ReadWrite, bgfx::TextureFormat::R32F);
    gfx::set_texture(uniforms_.s_gi_reflection_ray, 2, targets.ray);
    gfx::set_texture(uniforms_.s_gi_depth, 3, layer.depth);
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, gi_pass::get_sdf_coverage(clipmap_gpu, experiments_));
    gfx::set_texture(uniforms_.s_sdf_clipmap_mip, 9, clipmap_gpu.get_mip_texture());
    gfx::set_texture(uniforms_.s_gi_prev_color, 5, has_prev_color ? gather.prev_color : black);
    gfx::set_texture(uniforms_.s_gi_prev_depth, 6, gather.prev_depth ? gather.prev_depth : black);
    gfx::set_texture(uniforms_.s_gi_env_sh, 7, gather.irradiance_sh ? gather.irradiance_sh : black);
    gfx::set_texture(uniforms_.s_gi_probe_layer, 8, layer.probe_output);
    gfx::set_texture(uniforms_.s_gi_reflection_tiles, 12, targets.tiles_write);
    gather.gi_surface_cache->bind_for_sampling(13, 14, 15, true);
    set_frame_uniforms(layer, targets);
    if(layer.is_front)
    {
        // The front variant reads the opaque depth for its on-screen hits where the default one reads the velocity.
        gfx::set_texture(uniforms_.s_gi_scene_depth, 11, layer.scene_depth);
    }
    else
    {
        uniforms_.motion.bind(11, gather.velocity, *gather.cam, experiments_);
    }
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    const auto groups = get_trace_groups(layer);
    gfx::dispatch(pass.id, program->native_handle(), groups.x, groups.y, 1);
    program->end();
}

void gi_reflection_pass::run_resolve(const layer_run& layer, const frame_targets& targets) const
{
    const auto& gather = *layer.gather;
    const auto& program = layer.programs->resolve;
    gfx::render_pass pass(layer.is_front ? "GI/Front Reflections Resolve" : "GI/Reflections Resolve");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    program->begin();
    bind_image(0, targets.resolved, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_gi_reflection_radiance, 8, targets.radiance);
    gfx::set_texture(uniforms_.s_gi_reflection_hit, 9, targets.hit);
    gfx::set_texture(uniforms_.s_gi_reflection_ray, 10, targets.ray);
    gfx::set_texture(uniforms_.s_gi_depth, 11, layer.depth);
    gfx::set_texture(uniforms_.s_gi_normal, 12, layer.normal);
    gfx::set_texture(uniforms_.s_gi_reflection_tiles, 13, targets.tiles_write);
    set_frame_uniforms(layer, targets);
    const auto groups = get_pixel_groups(layer);
    gfx::dispatch(pass.id, program->native_handle(), groups.x, groups.y, 1);
    program->end();
}

void gi_reflection_pass::run_temporal(const layer_run& layer, const frame_targets& targets) const
{
    const auto& gather = *layer.gather;
    const auto& black = default_textures::get().black_texture();
    const auto& program = layer.programs->temporal;
    gfx::render_pass pass(layer.is_front ? "GI/Front Reflections Temporal" : "GI/Reflections Temporal");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    program->begin();
    bind_image(0, targets.history_write, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_image(1, targets.frames_write, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    gfx::set_texture(uniforms_.s_gi_reflection_resolved, 8, targets.resolved);
    gfx::set_texture(uniforms_.s_gi_reflection_history, 9, targets.has_history ? targets.history_read : black);
    gfx::set_texture(uniforms_.s_gi_reflection_frames_history, 10, targets.has_history ? targets.frames_read : black);
    gfx::set_texture(uniforms_.s_gi_reflection_tiles_history, 2, targets.has_history ? targets.tiles_read : black);
    gfx::set_texture(uniforms_.s_gi_reflection_tiles, 15, targets.tiles_write);
    gfx::set_texture(uniforms_.s_gi_prev_depth, 11, layer.prev_depth ? layer.prev_depth : black);
    gfx::set_texture(uniforms_.s_gi_depth, 12, layer.depth);
    gfx::set_texture(uniforms_.s_gi_normal, 13, layer.normal);
    set_frame_uniforms(layer, targets);
    // Stage 14 is free in the front variant, which reprojects by the camera.
    uniforms_.motion.bind(14, layer.velocity, *gather.cam, experiments_);
    gather.pre_exposure.bind(temporal_pre_exposure_stage);
    const auto groups = get_pixel_groups(layer);
    gfx::dispatch(pass.id, program->native_handle(), groups.x, groups.y, 1);
    program->end();
}

void gi_reflection_pass::run_spatial(const layer_run& layer, const frame_targets& targets) const
{
    const auto& gather = *layer.gather;
    const auto& program = layer.programs->spatial;
    gfx::render_pass pass(layer.is_front ? "GI/Front Reflections Spatial+Composite" : "GI/Reflections Spatial+Composite");
    pass.set_view_proj(gather.cam->get_view(), gather.cam->get_projection_unjittered());
    program->begin();
    bind_image(0, layer.output, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_gi_reflection_specular, 8, targets.history_write);
    gfx::set_texture(uniforms_.s_gi_reflection_frames, 9, targets.frames_write);
    gfx::set_texture(uniforms_.s_gi_depth, 10, layer.depth);
    gfx::set_texture(uniforms_.s_gi_normal, 11, layer.normal);
    gfx::set_texture(uniforms_.s_gi_reflection_tiles, 13, targets.tiles_write);
    set_frame_uniforms(layer, targets);
    const auto groups = get_pixel_groups(layer);
    gfx::dispatch(pass.id, program->native_handle(), groups.x, groups.y, 1);
    program->end();
}

auto gi_reflection_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Reflections");
    const auto* gather = params.gather;
    if(!has_programs() || gather == nullptr || !gather->g_buffer || !gather->cam || !gather->view_cache ||
       gather->gi_surface_cache == nullptr || !params.traced_output || !params.probe_output)
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
    downsample_ = gi_pass::get_reflection_downsample_factor(gather->settings.reflections, gather->reflection_quality);
    trace_size_ = get_trace_size(view_size_, downsample_);
    rview.data().get_or_emplace<trace_state>(trace_state::view_key).downsample = downsample_;
    layer_run layer;
    layer.gather = gather;
    layer.programs = &programs_;
    layer.depth = gather->g_buffer->get_texture(4);
    layer.normal = gather->g_buffer->get_texture(1);
    layer.scene_depth = layer.depth;
    layer.prev_depth = gather->prev_depth;
    layer.velocity = gather->velocity;
    layer.rough_specular = params.rough_specular;
    layer.rough_specular_scale = params.rough_specular_scale;
    layer.probe_output = params.probe_output;
    layer.output = params.traced_output;
    layer.target_prefix = opaque_target_prefix;
    layer.region.trace_size = {int32_t(trace_size_.width), int32_t(trace_size_.height)};
    layer.region.pixel_size = {int32_t(view_size_.width), int32_t(view_size_.height)};
    layer.target_size = view_size_;
    run_layer(rview, layer);
    return true;
}

void gi_reflection_pass::run_layer(gfx::render_view& rview, const layer_run& layer)
{
    const auto targets = acquire_targets(rview, layer, view_size_, trace_size_, layer.gather->camera_cut);
    if(!layer.is_front)
    {
        rview.tex_acquire(tiles_texture) = targets.tiles_write;
    }
    run_screen(layer, targets);
    run_world(layer, targets);
    if(!layer.is_front)
    {
        run_feedback(layer, targets);
    }
    run_resolve(layer, targets);
    run_temporal(layer, targets);
    run_spatial(layer, targets);
    bgfx::discard();
}

auto gi_reflection_pass::get_front_dispatch_region(const math::ivec2& region_min,
                                                   const math::ivec2& region_max,
                                                   const usize32_t& view_size,
                                                   uint32_t downsample) -> dispatch_region
{
    // Whole trace groups: the screen pass classifies tiles per group, and a group's first pixel then starts a tile.
    const auto step = int32_t(group_edge);
    const auto factor = int32_t(std::max(downsample, 1u));
    const math::ivec2 trace_view = {int32_t(divide_round_up(view_size.width, uint32_t(factor))),
                                    int32_t(divide_round_up(view_size.height, uint32_t(factor)))};
    const math::ivec2 low = math::max(region_min - front_region_margin, math::ivec2(0));
    const math::ivec2 high = math::min(region_max + front_region_margin, math::ivec2(int32_t(view_size.width), int32_t(view_size.height)));
    dispatch_region region;
    if(high.x <= low.x || high.y <= low.y)
    {
        return region;
    }
    region.trace_origin = {round_down(low.x / factor, step), round_down(low.y / factor, step)};
    const math::ivec2 trace_end = math::min(math::ivec2(round_up((high.x + factor - 1) / factor, step), round_up((high.y + factor - 1) / factor, step)),
                                            math::ivec2(round_up(trace_view.x, step), round_up(trace_view.y, step)));
    region.trace_size = trace_end - region.trace_origin;
    region.pixel_origin = region.trace_origin * factor;
    const math::ivec2 pixel_end = math::min(trace_end * factor, math::ivec2(int32_t(view_size.width), int32_t(view_size.height)));
    region.pixel_size = pixel_end - region.pixel_origin;
    return region;
}

auto gi_reflection_pass::get_front_region(const math::ivec2& region_min, const math::ivec2& region_max) const
    -> dispatch_region
{
    return get_front_dispatch_region(region_min, region_max, view_size_, downsample_);
}

auto gi_reflection_pass::update_front_target_size(front_target_size& state,
                                                  const math::ivec2& region_size,
                                                  const usize32_t& view_size) -> usize32_t
{
    const math::ivec2 view(int32_t(view_size.width), int32_t(view_size.height));
    const math::ivec2 wanted = math::min(math::ivec2(round_up(std::max(region_size.x, 1), front_target_step),
                                                     round_up(std::max(region_size.y, 1), front_target_step)),
                                         view);
    const math::ivec2 grown = math::min(math::max(state.size, wanted), view);
    if(grown != state.size)
    {
        state.size = grown;
        state.smaller_frames = 0;
    }
    else if(wanted != state.size)
    {
        state.smaller_frames++;
        if(state.smaller_frames >= front_target_shrink_frames)
        {
            state.size = wanted;
            state.smaller_frames = 0;
        }
    }
    else
    {
        state.smaller_frames = 0;
    }
    return {uint32_t(state.size.x), uint32_t(state.size.y)};
}

auto gi_reflection_pass::can_run_front_layer(const front_layer_params& params) const -> bool
{
    const auto* gather = params.gather;
    const bool has_scene = gather != nullptr && gather->g_buffer && gather->cam && gather->view_cache &&
                           gather->gi_surface_cache != nullptr && gather->view_cache->get_clipmap_gpu().is_valid();
    // It follows run() this frame, at its view size and trace downsample.
    const bool follows_run = has_scene && gather->g_buffer->get_size() == view_size_;
    const auto& region = params.region;
    const math::ivec2 target_size(int32_t(params.target_size.width), int32_t(params.target_size.height));
    const bool has_region = region.pixel_size.x > 0 && region.pixel_size.y > 0 && region.pixel_size.x <= target_size.x &&
                            region.pixel_size.y <= target_size.y;
    const bool has_targets = covers(params.output, target_size) && covers(params.depth, target_size) &&
                             covers(params.normal, target_size);
    return has_front_programs() && follows_run && has_region && has_targets;
}

void gi_reflection_pass::release_front_layer(gfx::render_view& rview)
{
    const std::string prefix = front_target_prefix;
    for(const char* name : {"_HISTORY_A", "_HISTORY_B", "_FRAMES_A", "_FRAMES_B", "_TILES_A", "_TILES_B"})
    {
        rview.tex_remove(prefix + name);
    }
    rview.data().remove(prefix + "_TRACE_STATE");
}

auto gi_reflection_pass::run_front_layer(gfx::render_view& rview, const front_layer_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Front Reflections");
    if(!can_run_front_layer(params))
    {
        return false;
    }
    const auto* gather = params.gather;
    layer_run layer;
    layer.gather = gather;
    layer.programs = &front_programs_;
    layer.depth = params.depth;
    layer.normal = params.normal;
    layer.scene_depth = gather->g_buffer->get_texture(4);
    layer.prev_depth = params.prev_depth;
    layer.rough_specular_scale = 1.0f;
    // The misses take the environment SH: the probe layer reflects the opaque surface behind (the front variant reads
    // none, the stage keeps a texture).
    layer.probe_output = default_textures::get().black_texture();
    layer.output = params.output;
    layer.target_prefix = front_target_prefix;
    layer.region = params.region;
    layer.target_size = params.target_size;
    layer.is_front = true;
    run_layer(rview, layer);
    front_written_min_ = layer.region.pixel_origin;
    front_written_max_ = layer.region.pixel_origin + layer.region.pixel_size;
    return true;
}

} // namespace unravel
