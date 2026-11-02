#include "pipeline_internal.h"

#include <utility>

namespace unravel
{
namespace
{
namespace ANONYMOUS
{
using namespace rendering::deferred_detail;

} // namespace ANONYMOUS
} // namespace

namespace rendering
{

namespace
{
auto get_default_depth_format() -> bgfx::TextureFormat::Enum
{
    return bgfx::TextureFormat::D32F;
}

// run_pipeline_impl takes const camera& for reads; jitter only touches projection jitter state.
void apply_pipeline_taa_jitter_to_camera(const camera& view_camera,
                                         const usize32_t& viewport_size,
                                         const pipeline::run_params& params)
{
    camera& cam = const_cast<camera&>(view_camera);
    if(params.apply_taa_params)
    {
        params.apply_taa_params(cam, viewport_size);
    }
    else
    {
        cam.set_aa_data(viewport_size, 0u, 1u);
    }
    // With the frame's jitter final, record this frame's matrices; last frame's recording
    // becomes the camera's get_prev_* set (frame-stamped - a second run of the same camera
    // in one frame is a no-op). This is the ONE place previous matrices are maintained.
    cam.record_current_matrices();
}

auto create_or_resize_d_buffer(gfx::render_view& rview,
                               const usize32_t& render_size,
                               const pipeline::run_params& params) -> const gfx::texture::ptr&
{
    return rview.tex_acquire("DEPTH",
                             {.size = render_size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_RT});
}

auto create_or_resize_g_buffer(gfx::render_view& rview,
                               const usize32_t& render_size,
                               const pipeline::run_params& params) -> const gfx::frame_buffer::ptr&
{
    const auto& depth = create_or_resize_d_buffer(rview, render_size, params);
    const auto format = wants_hdr_buffers(params) ? get_default_hdr_format() : get_default_format();
    // sRGB: the base colour is stored encoded and every reader gets it decoded by the texture unit (EncodeGBuffer).
    const auto& tex0 = rview.tex_acquire(
        "GBUFFER_0",
        {.size = render_size, .format = get_default_format(), .flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_SRGB});
    const auto& tex1 = rview.tex_acquire("GBUFFER_1", {.size = render_size, .format = format, .flags = BGFX_TEXTURE_RT});
    const auto& tex2 =
        rview.tex_acquire("GBUFFER_2", {.size = render_size, .format = get_light_target_format(params), .flags = BGFX_TEXTURE_RT});
    const auto& tex3 = rview.tex_acquire(
        "GBUFFER_3",
        {.size = render_size, .format = get_default_format(), .flags = BGFX_TEXTURE_RT});

    auto& fbo = rview.fbo_acquire("GBUFFER");
    if(gfx::needs_recreate(fbo, {tex0, tex1, tex2, tex3, depth}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex0, tex1, tex2, tex3, depth});
    }

    if(params.limits.subsurface)
    {
        rview.fbo_remove("GBUFFER_NO_SUBSURFACE");
        return fbo;
    }
    // Without subsurface the G-buffer pass leaves the fourth target out: one less to write per pixel. Whatever samples
    // it still reads black, so it is cleared once, when it is new.
    auto& draw_fbo = rview.fbo_acquire("GBUFFER_NO_SUBSURFACE");
    if(gfx::needs_recreate(draw_fbo, {tex0, tex1, tex2, depth}))
    {
        draw_fbo.reset();
        draw_fbo = std::make_shared<gfx::frame_buffer>();
        draw_fbo->populate({tex0, tex1, tex2, depth});
        auto subsurface_fbo = std::make_shared<gfx::frame_buffer>();
        subsurface_fbo->populate({tex3});
        gfx::render_pass clear_pass("G-Buffer/Clear Subsurface");
        clear_pass.bind(subsurface_fbo.get());
        clear_pass.clear(BGFX_CLEAR_COLOR, 0, 1.0f, 0);
    }

    return fbo;
}

auto create_or_resize_l_buffer(gfx::render_view& rview,
                               const usize32_t& render_size,
                               const pipeline::run_params& params) -> const gfx::frame_buffer::ptr&
{
    const auto& depth = create_or_resize_d_buffer(rview, render_size, params);
    const auto& tex = rview.tex_acquire("LBUFFER",
                                        {.size = render_size, .format = get_light_target_format(params), .flags = BGFX_TEXTURE_RT});

    auto& fbo_depth = rview.fbo_acquire("LBUFFER_DEPTH");
    if(gfx::needs_recreate(fbo_depth, {tex, depth}))
    {
        fbo_depth.reset();
        fbo_depth = std::make_shared<gfx::frame_buffer>();
        fbo_depth->populate({tex, depth});
    }

    auto& fbo = rview.fbo_acquire("LBUFFER");
    if(gfx::needs_recreate(fbo, {tex}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

/// A reflection target at the render size, rebuilt when its size, format or flags change.
auto create_or_resize_reflection_buffer(gfx::render_view& rview,
                                        const std::string& name,
                                        const usize32_t& render_size,
                                        const pipeline::run_params& params,
                                        uint64_t texture_flags) -> const gfx::frame_buffer::ptr&
{
    const auto format = wants_hdr_buffers(params) ? get_default_hdr_format() : get_default_format();
    const auto& tex = rview.tex_acquire(name, {.size = render_size, .format = format, .flags = texture_flags});

    auto& fbo = rview.fbo_acquire(name);
    if(gfx::needs_recreate(fbo, {tex}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

/// The reflection buffers. RBUFFER holds the traced layers (GI reflections, then SSR),
/// premultiplied in rgb, with the share they leave uncovered in alpha; PBUFFER holds the
/// untraced layer (reflection probes, sky, the GI rough tier). The indirect pass occludes the
/// two differently (ComposeIndirectSpecular in lighting.sh). The GI reflections write both
/// from compute.
void create_or_resize_reflection_buffers(gfx::render_view& rview,
                                         const usize32_t& render_size,
                                         const pipeline::run_params& params)
{
    create_or_resize_reflection_buffer(rview, "RBUFFER", render_size, params, BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE);
    create_or_resize_reflection_buffer(rview, "PBUFFER", render_size, params, BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE);
}
/// The size a run renders its scene at: a camera run's render resolution (its viewport's size unless set) at its
/// render scale (get_view_render_size); every other run renders at its viewport's size.
auto get_run_render_size(const usize32_t& viewport_size, const pipeline::run_params& params) -> usize32_t
{
    return is_camera_run(params) ? get_view_render_size(viewport_size, params.render_resolution, params.render_scale) : viewport_size;
}

/// The output's color at the viewport's size; acquire_scene_output attaches the scene's depth to it.
auto create_or_resize_o_buffer(gfx::render_view& rview, const usize32_t& viewport_size) -> const gfx::frame_buffer::ptr&
{
    const auto& tex = rview.tex_acquire("OBUFFER",
                                        {.size = viewport_size,
                                         .format = get_default_format(),
                                         .flags = BGFX_TEXTURE_RT});

    auto& fbo = rview.fbo_acquire("OBUFFER");
    if(gfx::needs_recreate(fbo, {tex}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

/// Where a camera run finishes its scene: the passes after the post-processing chain draw into it.
struct scene_output
{
    /// The finished scene's color.
    gfx::frame_buffer::ptr color;
    /// The same color with the scene's depth attached.
    gfx::frame_buffer::ptr color_depth;
    /// A scaled run's output with a depth of its own at the output's size (OBUFFER_DEPTH), which the upscale
    /// fills with the scene's depth; null unless the run asks for it (run_params::needs_output_depth).
    gfx::frame_buffer::ptr output_depth;
};

/// The depth a scaled run gives its output, at the output's size, for the overlays drawn over it.
auto acquire_output_depth(gfx::render_view& rview, const gfx::frame_buffer::ptr& output, const usize32_t& viewport_size)
    -> const gfx::frame_buffer::ptr&
{
    const auto& depth = rview.tex_acquire("OUTPUT_DEPTH",
                                          {.size = viewport_size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_RT});
    auto& output_depth = rview.fbo_acquire("OBUFFER_DEPTH");
    if(gfx::needs_recreate(output_depth, {output->get_texture(), depth}))
    {
        output_depth.reset();
        output_depth = std::make_shared<gfx::frame_buffer>();
        output_depth->populate({output->get_texture(), depth});
    }
    return output_depth;
}

/**
 * @brief The scene output of a camera run rendering at `render_size` into `output`.
 *
 * At the output's size the run finishes in the output itself. A scaled run finishes in SCALED_OBUFFER, at the
 * render size like the rest of its targets, which the pipeline resamples into the output last; its targets are
 * released while the run is not scaled.
 * @param needs_output_depth A scaled run also gives the output a depth at its size (run_params::needs_output_depth).
 */
auto acquire_scene_output(gfx::render_view& rview,
                          const gfx::frame_buffer::ptr& output,
                          const usize32_t& viewport_size,
                          const usize32_t& render_size,
                          bool needs_output_depth) -> scene_output
{
    const auto& depth = rview.tex_get("DEPTH");
    if(render_size == viewport_size)
    {
        rview.fbo_remove("SCALED_OBUFFER_DEPTH");
        rview.fbo_remove("SCALED_OBUFFER");
        rview.tex_remove("SCALED_OBUFFER");
        rview.tex_remove("OUTPUT_DEPTH");
        auto& output_depth = rview.fbo_acquire("OBUFFER_DEPTH");
        if(gfx::needs_recreate(output_depth, {output->get_texture(), depth}))
        {
            output_depth.reset();
            output_depth = std::make_shared<gfx::frame_buffer>();
            output_depth->populate({output->get_texture(), depth});
        }
        return {output, output_depth};
    }
    // Clamped: the upscale filters across the edge texels.
    const auto& tex = rview.tex_acquire(
        "SCALED_OBUFFER",
        {.size = render_size, .format = get_default_format(), .flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP});
    auto& color = rview.fbo_acquire("SCALED_OBUFFER");
    if(gfx::needs_recreate(color, {tex}))
    {
        color.reset();
        color = std::make_shared<gfx::frame_buffer>();
        color->populate({tex});
    }
    auto& color_depth = rview.fbo_acquire("SCALED_OBUFFER_DEPTH");
    if(gfx::needs_recreate(color_depth, {tex, depth}))
    {
        color_depth.reset();
        color_depth = std::make_shared<gfx::frame_buffer>();
        color_depth->populate({tex, depth});
    }
    if(needs_output_depth)
    {
        return {color, color_depth, acquire_output_depth(rview, output, viewport_size)};
    }
    // The scene's DEPTH is at the render size: the output has no depth.
    rview.fbo_remove("OBUFFER_DEPTH");
    rview.tex_remove("OUTPUT_DEPTH");
    return {color, color_depth, {}};
}

/// The targets a run's passes share, at the size it renders at.
void create_or_resize_render_targets(gfx::render_view& rview,
                                     const usize32_t& render_size,
                                     const pipeline::run_params& params)
{
    create_or_resize_d_buffer(rview, render_size, params);
    create_or_resize_g_buffer(rview, render_size, params);
    create_or_resize_l_buffer(rview, render_size, params);
    create_or_resize_reflection_buffers(rview, render_size, params);
}

/// What resamples a scaled run's finished scene into its output.
auto make_upscale_params(const gfx::render_view& rview, const scene_output& scene, const gfx::frame_buffer::ptr& output)
    -> upscale_pass::run_params
{
    upscale_pass::run_params upscale;
    upscale.input = scene.color;
    upscale.output = output;
    upscale.output_depth = scene.output_depth;
    upscale.input_depth = rview.tex_find("DEPTH");
    return upscale;
}

/// Whether a camera run's next frame reads its scene history (PREV_SCENE_HDR): SSR's trace and the GI's far field do.
auto has_scene_history_reader(const pipeline::run_params& params) -> bool
{
    const bool has_ssr = reflection_screen_stack_enabled(params) && params.fill_ssr_params;
    return has_ssr || params.fill_gi_params;
}

/**
 * @brief Whether a camera run's next frame reads its depth (PREV_DEPTH), as the resources it finished with tell.
 *
 * The passes that march Hi-Z read it. So do the GI and GTAO, whose temporal accumulations reproject against it and treat
 * a missing one as "no history", and TAA, whose disocclusion test compares the reprojected depth against it.
 */
auto has_prev_depth_reader(const gfx::render_view& rview, const pipeline::run_params& params) -> bool
{
    return rview.tex_find("HIZBUFFER") || rview.tex_find("GI_RESOLVE") || rview.tex_find("GTAO") || params.fill_taa_params;
}
} // namespace

auto deferred::run_pipeline(scene& scn,
                            const camera& camera,
                            gfx::render_view& rview,
                            delta_t dt,
                            const run_params& params,
                            layer_mask render_mask) -> gfx::frame_buffer::ptr
{
    const auto& obuffer = create_or_resize_o_buffer(rview, camera.get_viewport_size());

    run_pipeline_impl(obuffer, scn, camera, rview, dt, params, render_mask);

    return obuffer;
}

void deferred::run_pipeline(const gfx::frame_buffer::ptr& output,
                            scene& scn,
                            const camera& camera,
                            gfx::render_view& rview,
                            delta_t dt,
                            const run_params& params,
                            layer_mask render_mask)
{
    // A scaled view upscales straight into the caller's target: going through OBUFFER would cost one more pass at
    // the output's size, on the devices the scale is for.
    const auto& viewport_size = camera.get_viewport_size();
    if(get_run_render_size(viewport_size, params) != viewport_size)
    {
        run_pipeline_impl(output, scn, camera, rview, dt, params, render_mask);
        return;
    }
    auto obuffer = run_pipeline(scn, camera, rview, dt, params, render_mask);

    blit_to_output(rview, obuffer, output);
}

void deferred::set_debug_pass(int pass)
{
    debug_pass_ = pass;
    is_particle_overdraw_view_ = pass == debug_pass_particle_overdraw;
}

void deferred::set_debug_view_scale(float scale)
{
    debug_view_scale_ = scale > 0.0f ? scale : 1.0f;
}

/// Makes a run the current one for as long as it lasts. A probe capture is a run nested in the camera's, so the
/// camera's becomes current again when the capture ends.
struct deferred::run_scope
{
    run_scope(deferred& owner, const run_context& run)
        : pipeline(owner)
        , outer_run(std::exchange(owner.current_run_, &run))
    {
        pipeline.camera_cut_ = hpp::nullopt;
    }
    ~run_scope()
    {
        pipeline.current_run_ = outer_run;
        pipeline.camera_cut_ = hpp::nullopt;
    }
    run_scope(const run_scope&) = delete;
    auto operator=(const run_scope&) -> run_scope& = delete;

    deferred& pipeline;
    const run_context* outer_run;
};

/// The scene a run has lit, which its last steps read.
struct deferred::lit_scene
{
    gfx::frame_buffer::ptr color;
    /// The particles' TAA reactive coverage (run_particle_pass), or null.
    gfx::texture::ptr particle_reactive;
};

void deferred::run_pipeline_impl(const gfx::frame_buffer::ptr& output,
                                 scene& scn,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 delta_t dt,
                                 const run_params& params,
                                 layer_mask render_mask)
{
    APP_SCOPE_PERF("Rendering/Run Pipeline");
    const usize32_t render_size = get_run_render_size(camera.get_viewport_size(), params);
    const run_context run{output, scn, camera, rview, dt, params, render_mask, render_size};
    const run_scope scope(*this, run);
    begin_camera_frame(run);
    capture_reflection_probes(run);
    prepare_passes(run);
    render_geometry(run);
    render_reflection_layers(run);
    const gfx::frame_buffer::ptr lighting = render_lighting(run);
    const lit_scene lit = render_sky_fog_and_particles(run, lighting);
    if(run.is_camera_run())
    {
        finish_camera_image(run, lit);
    }
    else
    {
        blit_to_output(rview, lit.color, output);
    }
    finish_run_batches();
}

void deferred::begin_camera_frame(const run_context& run)
{
    if(!run.is_camera_run())
    {
        return;
    }
    stats_ = {};
    // Before the probe captures: they are the frame's first views of the local lights, whose shadow maps every later
    // view reuses, so they render them with the camera's instanced casters too.
    gather_instance_sources(run.scn, run.cam, run.render_mask);
}

void deferred::capture_reflection_probes(const run_context& run)
{
    if(run.runs_probe_step())
    {
        build_reflections(run.scn, run.cam, run.dt, run.params.limits);
    }
}

void deferred::prepare_passes(const run_context& run)
{
    if(run.runs_shadow_step())
    {
        build_shadows(run.scn, run.cam, run.dt, run.params.limits, run.render_mask);
    }
    // Before any pass writes or reads scene lighting (the nested probe captures rendered with their own unit
    // pre-exposure). Stored in the render view; the passes read it back.
    update_pre_exposure(run.rview, run.params);
    // The GI scene: instance residency and this view's global distance field (gating rationale at the definition).
    run_gi_scene_passes(run.scn, run.cam, run.rview, run.params);
    create_or_resize_render_targets(run.rview, run.render_size, run.params);
    apply_pipeline_taa_jitter_to_camera(run.cam, run.render_size, run.params);
}

void deferred::render_geometry(const run_context& run)
{
    begin_velocity_recording(run.params);
    const auto visibility_set = collect_visible_models(run.scn, run.cam, run.params, run.render_mask, run.dt);
    run_g_buffer_pass(visibility_set, run.cam, run.rview, run.params.limits, run.dt);
    run_velocity_pass(visibility_set, run.cam, run.rview, run.render_size);
    run_screen_ao_pass(run.cam, run.rview, run.dt, run.params);
}

void deferred::begin_velocity_recording(const run_params& params)
{
    // The velocity buffer is two passes at the render size, so a run without a reader skips it. Consumers receive the
    // texture EXPLICITLY through their run params (this pipeline is the only place that fetches "VELOCITY" from the
    // render view); a valid texture IS their enable - null falls back to their matrix reprojection. Also excludes movers
    // from static-mesh batching so their G-buffer raster matches the velocity pass for the EQUAL depth test.
    velocity_run_active_ = is_velocity_wanted(params);
    if(velocity_run_active_)
    {
        model_component::request_velocity_recording(gfx::get_render_frame());
    }
}

void deferred::render_reflection_layers(const run_context& run)
{
    begin_traced_reflections(run.rview, run.params);
    run_reflection_probe_pass(run.scn,
                              run.cam,
                              run.rview,
                              run.runs_probe_step(),
                              gi_reflections_own_view(run.params),
                              run.dt);
    run_hiz_pass(run.rview, run.params, run.render_size);
    // SSR samples last frame's PREV_SCENE_HDR snapshot (post-TAA, scene-referred linear).
    // It must NOT sample the final OBUFFER: that image is tonemapped, sRGB-encoded and has
    // UI composited on it -- display-referred values injected into linear lighting, which
    // auto exposure then meters and re-amplifies (runaway brightening in dark scenes).
    run_ssr_pass(run.cam, run.rview, run.params);
}

void deferred::begin_traced_reflections(gfx::render_view& rview, const run_params& params)
{
    gi_reflections_written_ = false;
    traced_reflections_cleared_ = false;
    // SSR composites into RBUFFER from the traced clear. The GI reflections write every pixel of it
    // when they run, and without a traced layer the indirect pass reads the clear value from a
    // constant texture (get_traced_reflection_layer), so only SSR needs the clear.
    if(is_ssr_wanted(params))
    {
        clear_traced_reflections(rview);
    }
}

auto deferred::render_lighting(const run_context& run) -> gfx::frame_buffer::ptr
{
    const feature_limits& limits = run.params.limits;
    // Cloud shadow map before any lighting: the directional light and the irradiance bake read it.
    run_cloud_shadow_pass(run.scn, run.cam, run.rview, limits);
    // The sky's irradiance SH, before GI: the radiance cache, the screen probes and the card radiosity read this
    // frame's sky on their misses (a view's first frame included), and the indirect pass composites it.
    const irradiance_pass_result irradiance = run_irradiance_pass(run.scn, run.rview, limits);
    // Direct lighting starts the current frame LBUFFER after SSR has consumed its history source.
    run_direct_lighting_pass(run.scn, run.cam, run.rview, run.runs_shadow_step(), limits, run.dt);
    // GI: the surface cache, the screen probe gather and the reflections. Runs after direct lighting, so this
    // frame's light buffer is populated, and before the indirect pass, which composites the results.
    const bool gi_active = run_gi_pass(run.cam, run.rview, run.params);
    run_ssil_pass(run.cam, run.rview, run.params, gi_active);
    // Indirect lighting after SSIL so it can use the result.
    return run_indirect_lighting_pass(run.cam, run.rview, irradiance, run.runs_probe_step(), limits, run.dt);
}

auto deferred::render_sky_fog_and_particles(const run_context& run, const gfx::frame_buffer::ptr& color) -> lit_scene
{
    if(run.runs_sky_step())
    {
        run_atmospherics_pass(color, run.scn, run.cam, run.rview, run.params.limits);
    }
    // Over the sky and the clouds; the particles drawn next fog themselves with the same fog.
    const fog_pass::shader_params fog = run_fog_pass(run.scn, run.cam, run.rview, color, run.params, run.runs_shadow_step());
    gfx::texture::ptr particle_reactive = render_particles(run, color, fog);
    if(run.is_camera_run())
    {
        run_gi_visualize_scene_color(run.cam, run.rview, get_pre_exposure(run.rview));
        release_gi_visualize_card_generation();
    }
    return {color, std::move(particle_reactive)};
}

auto deferred::render_particles(const run_context& run,
                                const gfx::frame_buffer::ptr& target,
                                const fog_pass::shader_params& fog) -> gfx::texture::ptr
{
    if(!run.runs_particle_step())
    {
        return {};
    }
    const gfx::texture::ptr hiz_depth = run.rview.tex_find("HIZBUFFER");
    const bool wants_reactive = static_cast<bool>(run.params.fill_taa_params);
    return run_particle_pass(run.scn,
                             run.cam,
                             run.rview,
                             target,
                             get_pre_exposure(run.rview),
                             hiz_depth,
                             run.runs_shadow_step(),
                             wants_reactive,
                             run.dt,
                             fog,
                             run.params.limits,
                             run.render_mask);
}

void deferred::finish_camera_image(const run_context& run, const lit_scene& lit)
{
    const scene_output scene = acquire_scene_output(run.rview,
                                                    run.output,
                                                    run.cam.get_viewport_size(),
                                                    run.render_size,
                                                    run.params.needs_output_depth);
    const bool is_upscaled = scene.color != run.output;
    render_post_processing(run, lit, scene.color, is_upscaled);
    render_overlays(run, scene.color, scene.color_depth);
    if(is_upscaled)
    {
        run_upscale_pass(make_upscale_params(run.rview, scene, run.output), run.params);
    }
    // After every pass that samples PREV_DEPTH.
    snapshot_prev_depth(run.rview, run.render_size, has_prev_depth_reader(run.rview, run.params));
}

void deferred::render_post_processing(const run_context& run,
                                      const lit_scene& lit,
                                      const gfx::frame_buffer::ptr& output,
                                      bool is_upscaled)
{
    const gfx::frame_buffer::ptr resolved = run_taa_pass(run.cam, run.rview, lit.color, run.params, lit.particle_reactive);
    // Scene-referred history for next frame's SSR trace and GI far-field. Taken after TAA
    // (temporally stable) and before bloom/tonemap/UI (still linear HDR, no display encode,
    // no interface pixels). Only kept while a consumer exists.
    snapshot_prev_scene_color(run.rview, resolved, run.cam, has_scene_history_reader(run.params));
    run_auto_exposure_pass(run.rview, run.cam, resolved, run.params, run.dt);
    // The tonemapper adds the bloom on top of the scene the exposure pass metered, which local exposure measures.
    const bloom_pass::composite bloom = run_bloom_pass(run.rview, resolved, run.params);
    const gfx::frame_buffer::ptr tonemapped = run_tonemapping_pass(run.rview, resolved, bloom, output, run.params, is_upscaled);
    run_fxaa_pass(run.rview, tonemapped, output, run.params, is_upscaled);
}

void deferred::render_overlays(const run_context& run,
                               const gfx::frame_buffer::ptr& color,
                               const gfx::frame_buffer::ptr& color_depth)
{
    run_ui_pass(run.scn, run.cam, run.rview, color_depth);
    run_debug_passes(run.cam, run.rview, color, run.params);
    // Whatever shaders printed this frame (nothing to draw unless one did).
    shader_print_.draw(color);
    // The temporal-stability instrument measures the finished image (the lit frame or the active
    // debug view, before the editor draws its overlays) and must read LAST frame's depth, so it
    // runs before the depth snapshot.
    run_temporal_probe_pass(run.cam, run.rview, color);
}

void deferred::finish_run_batches()
{
    batch_collector_.clear();
    skinned_batch_collector_.clear();
    source_runs_.clear();
    // The palettes this run acquired are packed now, while their models certainly hold them: the editor may change or
    // delete entities between this run and the frame's upload.
    engine::context().get_cached<renderer>().get_skinning_palettes().pack_pending();
}

deferred::deferred()
{
    APP_SCOPE_PERF("Rendering/Pipeline Init");
    init(engine::context());
}

deferred::~deferred()
{
    deinit(engine::context());
}

auto deferred::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    // Before any program, this pipeline's and its passes' alike (pipeline::init runs last).
    pre_exposure_sampler_ = std::make_shared<gfx::uniform>("s_pre_exposure", bgfx::UniformType::Sampler);

    constexpr auto default_variant = gfx::shader::DEFAULT_VARIANT;

    auto load_program = [&](const std::string& vs, const std::string& vs_variant, const std::string& fs, const std::string& fs_variant)
    {
        auto vs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/" + vs + ".eshader");
        auto fs_shadfer = am.get_asset<gfx::shader>("engine:/data/shaders/" + fs + ".eshader");

        return std::make_unique<gpu_program>(vs_shader, vs_variant, fs_shadfer, fs_variant);
    };

    const auto load_geom_program = [&](const std::string& vs_variant, const std::string& fs_variant)
    {
        return load_program("deferred_geom/vs_deferred_geom", vs_variant, "deferred_geom/fs_deferred_geom", fs_variant);
    };

    const auto load_velocity_program = [&](const std::string& vs_variant)
    {
        return load_program("velocity/vs_velocity", vs_variant, "velocity/fs_velocity", default_variant);
    };

    const auto load_screen_program = [&](const std::string& vs, const std::string& fs, const std::string& fs_variant = gfx::shader::DEFAULT_VARIANT)
    {
        return load_program(vs, default_variant, fs, fs_variant);
    };

    geom_program_.cache_uniforms();
    geom_program_.program = load_geom_program(default_variant, default_variant);

    geom_program_skinned_.cache_uniforms();
    geom_program_skinned_.program = load_geom_program("skinned", default_variant);

    geom_program_instanced_.cache_uniforms();
    geom_program_instanced_.program = load_geom_program("instanced", default_variant);

    geom_program_instanced_material_.cache_uniforms();
    geom_program_instanced_material_.program = load_geom_program("instanced_material", "instance_material");

    geom_program_skinned_instanced_.cache_uniforms();
    geom_program_skinned_instanced_.program = load_geom_program("skinned_instanced", default_variant);

    geom_program_skinned_instanced_material_.cache_uniforms();
    geom_program_skinned_instanced_material_.program = load_geom_program("skinned_instanced_material", "instance_material");

    geom_program_skinned_baked_instanced_.cache_uniforms();
    geom_program_skinned_baked_instanced_.program = load_geom_program("skinned_baked_instanced", default_variant);


    card_capture_program_.cache_uniforms();
    card_capture_program_.program = load_geom_program("card_capture", "card_capture");

    velocity_program_.cache_uniforms();
    velocity_program_.program = load_velocity_program(default_variant);

    velocity_program_skinned_.cache_uniforms();
    velocity_program_skinned_.program = load_velocity_program("skinned");

    velocity_program_instanced_.cache_uniforms();
    velocity_program_instanced_.program = load_velocity_program("instanced");

    velocity_program_skinned_instanced_.cache_uniforms();
    velocity_program_skinned_instanced_.program = load_velocity_program("skinned_instanced");

    velocity_program_skinned_baked_instanced_.cache_uniforms();
    velocity_program_skinned_baked_instanced_.program = load_velocity_program("skinned_baked_instanced");

    velocity_camera_program_.cache_uniforms();
    velocity_camera_program_.program = load_screen_program("vs_clip_quad", "velocity/fs_velocity_camera");

    velocity_debug_program_.cache_uniforms();
    velocity_debug_program_.program = load_screen_program("vs_clip_quad", "velocity/fs_velocity_debug");

    particle_overdraw_debug_program_.cache_uniforms();
    particle_overdraw_debug_program_.program = load_screen_program("vs_clip_quad", "particles/fs_particle_overdraw_debug");

    exposure_debug_program_.cache_uniforms();
    exposure_debug_program_.program = load_screen_program("vs_clip_quad", "exposure/fs_exposure_debug");

    sphere_ref_probe_program_.cache_uniforms();
    sphere_ref_probe_program_.program = load_screen_program("vs_clip_quad_ex", "reflection_probe/fs_sphere_reflection_probe");

    box_ref_probe_program_.cache_uniforms();
    box_ref_probe_program_.program = load_screen_program("vs_clip_quad_ex", "reflection_probe/fs_box_reflection_probe");

    indirect_lighting_program_.cache_uniforms();
    indirect_lighting_program_.program = load_screen_program("vs_clip_quad", "fs_deferred_indirect_light");

    indirect_lighting_probe_program_.cache_uniforms();
    indirect_lighting_probe_program_.program = load_screen_program("vs_clip_quad", "fs_deferred_indirect_light", "probe");

    const auto load_no_screen_inputs_variants = [&](indirect_lighting_no_screen_inputs_variants& variants, const std::string& name)
    {
        const auto load_variant = [&](indirect_lighting_program& variant, const char* suffix)
        {
            variant.cache_uniforms();
            variant.program = load_screen_program("vs_clip_quad", "fs_deferred_indirect_light", name + suffix);
        };
        load_variant(variants.base, "");
        load_variant(variants.no_subsurface, "_no_subsurface");
        load_variant(variants.low_detail, "_low_detail");
        load_variant(variants.no_subsurface_low_detail, "_no_subsurface_low_detail");
    };
    load_no_screen_inputs_variants(indirect_lighting_no_screen_inputs_, "no_screen_inputs");
    load_no_screen_inputs_variants(indirect_lighting_probe_no_screen_inputs_, "probe_no_screen_inputs");

    auto cs_irradiance = am.get_asset<gfx::shader>("engine:/data/shaders/irradiance/cs_irradiance_sh.eshader");
    if(cs_irradiance)
    {
        irradiance_compute_program_.cache_uniforms();
        irradiance_compute_program_.program = std::make_unique<gpu_program>(cs_irradiance);
    }

    debug_visualization_program_.cache_uniforms();
    debug_visualization_program_.program = load_screen_program("vs_clip_quad", "gbuffer/fs_gbuffer_visualize");

    // Color lighting.

    // Uniforms before programs (the cache_uniform order contract): every slot shares the
    // same uniform set, so registering one of each array is enough for all of them.
    // Every program of an array of light programs, whatever its dimensions.
    const auto cache_light_uniforms = [](const auto& self, auto& programs) -> void
    {
        for(auto& entry : programs)
        {
            if constexpr(std::is_array_v<std::remove_reference_t<decltype(entry)>>)
            {
                self(self, entry);
            }
            else
            {
                entry.full.cache_uniforms();
                entry.no_contact.cache_uniforms();
                entry.no_contact_no_subsurface.cache_uniforms();
            }
        }
    };
    cache_light_uniforms(cache_light_uniforms, color_lighting_no_shadow_);
    cache_light_uniforms(cache_light_uniforms, color_lighting_);
    cache_light_uniforms(cache_light_uniforms, color_lighting_pcf_tent_);
    // Each light program with its variants (light_program_variants). A shadow filter starts its variants' names; without
    // shadows there is none.
    const auto load_light_program = [&](light_program_variants& variants, const std::string& fs, const std::string& shadow_filter)
    {
        const auto prefix = shadow_filter.empty() ? std::string{} : shadow_filter + "_";
        variants.full.program = load_screen_program("vs_clip_quad", fs, shadow_filter.empty() ? default_variant : shadow_filter);
        variants.no_contact.program = load_screen_program("vs_clip_quad", fs, prefix + "no_contact");
        variants.no_contact_no_subsurface.program = load_screen_program("vs_clip_quad", fs, prefix + "no_contact_no_subsurface");
    };

    // clang-format off
    load_light_program(color_lighting_no_shadow_[uint8_t(light_type::spot)], "fs_deferred_spot_light", "");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)], "fs_deferred_spot_light", "hard");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ], "fs_deferred_spot_light", "pcf");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ], "fs_deferred_spot_light", "pcss");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ], "fs_deferred_spot_light", "vsm");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ], "fs_deferred_spot_light", "esm");

    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)], "fs_deferred_spot_light", "hard_linear");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ], "fs_deferred_spot_light", "pcf_linear");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ], "fs_deferred_spot_light", "pcss_linear");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ], "fs_deferred_spot_light", "vsm_linear");
    load_light_program(color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ], "fs_deferred_spot_light", "esm_linear");

    load_light_program(color_lighting_no_shadow_[uint8_t(light_type::point)], "fs_deferred_point_light", "");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)], "fs_deferred_point_light", "hard");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ], "fs_deferred_point_light", "pcf");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ], "fs_deferred_point_light", "pcss");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ], "fs_deferred_point_light", "vsm");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ], "fs_deferred_point_light", "esm");

    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)], "fs_deferred_point_light", "hard_linear");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ], "fs_deferred_point_light", "pcf_linear");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ], "fs_deferred_point_light", "pcss_linear");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ], "fs_deferred_point_light", "vsm_linear");
    load_light_program(color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ], "fs_deferred_point_light", "esm_linear");

    load_light_program(color_lighting_no_shadow_[uint8_t(light_type::directional)], "fs_deferred_directional_light", "");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)], "fs_deferred_directional_light", "hard");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ], "fs_deferred_directional_light", "pcf");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ], "fs_deferred_directional_light", "pcss");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ], "fs_deferred_directional_light", "vsm");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ], "fs_deferred_directional_light", "esm");

    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)], "fs_deferred_directional_light", "hard_linear");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ], "fs_deferred_directional_light", "pcf_linear");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ], "fs_deferred_directional_light", "pcss_linear");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ], "fs_deferred_directional_light", "vsm_linear");
    load_light_program(color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ], "fs_deferred_directional_light", "esm_linear");

    // Linear spot and point maps keep the point-tap PCF and have no tent sizes; point lights never reach the 7x7.
    load_light_program(color_lighting_pcf_tent_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent3_slot], "fs_deferred_spot_light", "pcf_tent3");
    load_light_program(color_lighting_pcf_tent_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent7_slot], "fs_deferred_spot_light", "pcf_tent7");
    load_light_program(color_lighting_pcf_tent_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent3_slot], "fs_deferred_point_light", "pcf_tent3");
    load_light_program(color_lighting_pcf_tent_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent3_slot], "fs_deferred_directional_light", "pcf_tent3");
    load_light_program(color_lighting_pcf_tent_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent7_slot], "fs_deferred_directional_light", "pcf_tent7");
    load_light_program(color_lighting_pcf_tent_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][ANONYMOUS::pcf_tent3_slot], "fs_deferred_directional_light", "pcf_linear_tent3");
    load_light_program(color_lighting_pcf_tent_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][ANONYMOUS::pcf_tent7_slot], "fs_deferred_directional_light", "pcf_linear_tent7");
    // clang-format on

    ibl_brdf_lut_ = am.get_asset<gfx::texture>("engine:/data/textures/ibl_brdf_lut.png");

    return pipeline::init(ctx);
}

auto deferred::deinit(rtti::context& ctx) -> bool
{
    return true;
}

} // namespace rendering
} // namespace unravel
