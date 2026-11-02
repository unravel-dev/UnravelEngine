#include "pipeline_internal.h"

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
    if(params.run_type != pipeline_run_type::camera || !is_gi_requested(params))
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

auto deferred::get_gi_resolve_scale(gfx::render_view& rview) -> float
{
    const auto* scale = rview.data().try_get<float>(gi_resolve_scale);
    return scale ? *scale : 1.0f;
}

auto deferred::is_gi_requested(const run_params& rparams) -> bool
{
    return rparams.fill_gi_params && engine::context().get_cached<surface_cache_system>().is_enabled();
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
    return run_scene_ != nullptr && engine::context().get_cached<surface_cache_system>().is_world_scene(*run_scene_);
}

auto deferred::run_gi_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams) -> bool
{
    if(rparams.run_type != pipeline_run_type::camera)
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
        if(submesh == nullptr || !src.material || !src.material->is<pbr_material>())
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
        const auto& pbr = static_cast<const pbr_material&>(*src.material);
        submit_pbr_material(program, pbr);
        // A card basis of the opposite orientation winds triangles the other way: flip culling.
        uint64_t state = pbr.get_render_states(true, true, true);
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
        bgfx::submit(pass.id, program.program->native_handle());
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
