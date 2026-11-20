#include "pipeline_internal.h"

#include <engine/rendering/material_graph/graph_material.h>

namespace unravel
{
namespace
{
namespace ANONYMOUS
{
using namespace rendering::deferred_detail;

/// GI experiment flag (surface_cache_system::get_experiment_flags): the global SDF clipmap stays where it was when
/// the bit was set (no camera re-snaps), to isolate re-snap transients in A/Bs.
constexpr uint32_t gi_experiment_freeze_clipmap_origin = 1u << 27u;
/// The global distance field's level scale for the GI: each level doubles the previous one's extent.
constexpr float gi_clipmap_level_scale = 2.0f;
/// The scene detail's range; 1 / detail scales the smallest object the distance field keeps.
constexpr float gi_clipmap_min_detail = 0.01f;
constexpr float gi_clipmap_max_detail = 100.0f;

/// The global distance field the GI traces (its hits read the surface cache) in the GI's layout (gi_pass_constants.h),
/// with the view's rebuild budget, level blend and the smallest object its scene detail keeps, partially updated
/// unless @p experiments holds gi_pass::experiment_no_sdf_partial_updates.
auto make_gi_clipmap_settings(const gi_settings& gi, bool compose_on_gpu, uint64_t experiments)
    -> global_sdf_clipmap::settings
{
    const auto& field = gi.distance_field;
    global_sdf_clipmap::settings settings;
    settings.resolution = uint32_t(gi::GI_GLOBAL_SDF_RESOLUTION);
    settings.base_extent = gi::GI_GLOBAL_SDF_EXTENT;
    settings.level_scale = gi_clipmap_level_scale;
    settings.compose_on_gpu = compose_on_gpu;
    settings.max_levels_per_update = std::clamp(field.levels_per_update, 1u, global_sdf_clipmap::level_count);
    settings.blend_voxels = std::max(field.level_blend_band, 0.0f);
    settings.object_radius_scale =
        1.0f / std::clamp(gi.scene.detail, gi_clipmap_min_detail, gi_clipmap_max_detail);
    settings.partial_updates = (experiments & gi_pass::experiment_no_sdf_partial_updates) == 0u;
    return settings;
}
} // namespace ANONYMOUS
} // namespace

namespace rendering
{

void deferred::run_gi_visualize_scene_color(const camera& camera,
                                            gfx::render_view& rview,
                                            const pre_exposure_state& pre_exposure)
{
    if(!gi_visualize_settings_.is_any_in_scene_color())
    {
        return;
    }
    gi_visualize_pass::world_params world;
    const auto& lbuffer = rview.fbo_find("LBUFFER");
    const auto& gbuffer = rview.fbo_find("GBUFFER");
    world.scene_color = lbuffer ? lbuffer->get_texture(0) : nullptr;
    world.scene_depth = gbuffer ? gbuffer->get_texture(4) : nullptr;
    world.rview = &rview;
    world.cam = &camera;
    world.surface_cache = &gi_surface_cache_pass_;
    world.gi_scene = &engine::context().get_cached<surface_cache_system>();
    world.radiance_cache = gi_gather_pass_.get_radiance_cache();
    world.pre_exposure = pre_exposure;
    world.settings = gi_visualize_settings_;
    gi_visualize_pass_.draw_world(world);
}

void deferred::release_gi_visualize_card_generation()
{
    if(gi_visualize_settings_.is_card_generation())
    {
        return;
    }
    gi_visualize_pass_.release_card_generation(engine::context().get_cached<surface_cache_system>());
}

void deferred::run_gi_scene_passes(scene& scn, const camera& camera, gfx::render_view& rview, const run_params& params)
{
    // The GI scene is world state shared by every camera, refreshed once per camera-driven frame; reflection probe
    // captures skip it. Gated on a gi_component asking for GI: the update rebuilds the instance list of every model and
    // composes clipmap levels, a cost a camera without GI must not pay.
    if(!is_camera_run(params) || !is_gi_requested(params))
    {
        return;
    }
    auto& surface_cache = engine::context().get_cached<surface_cache_system>();
    // World half: identical for every camera, so it self-limits to once per frame.
    surface_cache.update_world(scn);
    // After the update, which decides the frame's scene: a camera of another one renders without GI.
    gi_settings gi;
    if(!resolve_gi_settings(params, gi))
    {
        return;
    }
    // Camera half: the cascade is snapped around THIS viewer, so it belongs to the render
    // view. Two cameras sharing one cascade re-snapped it to each other's position every
    // frame and it never settled.
    auto& view_cache = rview.data().get_or_emplace<surface_cache_view>(surface_cache_view::view_key);
    const bool freeze_origin =
        (surface_cache.get_experiment_flags() & ANONYMOUS::gi_experiment_freeze_clipmap_origin) != 0u;
    if(!freeze_origin || !has_frozen_clipmap_camera_)
    {
        clipmap_camera_ = camera.get_position();
        has_frozen_clipmap_camera_ = freeze_origin;
    }
    view_cache.set_march_experiments(gi_pass::get_sdf_march_experiments(surface_cache.get_experiment_flags()));
    // The GPU composes the voxels when its program loaded; otherwise the CPU does, so the cascade is never left empty.
    view_cache.update(surface_cache.get_clipmap_instances(),
                      clipmap_camera_,
                      ANONYMOUS::make_gi_clipmap_settings(gi,
                                                          gi_clipmap_compose_pass_.begin(),
                                                          surface_cache.get_experiment_flags()),
                      surface_cache.get_content_revision());
    // The GPU composes the levels the update above marked dirty; without the program the CPU
    // composer already wrote and uploaded them.
    if(gi_clipmap_compose_pass_.begin())
    {
        gi_clipmap_compose_pass::run_params compose_params;
        compose_params.surface_cache = &surface_cache;
        compose_params.view_cache = &view_cache;
        gi_clipmap_compose_pass_.run(rview, compose_params);
    }
}

auto deferred::gi_short_range_ao_owns_view(const run_params& rparams) -> bool
{
    if(!reflection_screen_stack_enabled(rparams) || !gi_gather_pass_.has_short_range_ao())
    {
        return false;
    }
    // At any intensity: at 0 the gather skips the pass and the view has no screen-space AO at all.
    gi_settings gi;
    return resolve_gi_settings(rparams, gi) && gi.ambient_occlusion.enabled;
}

auto deferred::is_ssr_wanted(const run_params& rparams) -> bool
{
    return reflection_screen_stack_enabled(rparams) && rparams.fill_ssr_params && !gi_reflections_own_view(rparams);
}

auto deferred::gi_reflections_own_view(const run_params& rparams) -> bool
{
    if(!reflection_screen_stack_enabled(rparams) || !wants_hdr_buffers(rparams) || !gi_reflection_pass_.has_programs())
    {
        return false;
    }
    gi_settings gi;
    return resolve_gi_settings(rparams, gi) && gi.reflections.enabled;
}

auto deferred::run_gi_reflection_pass(gfx::render_view& rview, const gi_run_params& gather_params) -> bool
{
    const auto rbuffer = rview.fbo_find("RBUFFER");
    const auto pbuffer = rview.fbo_find("PBUFFER");
    if(!rbuffer || !pbuffer)
    {
        return false;
    }
    gi_reflection_pass::run_params params;
    params.gather = &gather_params;
    params.rough_specular = rview.tex_find("GI_ROUGH_SPECULAR");
    params.rough_specular_scale = get_gi_resolve_scale(rview);
    params.traced_output = rbuffer->get_texture(0);
    params.probe_output = pbuffer->get_texture(0);
    return gi_reflection_pass_.run(rview, params);
}

auto deferred::wants_gi_reflection_layer(const run_params& rparams) -> bool
{
    const auto& ctx = engine::context();
    const bool is_on = !ctx.has<settings>() || ctx.get<settings>().global_illumination.translucent_reflections;
    return is_on && is_camera_run(rparams) && gi_reflection_pass_.has_front_programs();
}

void deferred::get_screen_rect(const camera& camera,
                               const math::bbox& bounds,
                               const usize32_t& size,
                               math::ivec2& min,
                               math::ivec2& max)
{
    const math::ivec2 view_size(int32_t(size.width), int32_t(size.height));
    min = view_size;
    max = math::ivec2(0);
    const math::mat4 view_proj = camera.get_view_projection().get_matrix();
    const bool is_origin_bottom_left = bgfx::getCaps()->originBottomLeft;
    for(uint32_t corner = 0; corner < 8; ++corner)
    {
        const math::vec3 point((corner & 1u) != 0 ? bounds.max.x : bounds.min.x,
                               (corner & 2u) != 0 ? bounds.max.y : bounds.min.y,
                               (corner & 4u) != 0 ? bounds.max.z : bounds.min.z);
        const math::vec4 clip = view_proj * math::vec4(point, 1.0f);
        if(clip.w <= 1e-4f)
        {
            // Behind the camera: the box's projection is unbounded.
            min = math::ivec2(0);
            max = view_size;
            return;
        }
        const math::vec2 ndc = math::vec2(clip) / clip.w;
        const float u = ndc.x * 0.5f + 0.5f;
        const float v = is_origin_bottom_left ? ndc.y * 0.5f + 0.5f : 0.5f - ndc.y * 0.5f;
        const math::ivec2 pixel(int32_t(math::floor(u * float(size.width))), int32_t(math::floor(v * float(size.height))));
        min = math::min(min, pixel);
        max = math::max(max, pixel + 1);
    }
    min = math::clamp(min, math::ivec2(0), view_size);
    max = math::clamp(max, math::ivec2(0), view_size);
}

void deferred::release_gi_reflection_layer(gfx::render_view& rview)
{
    if(!rview.tex_find("GI_FRONT_NORMAL"))
    {
        return;
    }
    for(const char* name : {"GI_FRONT_LAYER_A", "GI_FRONT_LAYER_B"})
    {
        rview.fbo_remove(name);
    }
    for(const char* name : {"GI_FRONT_NORMAL", "GI_FRONT_DEPTH_A", "GI_FRONT_DEPTH_B", "GI_FRONT_REFLECTION"})
    {
        rview.tex_remove(name);
    }
    rview.data().remove("GI_FRONT_LAYER_STATE");
    rview.data().remove("GI_FRONT_LAYER_SIZE");
    gi_reflection_pass::release_front_layer(rview);
}

auto deferred::collect_gi_reflection_layer(const camera& camera, const usize32_t& size, math::ivec2& region_min, math::ivec2& region_max)
    -> bool
{
    region_min = math::ivec2(int32_t(size.width), int32_t(size.height));
    region_max = math::ivec2(0);
    auto& layer_models = gi_reflection_layer_models_;
    layer_models.clear();
    for(size_t i = 0; i < translucent_models_.size(); ++i)
    {
        // Only a model that draws its "gi" variants reads the traces (draws_with_gi): its radiance cache points were
        // marked.
        const bool is_marked = i < translucent_gi_marked_.size() && translucent_gi_marked_[i] != 0;
        if(!translucent_reads_[i].has_gi_reflections || !is_marked)
        {
            continue;
        }
        math::ivec2 low;
        math::ivec2 high;
        get_screen_rect(camera, translucent_models_[i].entity.get<model_component>().get_world_bounds(), size, low, high);
        const math::ivec2 extent = math::max(high - low, math::ivec2(0));
        if(int64_t(extent.x) * int64_t(extent.y) < gi_reflection_layer_min_pixels)
        {
            continue;
        }
        region_min = math::min(region_min, low);
        region_max = math::max(region_max, high);
        layer_models.push_back(i);
    }
    return !layer_models.empty();
}

auto deferred::acquire_gi_reflection_layer(gfx::render_view& rview, const usize32_t& size) -> gi_reflection_layer_targets
{
    // The layer ping-pongs its depth: the temporal validates its history against last frame's.
    auto& state = rview.data().get_or_emplace<gi_pass::ping_pong_state>("GI_FRONT_LAYER_STATE");
    const auto step = state.advance(gfx::get_render_frame());
    const char* write_set = step.writes_even ? "A" : "B";
    const char* read_set = step.writes_even ? "B" : "A";
    gi_reflection_layer_targets targets;
    targets.normal = rview.tex_acquire("GI_FRONT_NORMAL", {.size = size, .format = bgfx::TextureFormat::RGBA16F, .flags = BGFX_TEXTURE_RT});
    targets.depth = rview.tex_acquire(std::string("GI_FRONT_DEPTH_") + write_set,
                                      {.size = size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_RT});
    targets.prev_depth = step.is_continuous ? rview.tex_find(std::string("GI_FRONT_DEPTH_") + read_set) : nullptr;
    auto& fbo = rview.fbo_acquire(std::string("GI_FRONT_LAYER_") + write_set);
    if(gfx::needs_recreate(fbo, {targets.normal, targets.depth}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({targets.normal, targets.depth});
    }
    targets.fbo = fbo;
    return targets;
}

auto deferred::get_region_projection(const math::mat4& projection,
                                     const usize32_t& view_size,
                                     const gi_reflection_pass::dispatch_region& region) -> math::mat4
{
    // A view pixel p lands on texel p - origin of a viewport of the region's size: the clip space scales by view / region
    // and shifts by the origin, measured from the bottom row where the origin is at the bottom left.
    const math::vec2 view(float(view_size.width), float(view_size.height));
    const math::vec2 size(float(region.pixel_size.x), float(region.pixel_size.y));
    const math::vec2 origin(float(region.pixel_origin.x), float(region.pixel_origin.y));
    const math::vec2 shift = (view - 2.0f * origin) / size;
    math::mat4 to_region(1.0f);
    to_region[0][0] = view.x / size.x;
    to_region[1][1] = view.y / size.y;
    to_region[3][0] = shift.x - 1.0f;
    to_region[3][1] = bgfx::getCaps()->originBottomLeft ? shift.y - 1.0f : 1.0f - shift.y;
    return to_region * projection;
}

auto deferred::draw_gi_reflection_layer(const camera& camera,
                                        const gi_reflection_layer_targets& targets,
                                        const gi_reflection_pass::dispatch_region& region,
                                        const usize32_t& view_size,
                                        const gfx::texture::ptr& opaque_depth) -> bool
{
    bool drew = false;
    gfx::render_pass pass("Translucency/GI Reflection Layer");
    pass.bind(targets.fbo.get());
    // The targets cover the region from their first texel.
    pass.set_view_rect(0, 0, uint16_t(region.pixel_size.x), uint16_t(region.pixel_size.y));
    const math::mat4 projection = get_region_projection(camera.get_projection().get_matrix(), view_size, region);
    pass.set_view_proj(camera.get_view(), &projection[0][0]);
    // Roughness 1 traces nothing where no such surface is; depth 1 reads as the sky.
    pass.clear(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    const math::vec4 layer_target(float(region.pixel_origin.x),
                                  float(region.pixel_origin.y),
                                  1.0f / float(view_size.width),
                                  1.0f / float(view_size.height));
    for(const size_t index : gi_reflection_layer_models_)
    {
        drew = draw_gi_reflection_layer_model(pass.id, index, *current_run_, opaque_depth, layer_target) || drew;
    }
    return drew;
}

void deferred::trace_gi_reflection_layer(gfx::render_view& rview,
                                         const gi_run_params& gather_params,
                                         const gi_reflection_layer_targets& targets,
                                         const gi_reflection_pass::dispatch_region& region,
                                         const usize32_t& target_size)
{
    gi_reflection_pass::front_layer_params params;
    params.gather = &gather_params;
    params.depth = targets.depth;
    params.normal = targets.normal;
    params.prev_depth = targets.prev_depth;
    params.output = gi_pass::ensure_texture(rview, "GI_FRONT_REFLECTION", target_size, bgfx::TextureFormat::RGBA16F);
    params.region = region;
    params.target_size = target_size;
    if(gi_reflection_pass_.run_front_layer(rview, params))
    {
        gi_front_reflection_ = params.output;
        gi_front_max_roughness_ = gi_reflection_pass::get_max_roughness_to_trace(gather_params.settings.reflections);
    }
}

auto deferred::run_gi_reflection_layer(const camera& camera, gfx::render_view& rview, const gi_run_params& gather_params) -> bool
{
    if(translucent_models_.empty() || current_run_ == nullptr || !gather_params.g_buffer)
    {
        return false;
    }
    const usize32_t size = gather_params.g_buffer->get_size();
    math::ivec2 region_min;
    math::ivec2 region_max;
    if(!collect_gi_reflection_layer(camera, size, region_min, region_max))
    {
        return false;
    }
    APP_SCOPE_PERF("Rendering/GI/Front Layer");
    // Every front target covers the region the passes run over, sized to follow it without a new texture every frame.
    const auto region = gi_reflection_pass_.get_front_region(region_min, region_max);
    if(region.pixel_size.x <= 0 || region.pixel_size.y <= 0)
    {
        return false;
    }
    auto& sizing = rview.data().get_or_emplace<gi_reflection_pass::front_target_size>("GI_FRONT_LAYER_SIZE");
    const usize32_t target_size = gi_reflection_pass::update_front_target_size(sizing, region.pixel_size, size);
    const auto targets = acquire_gi_reflection_layer(rview, target_size);
    // A surface whose layer variant is still compiling draws nothing yet: the targets stay for the next frame.
    if(draw_gi_reflection_layer(camera, targets, region, size, gather_params.g_buffer->get_texture(4)))
    {
        trace_gi_reflection_layer(rview, gather_params, targets, region, target_size);
    }
    return true;
}

auto deferred::get_gi_resolve_scale(gfx::render_view& rview) -> float
{
    const auto* scale = rview.data().try_get<float>(gi_resolve_scale);
    return scale ? *scale : 1.0f;
}

auto deferred::is_gi_requested(const run_params& rparams) -> bool
{
    // Supported, not enabled: the surface cache makes its resources when a camera first asks for global illumination.
    return rparams.fill_gi_params && engine::context().get_cached<surface_cache_system>().is_supported();
}

auto deferred::resolve_gi_settings(const run_params& rparams, gi_settings& gi) -> bool
{
    // Off unless a gi_component asks for it, the same contract every other pass here follows, and off while
    // the surface cache does not run or describes another scene this frame: then GTAO, ASSAO and SSR keep the AO
    // and reflections GI would own. The settings left in `gi` are meaningless when no gi_component asks for it.
    if(!is_gi_requested(rparams))
    {
        return false;
    }
    rparams.fill_gi_params(gi);
    return engine::context().get_cached<surface_cache_system>().is_world_scene(current_run_->scn);
}

auto deferred::run_gi_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams) -> bool
{
    gi_front_reflection_ = nullptr;
    if(!is_camera_run(rparams))
    {
        return false;
    }
    gfx::texture::ptr result;
    gi_settings gi;
    if(resolve_gi_settings(rparams, gi))
    {
        auto params = make_gi_run_params(camera, rview, gi);
        params.is_being_edited = rparams.is_being_edited;
        const auto& visualize = gi_visualize_settings_;
        params.visualize_traces.enabled = visualize.screen_probe_traces;
        params.visualize_traces.freeze = visualize.screen_probe_traces_freeze;
        params.visualize_traces.cursor = visualize.cursor;
        params.has_traced_reflections = gi_reflections_own_view(rparams);
        run_gi_surface_cache(camera, rview, *params.surface_cache, gi.scene);
        result = gi_gather_pass_.run(rview, params);
        rview.data().get_or_emplace<float>(gi_resolve_scale, 1.0f) = std::max(gi.diffuse.intensity, 0.0f);
        // The GI reflections run after the screen probe gather, whose rough specular they composite under the
        // traced layer.
        if(result && params.has_traced_reflections)
        {
            gi_reflections_written_ = run_gi_reflection_pass(rview, params);
        }
        // Then the front layer of the see-through surfaces with GI Reflections, tracing the same scene; its targets go
        // while no such surface is in view.
        const bool has_layer = gi_reflections_written_ && wants_gi_reflection_layer(rparams) && run_gi_reflection_layer(camera, rview, params);
        if(!has_layer)
        {
            release_gi_reflection_layer(rview);
        }
        // SSR stepped aside for them, so RBUFFER falls back to the traced clear.
        if(params.has_traced_reflections && !gi_reflections_written_)
        {
            clear_traced_reflections(rview);
        }
    }
    else
    {
        // GI is off for this camera: its surface cache, radiance cache and global distance field go (GTAO, ASSAO and
        // SSIL release theirs the same way). They are rebuilt when GI comes back.
        gi_surface_cache_pass_.release_targets();
        gi_gather_pass_.release_resources();
        rview.data().remove(surface_cache_view::view_key);
        release_gi_reflection_layer(rview);
    }
    if(result)
    {
        // The accumulated result ping-pongs between two targets, so it is published under a
        // stable name for the indirect consumer rather than being looked up by its own.
        rview.tex_acquire("GI_RESOLVE") = result;
        return true;
    }
    // The consumer picks GI_RESOLVE over SSIL purely by presence, so a buffer left behind from when
    // the pass last ran would keep overriding SSIL with a frozen image.
    rview.tex_remove("GI_RESOLVE");
    rview.fbo_remove("GI_RESOLVE");
    // The rough specular goes with it: the reflections read neither without the other.
    rview.tex_remove("GI_ROUGH_SPECULAR");
    return false;
}

auto deferred::make_gi_run_params(const camera& camera, gfx::render_view& rview, const gi_settings& gi)
    -> gi_run_params
{
    gi_run_params params;
    params.settings = gi;
    params.g_buffer = rview.fbo_find("GBUFFER");
    // Still the PREVIOUS frame's depth at this point: the snapshot happens later in the
    // frame, which is exactly what temporal reprojection needs to validate history.
    params.prev_depth = rview.tex_find("PREV_DEPTH");
    // This frame's environment SH (the irradiance pass runs before GI), the sky of the rays that leave the scene.
    params.irradiance_sh = rview.tex_find("IRRADIANCE_SH");
    // This frame's Hi-Z pyramid (built earlier in the frame) for the screen traces.
    params.hiz = rview.tex_find("HIZBUFFER");
    // Last frame's post-TAA linear scene color, the radiance of screen trace hits.
    params.prev_color = rview.tex_find("PREV_SCENE_HDR");
    // This frame's velocity buffer while the velocity pass drew movers this frame or the last (the gather history
    // carries their fast update one frame): moving surfaces reproject from where they were. Without movers every
    // surface is static and the GI passes skip the velocity reads; a buffer from an earlier frame would move them by
    // stale motion.
    constexpr uint64_t gi_mover_frames = 1;
    const uint64_t render_frame = gfx::get_render_frame();
    const bool has_recent_movers = velocity_movers_frame_ != ~0ull && render_frame >= velocity_movers_frame_ &&
                                   render_frame - velocity_movers_frame_ <= gi_mover_frames;
    params.velocity = velocity_run_active_ && has_recent_movers ? rview.tex_find("VELOCITY") : nullptr;
    params.cam = &camera;
    // Every GI target, its history included, is in this run's pre-exposed space.
    params.pre_exposure = get_pre_exposure(rview);
    params.surface_cache = &engine::context().get_cached<surface_cache_system>();
    params.view_cache = rview.data().try_get<surface_cache_view>(surface_cache_view::view_key);
    params.gi_surface_cache = &gi_surface_cache_pass_;
    params.surface_cache_feedback = gi_surface_cache_pass_.get_feedback();
    const auto& ctx = engine::context();
    if(ctx.has<settings>())
    {
        params.gi_quality = ctx.get<settings>().global_illumination.global_illumination_quality;
        params.reflection_quality = ctx.get<settings>().global_illumination.reflection_quality;
    }
    params.camera_cut = has_camera_cut(camera);
    params.global_lighting_change = params.surface_cache->has_global_lighting_change();
    params.surface_points = collect_translucent_gi_points(camera.get_position());
    return params;
}

void deferred::run_gi_surface_cache(const camera& camera,
                                    gfx::render_view& rview,
                                    surface_cache_system& gi_scene,
                                    const gi_settings::scene_settings& scene_settings)
{
    APP_SCOPE_PERF("Rendering/Surface Cache");
    const auto& ctx = engine::context();
    const gi_project_settings project_settings =
        ctx.has<settings>() ? ctx.get<settings>().global_illumination : gi_project_settings{};
    const auto* view_cache = rview.data().try_get<surface_cache_view>(surface_cache_view::view_key);
    // The cards' sunlight takes this run's cloud shadow as the deferred directional light does.
    const auto cloud_uniforms = ANONYMOUS::make_cloud_shadow_uniforms(cloud_shadow_);
    gi_surface_cache_pass::cloud_shadow card_cloud_shadow;
    card_cloud_shadow.map = cloud_uniforms.is_applied ? cloud_shadow_.map : nullptr;
    card_cloud_shadow.placement = cloud_uniforms.placement;
    card_cloud_shadow.layer = cloud_uniforms.layer;
    card_cloud_shadow.signature = cloud_uniforms.is_applied ? cloud_shadow_.signature : 0u;
    gi_surface_cache_pass_.set_cloud_shadow(card_cloud_shadow);
    // The surface cache is shared by every camera: the frame's first one updates it and captures the cards; each one
    // keeps its object grid on its own distance field and lights the pages nearest to it.
    const bool is_updating_camera =
        gi_surface_cache_pass_.update(gi_scene,
                                      view_cache != nullptr ? &view_cache->get_clipmap() : nullptr,
                                      camera.get_position(),
                                      camera.get_frustum(),
                                      scene_settings,
                                      project_settings);
    if(view_cache != nullptr)
    {
        gi_surface_cache_pass_.update_object_grid(gi_scene, *view_cache);
    }
    if(is_updating_camera)
    {
        const auto& scene_stats = gi_surface_cache_pass_.get_scene().get_stats();
        stats_.gi_cards = scene_stats.cards;
        stats_.gi_captures = scene_stats.captures;
        stats_.gi_material_changes = scene_stats.material_changes;
        stats_.gi_direct_dirty_pages = scene_stats.direct_dirty_pages;
        stats_.gi_pages_used = scene_stats.pages_used;
        stats_.gi_direct_invalidated = scene_stats.direct_invalidated;
        capture_gi_cards(camera, gi_scene);
        gi_surface_cache_pass_.copy_captures();
    }
    gi_surface_cache_pass::lighting_inputs inputs;
    inputs.gi_scene = &gi_scene;
    inputs.view_cache = view_cache;
    // This frame's environment SH: the irradiance pass runs before GI.
    inputs.environment_sh = rview.tex_find("IRRADIANCE_SH");
    inputs.view_exposure = get_pre_exposure(rview);
    gi_surface_cache_pass_.light(inputs);
}

void deferred::capture_gi_cards(const camera& camera, const surface_cache_system& gi_scene)
{
    const auto& captures = gi_surface_cache_pass_.get_scene().get_captures();
    auto& program = card_capture_program_;
    if(captures.empty() || !program.program || !program.program->is_valid())
    {
        return;
    }
    const auto& sources = gi_scene.get_gi_sources();
    // The camera's winding convention, which every capture's culling is matched against.
    const math::mat4 camera_view_proj = camera.get_projection() * camera.get_view();
    const float camera_orientation = math::determinant(math::mat3(camera_view_proj)) < 0.0f ? -1.0f : 1.0f;
    const math::vec2 clip_planes(camera.get_near_clip(), camera.get_far_clip());
    // No LOD fade: x = 0 never discards.
    const math::vec3 lod_params(0.0f, -1.0f, 1.0f);
    // One pass draws every capture: each draw's clip transform places it in its tile of the capture atlas and its
    // scissor keeps it there. The atlas holds this frame's captures only (the copy reads them next), so one clear of
    // the whole atlas serves them all - of its depth alone: the copy keeps a texel's colours only where the depth says
    // a surface was drawn.
    gfx::render_pass pass("GI/Card Capture");
    pass.bind(gi_surface_cache_pass_.get_capture_target().get());
    pass.clear(BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
    for(const auto& cap : captures)
    {
        const auto& src = sources[cap.source_index];
        // The LOD the cards were built from (LOD 1 unless the asset sets one): the card planes sit on
        // that surface, and it costs a fraction of LOD 0.
        uint32_t capture_lod = src.owner ? src.owner->get_gi_cards_lod() : 0u;
        const auto* submesh = src.owner ? src.owner->get_submesh(src.submesh_index, capture_lod) : nullptr;
        if(submesh == nullptr && src.owner)
        {
            capture_lod = 0;
            submesh = src.owner->get_submesh(src.submesh_index);
        }
        if(submesh == nullptr || !src.material || !src.material->is<graph_material>())
        {
            continue;
        }
        const auto view = gi_surface_cache_pass_.compute_capture_view(cap);
        program.program->begin();
        gfx::set_uniform(program.u_card_capture_view_proj, view.view_proj);
        // The camera sits far in front of the card so the shader's near-camera dither never fires.
        gfx::set_uniform(program.u_camera_wpos, math::vec4(view.far_eye, 0.0f));
        gfx::set_uniform(program.u_camera_clip_planes, clip_planes);
        gfx::set_uniform(program.u_lod_params, lod_params);
        gfx::set_world_transform(&src.local_to_world);
        src.owner->bind_render_buffers_for_submesh(submesh, capture_lod);
        const auto& mat = *src.material;
        const auto program_handle = submit_geometry_material(program, mat);
        // A card basis of the opposite orientation winds triangles the other way: flip culling.
        uint64_t state = mat.get_render_states(true, true, true);
        if(view.orientation != camera_orientation)
        {
            const uint64_t cull = state & BGFX_STATE_CULL_MASK;
            state &= ~BGFX_STATE_CULL_MASK;
            state |= cull == BGFX_STATE_CULL_CW ? BGFX_STATE_CULL_CCW : (cull == BGFX_STATE_CULL_CCW ? BGFX_STATE_CULL_CW : 0);
        }
        bgfx::setState(state);
        bgfx::setScissor(uint16_t(view.scissor.left),
                         uint16_t(view.scissor.top),
                         uint16_t(view.scissor.width()),
                         uint16_t(view.scissor.height()));
        bgfx::submit(pass.id, program_handle);
        program.program->end();
    }
}

static_assert(deferred::debug_pass_gi_reflection_rays - deferred::debug_pass_gi_scene ==
                  int(gi_visualize_pass::view::dedicated_reflection_rays),
              "a GI debug pass id is debug_pass_gi_scene + its gi_visualize_pass::view");
static_assert(deferred::debug_pass_gi_performance_overview - deferred::debug_pass_gi_scene + 1 ==
                  int(gi_visualize_pass::view::count),
              "every gi_visualize_pass::view has a debug pass id");

void deferred::run_gi_visualize_pass(const camera& camera,
                                     gfx::render_view& rview,
                                     const gfx::frame_buffer::ptr& output,
                                     const run_params& rparams)
{
    gi_visualize_pass::run_params params;
    params.mode = gi_visualize_pass::view(debug_pass_ - debug_pass_gi_scene);
    params.output = output;
    params.cam = &camera;
    params.rview = &rview;
    params.surface_cache = &gi_surface_cache_pass_;
    params.gi_scene = &engine::context().get_cached<surface_cache_system>();
    params.view_cache = rview.data().try_get<surface_cache_view>(surface_cache_view::view_key);
    resolve_gi_settings(rparams, params.gi);
    params.exposure = get_pre_exposure(rview);
    params.tonemapping = get_debug_tonemapping(rparams);
    gi_visualize_pass_.run(params, debug_view_labels_);
}

void deferred::run_gi_visualize_overlays(const camera& camera,
                                         gfx::render_view& rview,
                                         const gfx::frame_buffer::ptr& output,
                                         const run_params& rparams)
{
    const auto& gbuffer = rview.fbo_find("GBUFFER");
    gi_visualize_pass::overlay_params params;
    params.output = output;
    params.scene_depth = gbuffer ? gbuffer->get_texture(4) : nullptr;
    params.cam = &camera;
    params.rview = &rview;
    params.gather = &gi_gather_pass_;
    params.print = &shader_print_;
    params.settings = gi_visualize_settings_;
    params.tonemapping = get_debug_tonemapping(rparams);
    // The overlays are the frame's printers: the buffer empties in a view ahead of theirs.
    shader_print_.begin_frame();
    gi_visualize_pass_.draw_overlays(params);
}

} // namespace rendering
} // namespace unravel
