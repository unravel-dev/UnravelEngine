#include "gi_resolve_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>

#include <graphics/graphics.h>
#include <logging/logging.h>

namespace unravel
{

auto gi_resolve_pass::create_or_update_target(gfx::render_view& rview,
                                              const std::string& name,
                                              const usize32_t& size,
                                              gfx::texture::ptr& out_tex) -> gfx::frame_buffer::ptr
{
    auto& tex = rview.tex_get_or_emplace(name);
    if(gfx::needs_recreate(tex, size))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(size.width,
                                             size.height,
                                             false,
                                             1,
                                             gfx::texture_format::RGBA16F,
                                             BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }
    auto& fbo = rview.fbo_get_or_emplace(name);
    if(gfx::needs_recreate(fbo, size))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }
    out_tex = tex;
    return fbo;
}

auto gi_resolve_pass::create_or_update_target_mrt(gfx::render_view& rview,
                                                  const std::string& name,
                                                  const usize32_t& size,
                                                  gfx::texture::ptr& out_color,
                                                  gfx::texture::ptr& out_moments) -> gfx::frame_buffer::ptr
{
    const auto flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    auto& color = rview.tex_get_or_emplace(name);
    if(gfx::needs_recreate(color, size))
    {
        color.reset();
        color = std::make_shared<gfx::texture>(size.width, size.height, false, 1,
                                               gfx::texture_format::RGBA16F, flags);
    }
    auto& moments = rview.tex_get_or_emplace(name + "_MOMENTS");
    if(gfx::needs_recreate(moments, size))
    {
        moments.reset();
        moments = std::make_shared<gfx::texture>(size.width, size.height, false, 1,
                                                 gfx::texture_format::RGBA16F, flags);
    }
    auto& fbo = rview.fbo_get_or_emplace(name);
    if(gfx::needs_recreate(fbo, size))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({color, moments});
    }
    out_color = color;
    out_moments = moments;
    return fbo;
}

auto gi_resolve_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_gi_resolve = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_resolve.sc");
    resolve_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_gi_resolve);
    resolve_program_.cache_uniforms();
    auto fs_gi_temporal = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_temporal.sc");
    temporal_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_gi_temporal);
    temporal_program_.cache_uniforms();
    auto fs_gi_upsample = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_upsample.sc");
    upsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_gi_upsample);
    upsample_program_.cache_uniforms();
    if(!upsample_program_.is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI upsample program failed to load. The gather will be "
                       "reconstructed bilinearly and will fringe at silhouettes.");
    }
    auto fs_gi_denoise = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_denoise.sc");
    denoise_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_gi_denoise);
    denoise_program_.cache_uniforms();
    if(!denoise_program_.is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI denoise program failed to load. The gather will run "
                       "without spatial filtering and will be grainier than intended.");
    }
    if(!temporal_program_.is_valid())
    {
        // Not fatal -- the gather still runs -- but it silently costs all temporal accumulation,
        // so it must be stated rather than left to be inferred from a noisy image.
        APPLOG_WARNING("[SurfaceCache] GI temporal program failed to load. The gather will run "
                       "without temporal accumulation and will be noisy.");
    }
    return resolve_program_.is_valid() && temporal_program_.is_valid();
}

auto gi_resolve_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/GI/Resolve Pass");
    if(!resolve_program_.is_valid() || !params.g_buffer || !params.cam || !params.surface_cache ||
       !params.view_cache)
    {
        return {};
    }
    auto& surface_cache = *params.surface_cache;
    if(!surface_cache.is_enabled())
    {
        return {};
    }
    auto& cache = surface_cache.get_radiance_cache();
    if(!cache.is_valid())
    {
        return {};
    }
    const auto& instances = surface_cache.get_instances();
    if(instances.empty())
    {
        return {};
    }
    auto& atlas = surface_cache.get_atlas();
    const auto& clipmap = params.view_cache->get_clipmap();
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    const auto& s = params.settings;

    const auto target_size = compute_trace_size(params.g_buffer->get_size(), s.resolution);
    gfx::texture::ptr trace_tex;
    auto trace_fbo = create_or_update_target(rview, "GI_TRACE", target_size, trace_tex);

    gfx::render_pass pass("GI/Resolve Pass");
    pass.bind(trace_fbo.get());
    // The shader reconstructs world positions from depth via u_invViewProj, which bgfx supplies
    // per view.
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());

    resolve_program_.program->begin();

    gfx::set_texture(resolve_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
    gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
    gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
    gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
    // Bound even when unavailable so the sampler always has a valid texture;
    // u_sdf_clipmap_params.w tells the shader whether to consult it.
    const bool clipmap_ready = clipmap_gpu.is_valid();
    gfx::set_texture(resolve_program_.s_sdf_clipmap,
                     4,
                     clipmap_ready ? clipmap_gpu.get_texture() : atlas.get_atlas_texture());
    gfx::set_buffer(6, cache.get_keys_buffer(), gfx::access::Read);
    gfx::set_buffer(7, cache.get_data_buffer(), gfx::access::Read);
    gfx::set_texture(resolve_program_.s_gi_depth, 8, params.g_buffer->get_texture(4));
    gfx::set_texture(resolve_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(instances.size()),
                                 0.0f};
    gfx::set_uniform(resolve_program_.u_sdf_params, sdf_params);
    gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
    gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
    gfx::set_uniform(resolve_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
    gfx::set_uniform(resolve_program_.u_sdf_clipmap_levels,
                     clipmap_gpu.get_level_params(),
                     global_sdf_clipmap::level_count);
    gfx::set_uniform(resolve_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());

    // Must match what the cache pass wrote with, or the key derived here addresses a different
    // cell than the one the update pass filled and every lookup misses.
    const auto& cache_settings = cache.get_settings();
    const float cache_params[4] = {float(cache.get_capacity() - 1u),
                                   cache_settings.base_cell_size,
                                   cache_settings.base_distance,
                                   float(cache_settings.max_level)};
    gfx::set_uniform(resolve_program_.u_gi_cache_params, cache_params);
    // Only w matters to a reader -- the level cross-fade band, which decides whether a surface
    // near a boundary is looked up at one level or blended across two. It has to be the same value
    // the insert pass wrote with, so it comes from the cache and not from this pass's settings.
    const float cache_params2[4] = {0.0f, 0.0f, 0.0f, cache_settings.level_blend};
    gfx::set_uniform(resolve_program_.u_gi_cache_params2, cache_params2);

    const float resolve_params[4] = {float(s.ray_count),
                                     s.max_distance,
                                     s.normal_bias_voxels,
                                     float(gfx::get_render_frame())};
    gfx::set_uniform(resolve_program_.u_gi_resolve_params, resolve_params);
    const float trace_params[4] = {s.near_field_distance,
                                   float(s.max_steps),
                                   s.surface_bias,
                                   s.step_relaxation};
    gfx::set_uniform(resolve_program_.u_gi_resolve_trace, trace_params);
    const auto camera_position = params.cam->get_position();
    const float camera[4] = {camera_position.x, camera_position.y, camera_position.z, s.intensity};
    gfx::set_uniform(resolve_program_.u_gi_resolve_camera, camera);
    // Independent of temporal accumulation, deliberately: the interpolation is deterministic, so
    // unlike a stochastic lookup it needs nothing downstream to average it back out and is just as
    // correct on a single frame.
    const float filter_params[4] = {s.interpolate_cache ? 1.0f : 0.0f,
                                    s.occlude_on_cache_miss ? 1.0f : 0.0f,
                                    0.0f,
                                    0.0f};
    gfx::set_uniform(resolve_program_.u_gi_resolve_filter, filter_params);

    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, resolve_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    resolve_program_.program->end();
    gfx::discard();
    // Falling back to the un-accumulated gather is correct here: it is noisy but valid, whereas
    // dispatching an invalid program would leave the history target holding whatever it held two
    // frames ago and publish that as the result.
    auto accumulated = trace_tex;
    gfx::texture::ptr moments;
    if(s.enable_temporal && temporal_program_.is_valid())
    {
        accumulated = run_temporal(rview, params, trace_tex, target_size, moments);
    }
    auto filtered = accumulated;
    if(s.enable_spatial_denoise && denoise_program_.is_valid() && s.denoise_passes > 0)
    {
        filtered = run_spatial_denoise(rview, params, accumulated, moments, target_size);
    }
    const auto full_size = params.g_buffer->get_size();
    const bool needs_upsample =
        target_size.width != full_size.width || target_size.height != full_size.height;
    if(!needs_upsample || !s.enable_bilateral_upsample || !upsample_program_.is_valid())
    {
        return filtered;
    }
    return run_upsample(rview, params, filtered, target_size);
}

auto gi_resolve_pass::run_upsample(gfx::render_view& rview,
                                   const run_params& params,
                                   const gfx::texture::ptr& input,
                                   const usize32_t& source_size) -> gfx::texture::ptr
{
    const auto& s = params.settings;
    const auto full_size = params.g_buffer->get_size();
    gfx::texture::ptr result;
    auto fbo = create_or_update_target(rview, "GI_UPSAMPLED", full_size, result);
    gfx::render_pass pass("GI/Upsample Pass");
    pass.bind(fbo.get());
    // World positions are reconstructed from depth, so the view state is required.
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    upsample_program_.program->begin();
    gfx::set_texture(upsample_program_.s_gi_input, 0, input);
    gfx::set_texture(upsample_program_.s_gi_depth, 1, params.g_buffer->get_texture(4));
    gfx::set_texture(upsample_program_.s_gi_normal, 2, params.g_buffer->get_texture(1));
    const float texel[4] = {1.0f / float(source_size.width),
                            1.0f / float(source_size.height),
                            float(source_size.width),
                            float(source_size.height)};
    gfx::set_uniform(upsample_program_.u_gi_upsample_texel, texel);
    const float upsample_params[4] = {s.upsample_normal_power, s.upsample_plane_tolerance, 0.0f, 0.0f};
    gfx::set_uniform(upsample_program_.u_gi_upsample_params, upsample_params);
    const auto camera_position = params.cam->get_position();
    const float camera[4] = {camera_position.x, camera_position.y, camera_position.z, 0.0f};
    gfx::set_uniform(upsample_program_.u_gi_upsample_camera, camera);
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, upsample_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    upsample_program_.program->end();
    gfx::discard();
    return result;
}

auto gi_resolve_pass::run_spatial_denoise(gfx::render_view& rview,
                                          const run_params& params,
                                          const gfx::texture::ptr& input,
                                          const gfx::texture::ptr& moments,
                                          const usize32_t& target_size) -> gfx::texture::ptr
{
    const auto& s = params.settings;
    gfx::texture::ptr target_a;
    gfx::texture::ptr target_b;
    auto fbo_a = create_or_update_target(rview, "GI_DENOISE_A", target_size, target_a);
    auto fbo_b = create_or_update_target(rview, "GI_DENOISE_B", target_size, target_b);
    auto source = input;
    const auto camera_position = params.cam->get_position();
    const float denoise_camera[4] = {camera_position.x, camera_position.y, camera_position.z, 0.0f};
    const float texel[4] = {1.0f / float(target_size.width),
                            1.0f / float(target_size.height),
                            float(target_size.width),
                            float(target_size.height)};
    for(int i = 0; i < s.denoise_passes; ++i)
    {
        const bool into_a = (i % 2) == 0;
        const auto& fbo = into_a ? fbo_a : fbo_b;
        const auto& result = into_a ? target_a : target_b;
        gfx::render_pass pass("GI/Denoise Pass");
        pass.bind(fbo.get());
        // World positions are reconstructed from depth, so the view state is required here too.
        pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
        denoise_program_.program->begin();
        gfx::set_texture(denoise_program_.s_gi_input, 0, source);
        gfx::set_texture(denoise_program_.s_gi_depth, 1, params.g_buffer->get_texture(4));
        gfx::set_texture(denoise_program_.s_gi_normal, 2, params.g_buffer->get_texture(1));
        // Without temporal accumulation there is no variance estimate, and the pass below is told
        // to skip the luminance stop rather than be fed a meaningless one.
        gfx::set_texture(denoise_program_.s_gi_moments, 3, moments ? moments : input);
        // Spacing doubles each pass, so the reach grows exponentially for a linear cost.
        const float step = float(1 << i);
        const float denoise_params[4] = {step,
                                         s.denoise_normal_power,
                                         s.denoise_plane_tolerance,
                                         moments ? s.denoise_luma_phi : 0.0f};
        gfx::set_uniform(denoise_program_.u_gi_denoise_params, denoise_params);
        gfx::set_uniform(denoise_program_.u_gi_denoise_texel, texel);
        const float denoise_params2[4] = {s.denoise_low_count_boost, 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(denoise_program_.u_gi_denoise_params2, denoise_params2);
        gfx::set_uniform(denoise_program_.u_gi_denoise_camera, denoise_camera);
        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(pass.id, denoise_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        denoise_program_.program->end();
        gfx::discard();
        source = result;
    }
    return source;
}

auto gi_resolve_pass::run_temporal(gfx::render_view& rview,
                                   const run_params& params,
                                   const gfx::texture::ptr& current,
                                   const usize32_t& target_size,
                                   gfx::texture::ptr& out_moments) -> gfx::texture::ptr
{
    const auto& s = params.settings;
    // Ping-pong: this frame reads what the previous frame wrote and writes the other target,
    // because one texture cannot be sampled and rendered to in the same pass.
    //
    // The parity counter belongs to the RENDER VIEW and advances once per execution of this pass.
    // Deriving it from a global frame counter instead assumes that counter steps exactly once per
    // execution -- and if it ever steps by two, the parity never alternates, the read target is
    // never written, and history is unavailable forever. That failure is silent: the pass simply
    // outputs the un-accumulated gather, which is indistinguishable from temporal being disabled.
    auto& parity = rview.data_get_or_emplace("GI_HISTORY_PARITY", 0u);
    const bool even_frame = (parity & 1u) == 0u;
    ++parity;
    const char* write_name = even_frame ? "GI_HISTORY_A" : "GI_HISTORY_B";
    const char* read_name = even_frame ? "GI_HISTORY_B" : "GI_HISTORY_A";
    gfx::texture::ptr write_tex;
    gfx::texture::ptr write_moments;
    auto write_fbo = create_or_update_target_mrt(rview, write_name, target_size, write_tex, write_moments);
    out_moments = write_moments;
    auto read_tex = rview.tex_safe_get(read_name);
    auto read_moments = rview.tex_safe_get(std::string(read_name) + "_MOMENTS");
    // No history on the first frame, after a resize, or without a previous depth to validate
    // against. Signalled to the shader rather than papered over by binding something neutral,
    // because there is no neutral history: whatever is bound gets blended in as if it were real.
    const bool has_history = read_tex && read_moments && params.prev_depth &&
                             read_tex->get_size().width == target_size.width &&
                             read_tex->get_size().height == target_size.height;
    // Report a history that never becomes available. Without accumulation this pass silently
    // degrades to its un-accumulated gather, which is indistinguishable from temporal being
    // switched off -- so the failure has to announce itself rather than be inferred from noise.
    if(has_history)
    {
        frames_without_history_ = 0;
    }
    else if(++frames_without_history_ == history_warning_frames)
    {
        APPLOG_WARNING("GI resolve has had no temporal history for {} frames. read target '{}' {}, "
                       "previous depth {}. Accumulation is disabled until this resolves.",
                       history_warning_frames,
                       read_name,
                       read_tex ? "present" : "MISSING",
                       params.prev_depth ? "present" : "MISSING");
    }

    gfx::render_pass temporal_pass("GI/Temporal Pass");
    temporal_pass.bind(write_fbo.get());
    temporal_pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    temporal_program_.program->begin();
    gfx::set_texture(temporal_program_.s_gi_current, 0, current);
    gfx::set_texture(temporal_program_.s_gi_history, 1, has_history ? read_tex : current);
    gfx::set_texture(temporal_program_.s_gi_depth, 2, params.g_buffer->get_texture(4));
    gfx::set_texture(temporal_program_.s_gi_prev_depth,
                     3,
                     params.prev_depth ? params.prev_depth : params.g_buffer->get_texture(4));
    gfx::set_texture(temporal_program_.s_gi_normal, 4, params.g_buffer->get_texture(1));
    gfx::set_texture(temporal_program_.s_gi_history_moments, 5, has_history ? read_moments : current);
    // get_matrix(), NOT the address of the transform: math::transform is a class with its own
    // members, so handing its address to a mat4 uniform uploads whatever happens to sit in the
    // first 64 bytes. The shader then reprojects to nonsense and rejects every pixel's history,
    // which looks exactly like temporal accumulation that is switched off.
    const auto prev_view_proj = params.cam->get_prev_view_projection();
    gfx::set_uniform(temporal_program_.u_gi_prev_view_proj, prev_view_proj.get_matrix());
    const auto prev_inv_view_proj = glm::inverse(prev_view_proj.get_matrix());
    gfx::set_uniform(temporal_program_.u_gi_prev_inv_view_proj, prev_inv_view_proj);
    // y is unused now that history is clamped rather than rejected on normal disagreement; kept
    // in the layout so the uniform block does not have to be reshuffled for one slot.
    const float temporal_params[4] = {s.reprojection_tolerance,
                                      0.0f,
                                      s.max_accum_frames,
                                      has_history ? 1.0f : 0.0f};
    gfx::set_uniform(temporal_program_.u_gi_temporal_params, temporal_params);
    const float temporal_clamp[4] = {s.history_clamp_sigma, 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(temporal_program_.u_gi_temporal_clamp, temporal_clamp);
    const float temporal_texel[4] = {1.0f / float(target_size.width),
                                     1.0f / float(target_size.height),
                                     float(target_size.width),
                                     float(target_size.height)};
    gfx::set_uniform(temporal_program_.u_gi_temporal_texel, temporal_texel);
    const auto camera_position = params.cam->get_position();
    const float temporal_camera[4] = {camera_position.x, camera_position.y, camera_position.z, 0.0f};
    gfx::set_uniform(temporal_program_.u_gi_temporal_camera, temporal_camera);
    auto temporal_topology = gfx::clip_quad(1.0f);
    gfx::set_state(temporal_topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB |
                   BGFX_STATE_WRITE_A);
    gfx::submit(temporal_pass.id, temporal_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    temporal_program_.program->end();
    gfx::discard();
    return write_tex;
}

} // namespace unravel
