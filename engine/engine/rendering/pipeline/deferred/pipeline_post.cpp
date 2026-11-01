#include "pipeline_internal.h"

namespace unravel
{
namespace rendering
{

auto deferred::get_debug_tonemapping(const run_params& rparams) -> tonemapping_method
{
    if(!rparams.fill_hdr_params)
    {
        return tonemapping_method::none;
    }
    tonemapping_pass::run_params tonemapping;
    rparams.fill_hdr_params(tonemapping);
    return tonemapping.config.method;
}

auto deferred::get_pre_exposure(gfx::render_view& rview) const -> pre_exposure_state
{
    const auto* state = rview.data().try_get<pre_exposure_state>(pre_exposure_state::view_key);
    return state ? *state : pre_exposure_state{};
}

void deferred::blit_to_output(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& input,
                              const gfx::frame_buffer::ptr& output)
{
    blit_pass::run_params pass_params;
    pass_params.input = input;
    pass_params.output = output;
    blit_pass_.run(rview, pass_params);
}

void deferred::run_debug_passes(const camera& camera,
                                gfx::render_view& rview,
                                const gfx::frame_buffer::ptr& output,
                                const run_params& rparams)
{
    debug_view_labels_.clear();

    if(debug_pass_ == debug_pass_velocity)
    {
        run_velocity_debug_pass(camera, rview, output);
    }
    else if(debug_pass_ == debug_pass_exposure)
    {
        // An overlay over the finished image, so it runs after the UI pass like the others
        // but keeps what is underneath.
        run_exposure_debug_pass(rview, output, rparams);
    }
    else if(debug_pass_ == debug_pass_particle_overdraw)
    {
        run_particle_overdraw_debug_pass(rview, output);
    }
    else if(debug_pass_ == debug_pass_ao_bent_normals)
    {
        run_debug_visualization_pass(camera, rview, output, rparams);
    }
    else if(debug_pass_ >= debug_pass_gi_scene && debug_pass_ <= debug_pass_gi_performance_overview)
    {
        run_gi_visualize_pass(camera, rview, output, rparams);
    }
    else if(debug_pass_ >= 0 && debug_pass_ < debug_pass_gbuffer_modes)
    {
        run_debug_visualization_pass(camera, rview, output, rparams);
    }

    if(gi_visualize_settings_.is_any_overlay())
    {
        run_gi_visualize_overlays(camera, rview, output, rparams);
    }
}

void deferred::snapshot_prev_scene_color(gfx::render_view& rview,
                                         const gfx::frame_buffer::ptr& source,
                                         const camera& camera,
                                         bool has_consumer)
{
    if(!has_consumer)
    {
        rview.tex_remove("PREV_SCENE_HDR");
        return;
    }
    // This frame's G-buffer depth rides along in alpha (scene_history_pass), so the readers
    // can tell a reprojection that still shows its surface from a disoccluded one.
    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto depth = gbuffer ? gbuffer->get_texture(4) : nullptr;
    scene_history_pass_.run(rview, source, depth, camera);
}

void deferred::snapshot_prev_depth(gfx::render_view& rview, const usize32_t& viewport_size, bool has_consumer)
{
    if(!has_consumer)
    {
        rview.tex_remove("PREV_DEPTH");
        return;
    }
    auto depth_src = rview.fbo_get("GBUFFER")->get_texture(4);
    const auto& prev_depth = rview.tex_acquire(
        "PREV_DEPTH",
        {.size = viewport_size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_BLIT_DST});
    gfx::render_pass blit_pass("History/Prev Depth Blit Pass");
    bgfx::blit(blit_pass.id,
               bgfx::TextureRegion{.handle = prev_depth->native_handle()},
               bgfx::TextureRegion{.handle = depth_src->native_handle()});
}

void deferred::run_velocity_debug_pass(const camera& camera,
                                       gfx::render_view& rview,
                                       const gfx::frame_buffer::ptr& output)
{
    if(!output)
    {
        return;
    }
    auto velocity_tex = rview.tex_find("VELOCITY");
    if(!velocity_tex || !velocity_debug_program_.program || !velocity_debug_program_.program->is_valid())
    {
        return;
    }

    gfx::render_pass pass("Debug/Velocity Pass");
    pass.bind(output.get());
    pass.set_view_proj(camera.get_view(), camera.get_projection());

    const auto output_size = output->get_size();

    velocity_debug_program_.program->begin();

    // x = pixels of motion mapped to full brightness in the visualization.
    const float debug_params[4] = {8.0f, 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(velocity_debug_program_.u_params, debug_params);
    gfx::set_texture(velocity_debug_program_.s_velocity, 0, velocity_tex);

    irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
    bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, velocity_debug_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    velocity_debug_program_.program->end();

    bgfx::discard();
}

void deferred::run_particle_overdraw_debug_pass(gfx::render_view& rview, const gfx::frame_buffer::ptr& output)
{
    const auto& overdraw = rview.tex_find("PARTICLE_OVERDRAW");
    if(!output || !overdraw || !particle_overdraw_debug_program_.program ||
       !particle_overdraw_debug_program_.program->is_valid())
    {
        return;
    }
    gfx::render_pass pass("Debug/Particle Overdraw Pass");
    pass.bind(output.get());
    const auto output_size = output->get_size();
    particle_overdraw_debug_program_.program->begin();
    gfx::set_texture(particle_overdraw_debug_program_.s_overdraw, 0, overdraw);
    bgfx::setScissor(0, 0, uint16_t(output_size.width), uint16_t(output_size.height));
    bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(pass.id, particle_overdraw_debug_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    particle_overdraw_debug_program_.program->end();
    bgfx::discard();
}

void deferred::run_exposure_debug_pass(gfx::render_view& rview,
                                       const gfx::frame_buffer::ptr& output,
                                       const run_params& rparams)
{
    if(!output || !exposure_debug_program_.program || !exposure_debug_program_.program->is_valid())
    {
        return;
    }
    // Auto exposure did not run for this camera (disabled, or an LDR / probe run): there is
    // nothing to draw and the lit image stays exactly as the tonemapper left it.
    auto exposure_tex = auto_exposure_pass_.get_exposure_texture(rview);
    auto history_tex = auto_exposure_pass_.get_history_texture(rview);
    auto histogram_tex = auto_exposure_pass_.get_histogram_texture(rview);
    if(!exposure_tex || !history_tex || !histogram_tex)
    {
        return;
    }
    // The live settings, for the markers the average shader decided against.
    auto_exposure_pass::settings config;
    if(rparams.fill_auto_exposure_params)
    {
        auto_exposure_pass::run_params exposure_params;
        rparams.fill_auto_exposure_params(exposure_params);
        config = exposure_params.config;
    }

    // Panel geometry in uv, bottom left: wide enough that the 256-frame trace gets about one
    // column per frame at 1080p, tall enough to separate the two bands.
    constexpr float panel_margin = 0.02f;
    constexpr float panel_width = 0.42f;
    constexpr float panel_height = 0.30f;

    gfx::render_pass pass("Debug/Exposure Pass");
    pass.bind(output.get());

    exposure_debug_program_.program->begin();
    constexpr uint64_t point_clamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    gfx::set_texture(exposure_debug_program_.s_exposure, 0, exposure_tex, point_clamp);
    gfx::set_texture(exposure_debug_program_.s_exposure_history, 1, history_tex, point_clamp);
    gfx::set_texture(exposure_debug_program_.s_exposure_histogram, 2, histogram_tex, point_clamp);
    const float rect[4] = {panel_margin, 1.0f - panel_margin - panel_height, panel_width, panel_height};
    gfx::set_uniform(exposure_debug_program_.u_exposure_debug_rect, rect);
    const float range[4] = {auto_exposure_pass::min_log_lum,
                            auto_exposure_pass::max_log_lum,
                            float(auto_exposure_pass::history_length),
                            float(auto_exposure_pass_.get_history_index(rview))};
    gfx::set_uniform(exposure_debug_program_.u_exposure_debug_range, range);
    const float settings_data[4] = {config.compensation,
                                    config.min_ev,
                                    config.max_ev,
                                    float(auto_exposure_pass::histogram_bins)};
    gfx::set_uniform(exposure_debug_program_.u_exposure_debug_settings, settings_data);
    // Blended, never opaque: the panel is an overlay on the finished frame, which this pass
    // cannot sample (it is the render target).
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(pass.id, exposure_debug_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    exposure_debug_program_.program->end();

    bgfx::discard();
}

namespace
{
/// The camera moved further than auto_exposure_pass::camera_cut_distance, or turned wider than
/// auto_exposure_pass::camera_cut_degrees about its right, up or forward axis, since last frame:
/// a cut, not a move: the exposure snaps to its target and the GI histories start over.
auto is_camera_cut(const camera& cam) -> bool
{
    // The camera recorded last frame's matrices before this frame's passes ran.
    const math::transform previous = math::inverse(cam.get_prev_view());
    const math::transform current = math::inverse(cam.get_view());
    const float min_axis_cosine = std::cos(math::radians(auto_exposure_pass::camera_cut_degrees));
    const bool is_large_turn = math::dot(current.x_unit_axis(), previous.x_unit_axis()) < min_axis_cosine ||
                               math::dot(current.y_unit_axis(), previous.y_unit_axis()) < min_axis_cosine ||
                               math::dot(current.z_unit_axis(), previous.z_unit_axis()) < min_axis_cosine;
    return is_large_turn ||
           math::distance(current.get_position(), previous.get_position()) > auto_exposure_pass::camera_cut_distance;
}

/// The local exposure the tonemapper and the bloom apply: the lookups the exposure pass built for
/// this view this frame and the settings' shape. Inactive when the pass built nothing, which is
/// exactly when the settings are neutral.
auto make_local_exposure_params(const auto_exposure_pass& exposure_pass,
                                gfx::render_view& rview,
                                const pipeline::run_params& rparams) -> local_exposure_params
{
    local_exposure_params params;
    const auto local_view = exposure_pass.get_local_exposure_view(rview);
    if(!local_view.grid || !local_view.blurred || !rparams.fill_auto_exposure_params)
    {
        return params;
    }
    auto_exposure_pass::run_params exposure_params;
    rparams.fill_auto_exposure_params(exposure_params);
    const auto& config = exposure_params.config;
    params.grid = local_view.grid;
    params.blurred = local_view.blurred;
    params.tiles_x = local_view.tiles_x;
    params.tiles_y = local_view.tiles_y;
    params.grid_uv_scale = local_view.grid_uv_scale;
    params.blurred_uv_scale = local_view.blurred_uv_scale;
    params.slices = float(auto_exposure_pass::local_exposure_slices);
    params.min_log_lum = auto_exposure_pass::min_log_lum;
    params.log_lum_range = auto_exposure_pass::max_log_lum - auto_exposure_pass::min_log_lum;
    params.highlight_contrast = config.local_highlight_contrast;
    params.shadow_contrast = config.local_shadow_contrast;
    params.detail_strength = config.local_detail_strength;
    params.blurred_blend = config.local_blurred_blend;
    params.middle_grey_bias = config.local_middle_grey_bias;
    return params;
}
} // namespace

auto deferred::has_camera_cut(const camera& camera) -> bool
{
    if(!camera_cut_)
    {
        camera_cut_ = is_camera_cut(camera);
    }
    return *camera_cut_;
}

auto deferred::run_taa_pass(const camera& camera,
                            gfx::render_view& rview,
                            const gfx::frame_buffer::ptr& input,
                            const run_params& rparams,
                            const gfx::texture::ptr& particle_reactive) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_taa_params)
    {
        taa_pass_.release_resources(rview);
        return input;
    }
    const auto& gbuffer = rview.fbo_find("GBUFFER");
    if(!input || !gbuffer)
    {
        return input;
    }
    taa_pass::run_params p;
    p.input = input;
    p.cam = &camera;
    p.g_buffer = gbuffer;
    // Still the PREVIOUS frame's depth here (the snapshot happens at end of frame);
    // used for disocclusion rejection. Null on the first frame.
    p.prev_depth = rview.tex_find("PREV_DEPTH");
    // THIS frame's velocity buffer (produced right after the G-buffer pass); null when the
    // velocity pass is off, which drops the resolve back to camera-only depth reprojection.
    p.velocity = rview.tex_find("VELOCITY");
    p.reactive = particle_reactive;
    rparams.fill_taa_params(p);
    p.pre_exposure = get_pre_exposure(rview);
    return taa_pass_.run(rview, p);
}

auto deferred::run_fxaa_pass(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& input,
                             const gfx::frame_buffer::ptr& output,
                             const run_params& rparams,
                             bool is_upscaled) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_fxaa_params || rparams.fill_taa_params)
    {
        fxaa_pass_.release_resources(rview);
        return input;
    }

    APP_SCOPE_PERF("Rendering/FXAA Pass");

    fxaa_pass::run_params params;
    params.input = input;
    params.output = output;

    rparams.fill_fxaa_params(params);

    if(rparams.fill_hdr_params && !is_upscaled)
    {
        tonemapping_pass::run_params hdr;
        rparams.fill_hdr_params(hdr);
        params.grain_intensity = hdr.config.grain_intensity;
        params.dithering = hdr.config.dithering;
    }

    return fxaa_pass_.run(rview, params);
}

void deferred::run_upscale_pass(upscale_pass::run_params params, const run_params& rparams)
{
    APP_SCOPE_PERF("Rendering/Upscale Pass");
    if(rparams.fill_hdr_params)
    {
        tonemapping_pass::run_params hdr;
        rparams.fill_hdr_params(hdr);
        params.grain_intensity = hdr.config.grain_intensity;
        params.dithering = hdr.config.dithering;
    }
    upscale_pass_.run(params);
}

auto deferred::update_pre_exposure(gfx::render_view& rview, const run_params& params, bool is_camera_run)
    -> pre_exposure_state
{
    auto& state = rview.data().get_or_emplace<pre_exposure_state>(pre_exposure_state::view_key);
    state.previous = state.value;

    float value = 1.0f;
    const bool has_hdr_exposure = is_camera_run && params.fill_hdr_params;
    const bool auto_exposure_active =
        has_hdr_exposure && reflection_screen_stack_enabled(params) && params.fill_auto_exposure_params != nullptr;
    if(has_hdr_exposure)
    {
        // The CPU's share: the manual exposure scale (the tonemapper's exposure).
        tonemapping_pass::run_params hdr;
        params.fill_hdr_params(hdr);
        value = hdr.config.exposure;
    }
    state.value = std::clamp(value, pre_exposure_state::min_value, pre_exposure_state::max_value);
    // The adaptive share stays on the GPU, in the view's environment SH: it follows the auto exposure for the
    // camera's passes and is 1 for every other run.
    const auto& environment_sh = create_or_get_irradiance_texture(rview);
    auto_exposure_pass_.update_gpu_pre_exposure(rview, environment_sh, state.value, auto_exposure_active);
    state.gpu_texture = environment_sh->native_handle();
    state.gpu_sampler = pre_exposure_sampler_->native_handle();
    return state;
}

void deferred::run_auto_exposure_pass(gfx::render_view& rview,
                                      const camera& camera,
                                      const gfx::frame_buffer::ptr& input,
                                      const run_params& rparams,
                                      delta_t dt)
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_auto_exposure_params)
    {
        auto_exposure_pass_.release_resources(rview);
        return;
    }
    auto_exposure_pass::run_params params;
    params.input = input;
    params.delta_time = dt.count();
    params.camera_cut = has_camera_cut(camera);
    params.pre_exposure = get_pre_exposure(rview);
    params.metering_interval = rparams.limits.per_frame_exposure_metering ? 1u : auto_exposure_pass::limited_metering_interval;
    rparams.fill_auto_exposure_params(params);
    auto_exposure_pass_.run(rview, params);
}

auto deferred::run_bloom_pass(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& input,
                              const run_params& rparams) -> bloom_pass::composite
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_bloom_params || !rparams.fill_hdr_params)
    {
        bloom_pass_.release_resources(rview);
        return {};
    }
    bloom_pass::run_params params;
    params.input = input;
    rparams.fill_bloom_params(params);
    params.config.mip_count = std::min(params.config.mip_count, rparams.limits.max_bloom_mips);
    params.format = get_light_target_format(rparams);
    params.pre_exposure = get_pre_exposure(rview);

    if(rparams.fill_auto_exposure_params)
    {
        params.exposure_texture = auto_exposure_pass_.get_exposure_texture(rview);
        params.local_exposure = make_local_exposure_params(auto_exposure_pass_, rview, rparams);
    }
    tonemapping_pass::run_params hdr;
    rparams.fill_hdr_params(hdr);
    params.manual_exposure = hdr.config.exposure;

    return bloom_pass_.run(rview, params);
}

auto deferred::run_tonemapping_pass(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& input,
                                    const bloom_pass::composite& bloom,
                                    const gfx::frame_buffer::ptr& output,
                                    const run_params& rparams,
                                    bool is_upscaled) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_hdr_params)
    {
        tonemapping_pass_.release_resources(rview);
        return input;
    }
    APP_SCOPE_PERF("Rendering/Tonemapping Pass");

    tonemapping_pass::run_params params;
    params.input = input;
    params.bloom = bloom;

    const bool fxaa_follows = static_cast<bool>(rparams.fill_fxaa_params) && !rparams.fill_taa_params;
    if(!fxaa_follows)
    {
        params.output = output;
    }
    params.defer_output_noise = fxaa_follows || is_upscaled;

    rparams.fill_hdr_params(params);
    params.pre_exposure = get_pre_exposure(rview);

    if(rparams.fill_auto_exposure_params)
    {
        params.exposure_texture = auto_exposure_pass_.get_exposure_texture(rview);
        params.local_exposure = make_local_exposure_params(auto_exposure_pass_, rview, rparams);
    }

    return tonemapping_pass_.run(rview, params);
}

void deferred::run_debug_visualization_pass(const camera& camera,
                                            gfx::render_view& rview,
                                            const gfx::frame_buffer::ptr& output,
                                            const run_params& rparams)
{
    const tonemapping_method tonemapping = get_debug_tonemapping(rparams);
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& irradiance_tex = create_or_get_irradiance_texture(rview);

    gfx::render_pass pass("Debug/Visualization Pass");
    pass.bind(output.get());
    pass.set_view_proj(view, proj);
    // pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

    const auto output_size = output->get_size();

    debug_visualization_program_.program->begin();

    // The AO bent normal view lives past the G-buffer modes in the pass ids; the shader knows it as the mode
    // after those.
    const int shader_mode = debug_pass_ == debug_pass_ao_bent_normals ? debug_pass_gbuffer_modes : debug_pass_;
    // y = the debug-view scale (see set_debug_view_scale).
    float u_params[4] = {float(shader_mode), debug_view_scale_, 0.0f, 0.0f};

    gfx::set_uniform(debug_visualization_program_.u_params, u_params);
    gfx::set_uniform(debug_visualization_program_.u_pre_exposure, get_pre_exposure(rview).to_uniform().data());
    get_pre_exposure(rview).bind();

    size_t i = 0;
    for(; i < gbuffer->get_attachment_count(); ++i)
    {
        gfx::set_texture(debug_visualization_program_.s_tex[i], i, gbuffer->get_texture(i));
    }
    gfx::set_texture(debug_visualization_program_.s_tex[i], i, get_traced_reflection_layer(rview));
    ++i;
    gfx::set_texture(debug_visualization_program_.s_tex[i], i, irradiance_tex);
    ++i;
    // Whichever buffer is actually feeding the indirect consumer, so this view shows what is
    // being used rather than what used to be. Without this the surface cache result has no
    // isolated view at all, and its noise cannot be told apart from noise arriving from the
    // cache upstream of it.
    auto indirect_diffuse_tex = rview.tex_find("GI_RESOLVE");
    if(!indirect_diffuse_tex)
    {
        indirect_diffuse_tex = rview.tex_find("SSIL");
    }
    gfx::set_texture(debug_visualization_program_.s_tex[i],
                     i,
                     indirect_diffuse_tex ? indirect_diffuse_tex : default_textures::get().transparent_texture());
    const auto screen_ao = get_screen_ao_inputs(rview);
    gfx::set_texture(debug_visualization_program_.s_tex[8], 8, screen_ao.texture);
    gfx::set_uniform(debug_visualization_program_.u_screen_ao, screen_ao.params.data());
    const bool indirect_diffuse_is_ssil = indirect_diffuse_tex && !rview.tex_find("GI_RESOLVE");
    const float visualize_indirect[4] = {float(tonemapping),
                                         screen_ao.multi_bounce_albedo_cap,
                                         indirect_diffuse_is_ssil ? 1.0f : 0.0f,
                                         get_gi_resolve_scale(rview)};
    gfx::set_uniform(debug_visualization_program_.u_visualize_indirect, visualize_indirect);
    // The reflection views compose the two reflection layers the way the indirect pass does.
    const auto probe_layer = get_probe_layer_inputs(rview, get_probe_reflection_layer(rview));
    gfx::set_texture(debug_visualization_program_.s_tex[9],
                     9,
                     probe_layer.texture ? probe_layer.texture : default_textures::get().black_texture());
    gfx::set_uniform(debug_visualization_program_.u_probe_layer_params, probe_layer.params.data());
    gfx::set_texture(debug_visualization_program_.s_tex[10], 10, default_textures::get().specular_occlusion().texture.get());

    irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
    bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, debug_visualization_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    debug_visualization_program_.program->end();

    bgfx::discard();
}

} // namespace rendering
} // namespace unravel
