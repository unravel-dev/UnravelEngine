#include "pipeline_internal.h"

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
auto get_default_format() -> bgfx::TextureFormat::Enum
{
    return bgfx::TextureFormat::RGBA8;
}

auto get_default_hdr_format() -> bgfx::TextureFormat::Enum
{
    return bgfx::TextureFormat::RGBA16F;
}

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
    const auto& tex2 = rview.tex_acquire("GBUFFER_2", {.size = render_size, .format = format, .flags = BGFX_TEXTURE_RT});
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
    const auto format = wants_hdr_buffers(params) ? get_default_hdr_format() : get_default_format();
    const auto& tex = rview.tex_acquire("LBUFFER", {.size = render_size, .format = format, .flags = BGFX_TEXTURE_RT});

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
    const bool is_camera_run = params.run_type == pipeline::pipeline_run_type::camera;
    return is_camera_run ? get_view_render_size(viewport_size, params.render_resolution, params.render_scale) : viewport_size;
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

void deferred::run_pipeline_impl(const gfx::frame_buffer::ptr& output,
                                 scene& scn,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 delta_t dt,
                                 const run_params& params,
                                 layer_mask render_mask)
{
    APP_SCOPE_PERF("Rendering/Run Pipeline");

    const pipeline_flags stages = params.pflags;
    const bool is_camera_run = params.run_type == pipeline_run_type::camera;
    const bool is_probe_capture = params.run_type == pipeline_run_type::reflection_probe_capture;

    if(is_camera_run)
    {
        stats_ = {};
    }

    gfx::frame_buffer::ptr target = nullptr;

    const bool build_shadowmaps = (stages & pipeline_steps::shadow_pass) != 0u;
    const bool build_reflection_probes = (stages & pipeline_steps::reflection_probe) != 0u;

    // Before the probe captures: they are the frame's first views of the local lights, whose shadow maps every later
    // view reuses, so they render them with the camera's instanced casters too.
    if(is_camera_run)
    {
        gather_instance_sources(scn, camera, render_mask);
    }

    if(build_reflection_probes)
    {
        build_reflections(scn, camera, dt, params.limits);
    }

    is_camera_run_ = is_camera_run;
    render_mask_ = render_mask;
    run_scene_ = &scn;
    camera_cut_ = hpp::nullopt;

    if(build_shadowmaps)
    {
        build_shadows(scn, camera, dt, params.limits, render_mask);
    }

    // Before any pass writes or reads scene lighting (the nested probe captures above rendered
    // with their own unit pre-exposure). Stored in the render view; the passes read it back.
    const pre_exposure_state pre_exposure = update_pre_exposure(rview, params, is_camera_run);

    // The GI scene: instance residency and this view's global distance field (gating rationale at the definition).
    run_gi_scene_passes(scn, camera, rview, params);

    // The scene renders at the render size; the camera's viewport keeps the output's size, which the screen
    // helpers, the UI and the picking work in.
    const auto& viewport_size = camera.get_viewport_size();
    const usize32_t render_size = get_run_render_size(viewport_size, params);
    create_or_resize_d_buffer(rview, render_size, params);
    create_or_resize_g_buffer(rview, render_size, params);
    create_or_resize_l_buffer(rview, render_size, params);
    create_or_resize_reflection_buffers(rview, render_size, params);

    apply_pipeline_taa_jitter_to_camera(camera, render_size, params);

    // Velocity buffer production: camera runs that have a reader (is_velocity_wanted). It is two
    // passes at the render size, so a run without temporal effects skips it. The velocity_pass step
    // bit stays the opt-out lever: probe captures build pflags from 0 and never set it, and a
    // custom caller can clear it. Consumers receive the texture EXPLICITLY through their run
    // params (this pipeline is the only place that fetches "VELOCITY" from the render view); a
    // valid texture IS their enable - null falls back to their matrix reprojection. Also excludes
    // movers from static-mesh batching below so their G-buffer raster matches the velocity pass
    // for the EQUAL depth test.
    velocity_run_active_ =
        is_camera_run && (stages & pipeline_steps::velocity_pass) != 0u && is_velocity_wanted(params);
    if(velocity_run_active_)
    {
        model_component::request_velocity_recording(gfx::get_render_frame());
    }

    const auto visibility_set = collect_visible_models(scn, camera, params, render_mask, dt);

    run_g_buffer_pass(visibility_set, camera, rview, params.limits, dt);

    run_velocity_pass(visibility_set, camera, rview, render_size);

    run_screen_ao_pass(camera, rview, dt, params);

    // SSR composites into RBUFFER from the traced clear. The GI reflections write every pixel of it
    // when they run, and without a traced layer the indirect pass reads the clear value from a
    // constant texture (get_traced_reflection_layer), so only SSR needs the clear.
    const bool gi_owns_reflections = is_camera_run && gi_reflections_own_view(params);
    gi_reflections_written_ = false;
    traced_reflections_cleared_ = false;
    if(build_reflection_probes && is_ssr_wanted(params))
    {
        clear_traced_reflections(rview);
    }
    run_reflection_probe_pass(scn, camera, rview, build_reflection_probes, gi_owns_reflections, dt);

    const bool hiz_active = run_hiz_pass(rview, params, render_size);

    // SSR samples last frame's PREV_SCENE_HDR snapshot (post-TAA, scene-referred linear).
    // It must NOT sample the final OBUFFER: that image is tonemapped, sRGB-encoded and has
    // UI composited on it -- display-referred values injected into linear lighting, which
    // auto exposure then meters and re-amplifies (runaway brightening in dark scenes).
    run_ssr_pass(camera, rview, params);

    // Cloud shadow map before any lighting: the directional light and the irradiance bake
    // read it.
    run_cloud_shadow_pass(scn, camera, rview, params.limits);

    // The sky's irradiance SH, before GI: the radiance cache, the screen probes and the card radiosity read this
    // frame's sky on their misses (a view's first frame included), and the indirect pass composites it.
    const irradiance_pass_result irradiance = run_irradiance_pass(scn, rview, params.limits);

    // Direct lighting starts the current frame LBUFFER after SSR has consumed its history source.
    target = run_direct_lighting_pass(scn, camera, rview, build_shadowmaps, params.limits, dt);

    // GI: the surface cache, the screen probe gather and the reflections. Runs after direct lighting, so this
    // frame's light buffer is populated, and before the indirect pass, which composites the results.
    bool gi_active = false;
    if(is_camera_run)
    {
        gi_active = run_gi_pass(camera, rview, params);
    }
    if(build_reflection_probes && gi_owns_reflections && !gi_reflections_written_)
    {
        clear_traced_reflections(rview);
    }

    // SSIL pass
    run_ssil_pass(camera, rview, params, gi_active);

    // Indirect lighting after SSIL so it can use the result.
    target = run_indirect_lighting_pass(camera, rview, irradiance, build_reflection_probes, params.limits, dt);

    if(stages & pipeline_steps::atmospheric)
    {
        target = run_atmospherics_pass(target, scn, camera, rview, params.limits);
    }

    // Over the sky and the clouds; the particles drawn next fog themselves with the same fog.
    const fog_pass::shader_params fog = run_fog_pass(scn, camera, rview, target, params, build_shadowmaps);

    gfx::texture::ptr particle_reactive;
    if(stages & pipeline_steps::particles_pass)
    {
        const gfx::texture::ptr hiz_depth = hiz_active ? rview.tex_find("HIZBUFFER") : gfx::texture::ptr{};
        const bool wants_reactive = static_cast<bool>(params.fill_taa_params);
        particle_reactive =
            run_particle_pass(scn, camera, rview, target, pre_exposure, hiz_depth, build_shadowmaps, wants_reactive, dt, fog, params.limits, render_mask);
    }

    if(is_camera_run)
    {
        run_gi_visualize_scene_color(camera, rview, pre_exposure);
        release_gi_visualize_card_generation();
    }

    if(is_probe_capture)
    {
        blit_to_output(rview, target, output);
        finish_run_batches();
        return;
    }

    const auto scene = acquire_scene_output(rview, output, viewport_size, render_size, params.needs_output_depth);

    target = run_taa_pass(camera, rview, target, params, particle_reactive);

    // Scene-referred history for next frame's SSR trace and GI far-field. Taken after TAA
    // (temporally stable) and before bloom/tonemap/UI (still linear HDR, no display encode,
    // no interface pixels). Only kept while a consumer exists.
    const bool wants_scene_history =
        is_camera_run &&
        ((reflection_screen_stack_enabled(params) && params.fill_ssr_params) || params.fill_gi_params);
    snapshot_prev_scene_color(rview, target, camera, wants_scene_history);

    run_auto_exposure_pass(rview, camera, target, params, dt);

    // The tonemapper adds the bloom on top of the scene the exposure pass metered, which local exposure measures.
    const bloom_pass::composite bloom = run_bloom_pass(rview, target, params);

    const bool is_upscaled = scene.color != output;
    target = run_tonemapping_pass(rview, target, bloom, scene.color, params, is_upscaled);

    run_fxaa_pass(rview, target, scene.color, params, is_upscaled);

    if(is_camera_run)
    {
        run_ui_pass(scn, camera, rview, scene.color_depth);
        run_debug_passes(camera, rview, scene.color, params);
        // Whatever shaders printed this frame (nothing to draw unless one did).
        shader_print_.draw(scene.color);
        // The temporal-stability instrument measures the finished image (the lit frame or the active
        // debug view, before the editor draws its overlays) and must read LAST frame's depth, so it
        // runs before the depth snapshot below.
        run_temporal_probe_pass(camera, rview, scene.color);
    }

    if(is_upscaled)
    {
        upscale_pass::run_params upscale;
        upscale.input = scene.color;
        upscale.output = output;
        upscale.output_depth = scene.output_depth;
        upscale.input_depth = rview.tex_find("DEPTH");
        run_upscale_pass(std::move(upscale), params);
    }

    // After all passes that sample PREV_DEPTH (must follow Hi-Z / SSIL path).
    //
    // The GI is a second, independent consumer: its temporal accumulation validates
    // reprojected history against this depth and treats a missing one as "no history", so the
    // snapshot cannot be gated on the Hi-Z stack alone.
    // TAA is a third consumer: its disocclusion test compares the reprojected
    // expected depth against this snapshot (a null snapshot degrades the test to a
    // same-frame approximation on frame 0 only).
    const bool taa_active = static_cast<bool>(params.fill_taa_params);
    // GTAO is a fourth consumer: its temporal accumulation reprojects against this depth and
    // treats a missing one as "no history" (raw per-frame noise).
    const bool gtao_active = static_cast<bool>(rview.tex_find("GTAO"));
    snapshot_prev_depth(rview, render_size, hiz_active || gi_active || taa_active || gtao_active);

    finish_run_batches();
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
