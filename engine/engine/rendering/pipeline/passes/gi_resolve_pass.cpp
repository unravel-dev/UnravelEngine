#include "gi_resolve_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>

#include <graphics/graphics.h>
#include <logging/logging.h>

namespace unravel
{
namespace
{
/// Mirror of GI_PROBE_DIR_EDGE / GI_PROBE_STRIDE in gi/gi_probe_common.sh.
constexpr uint32_t probe_dir_edge = 8;
constexpr uint32_t probe_vec4_stride = 12;
/// Mirror of GI_PROBE_LAYERS: majority-surface probe plus the adaptive minority-surface one.
constexpr uint32_t probe_layers = 2;

/// Layout of the probe buffer: a flat array of vec4, matching BUFFER_RW(_, vec4, _).
auto get_probe_vec4_layout() -> const gfx::vertex_layout&
{
    static const gfx::vertex_layout layout = []()
    {
        gfx::vertex_layout decl;
        decl.begin().add(gfx::attribute::TexCoord0, 4, gfx::attribute_type::Float).end();
        return decl;
    }();
    return layout;
}
} // namespace

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

gi_resolve_pass::~gi_resolve_pass()
{
    if(bgfx::isValid(probe_buffer_))
    {
        gfx::destroy(probe_buffer_);
        probe_buffer_ = {bgfx::kInvalidHandle};
    }
}

auto gi_resolve_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_gi_resolve = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_resolve.sc");
    resolve_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_gi_resolve);
    resolve_program_.cache_uniforms();
    auto cs_probe_trace = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_probe_trace.sc");
    probe_trace_program_.program = std::make_unique<gpu_program>(cs_probe_trace);
    probe_trace_program_.cache_uniforms();
    auto cs_probe_filter = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_probe_filter.sc");
    probe_filter_program_.program = std::make_unique<gpu_program>(cs_probe_filter);
    probe_filter_program_.cache_uniforms();
    auto fs_probe_integrate = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_probe_integrate.sc");
    probe_integrate_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_probe_integrate);
    probe_integrate_program_.cache_uniforms();
    if(!probe_trace_program_.is_valid() || !probe_filter_program_.is_valid() ||
       !probe_integrate_program_.is_valid())
    {
        // Not fatal: run() falls back to the per-pixel gather, which is correct and merely
        // costs more -- but it must be stated, because the fallback is visually identical.
        APPLOG_WARNING("[SurfaceCache] GI probe gather programs failed to load. Falling back to "
                       "the per-pixel gather, which traces several times more rays for the same "
                       "result.");
    }
    // GI v2 gather programs (plan phase 5). Their absence falls back to the v1 probe path.
    auto cs_v2_trace = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_trace_v2.sc");
    v2_trace_program_.program = std::make_unique<gpu_program>(cs_v2_trace);
    v2_trace_program_.cache_uniforms();
    auto cs_v2_filter = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_filter_v2.sc");
    v2_filter_program_.program = std::make_unique<gpu_program>(cs_v2_filter);
    v2_filter_program_.cache_uniforms();
    auto fs_v2_integrate = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_probe_integrate_v2.sc");
    v2_integrate_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_v2_integrate);
    v2_integrate_program_.cache_uniforms();
    if(!v2_trace_program_.is_valid() || !v2_filter_program_.is_valid() || !v2_integrate_program_.is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI v2 gather programs failed to load. Falling back to the "
                       "v1 probe gather.");
    }
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
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    const auto& s = params.settings;

    const auto target_size = compute_trace_size(params.g_buffer->get_size(), s.resolution);
    gfx::texture::ptr trace_tex;
    auto trace_fbo = create_or_update_target(rview, "GI_TRACE", target_size, trace_tex);

    // Everything both gather architectures share: the trace and cache uniforms. Must match what
    // the cache pass wrote with, or the key derived here addresses a different cell than the one
    // the update pass filled and every lookup misses.
    const bool clipmap_ready = clipmap_gpu.is_valid();
    const auto& cache_settings = cache.get_settings();
    const float cache_params[4] = {float(cache.get_capacity() - 1u),
                                   cache_settings.base_cell_size,
                                   cache_settings.base_distance,
                                   float(cache_settings.max_level)};
    // Only w matters to a reader -- the level cross-fade band. It has to be the same value the
    // insert pass wrote with, so it comes from the cache and not from this pass's settings.
    const float cache_params2[4] = {0.0f, 0.0f, 0.0f, cache_settings.level_blend};
    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(instances.size()),
                                 0.0f};
    const float resolve_params[4] = {float(s.ray_count),
                                     s.max_distance,
                                     s.normal_bias_voxels,
                                     float(gfx::get_render_frame())};
    const float trace_params[4] = {s.near_field_distance,
                                   float(s.max_steps),
                                   s.surface_bias,
                                   s.step_relaxation};
    const auto camera_position = params.cam->get_position();
    const float camera[4] = {camera_position.x, camera_position.y, camera_position.z, s.intensity};
    const float filter_params[4] = {s.interpolate_cache ? 1.0f : 0.0f,
                                    s.occlude_on_cache_miss ? 1.0f : 0.0f,
                                    s.near_field_fade_distance,
                                    s.ray_start_voxels};
    // Environment SH for the ray-miss sky measurement, mirroring the SSIL trace: null on the
    // first frame (the irradiance pass has not produced it yet), so bind black and signal the
    // shader to skip the measurement rather than read garbage as sky.
    const bool env_ready = static_cast<bool>(params.irradiance_sh);
    const auto env_sh_tex = env_ready ? params.irradiance_sh : default_textures::get().black_texture();
    const float env_params[4] = {env_ready ? 1.0f : 0.0f,
                                 s.contact_occlusion,
                                 s.contact_range,
                                 0.0f};

    // Whether the v2 gather runs this frame: the temporal filter (a separate function) drops
    // its neighbourhood clamp for it - the clamp fights the v2 placement jitter and eats
    // history under motion; Lumen ships depth rejection only [S21 s98].
    v2_active_ = false;
    // The probe gather is the production path; the per-pixel gather remains the reference and
    // the diagnostic surface. A non-zero debug mode forces the per-pixel path, because its
    // modes read individual rays that the probe path deliberately aggregates away.
    const bool probe_path = s.use_probe_gather &&
                            (s.debug_ray_diagnostics == 0 || params.probe_debug != 0) &&
                            probe_trace_program_.is_valid() && probe_filter_program_.is_valid() &&
                            probe_integrate_program_.is_valid();
    if(probe_path)
    {
        // Probe lattice, sized in TRACE-target pixels so probe density follows trace resolution.
        const uint32_t divisor = get_divisor(s.resolution);
        const uint32_t spacing = math::max(uint32_t(math::max(s.probe_spacing, 4)) / math::max(divisor, 1u), 2u);
        const uint32_t probes_x = (target_size.width + spacing - 1u) / spacing;
        const uint32_t probes_y = (target_size.height + spacing - 1u) / spacing;
        const float probe_params[4] = {float(probes_x),
                                       float(probes_y),
                                       float(spacing),
                                       float(gfx::get_render_frame())};
        const float probe_screen[4] = {float(target_size.width),
                                       float(target_size.height),
                                       1.0f / float(target_size.width),
                                       1.0f / float(target_size.height)};
        // Radiance atlases: one 8x8 octahedral tile per probe, PING-PONGED so this frame's trace
        // can blend each texel into last frame's -- the probe-space history that stabilises the
        // whole architecture (see cs_gi_probe_trace.sc).
        const usize32_t atlas_size{probes_x * probe_dir_edge, probes_y * probe_layers * probe_dir_edge};
        const auto ensure_atlas = [&](const char* name) -> gfx::texture::ptr
        {
            auto& tex = rview.tex_get_or_emplace(name);
            if(gfx::needs_recreate(tex, atlas_size))
            {
                tex.reset();
                tex = std::make_shared<gfx::texture>(atlas_size.width,
                                                     atlas_size.height,
                                                     false,
                                                     1,
                                                     gfx::texture_format::RGBA16F,
                                                     BGFX_TEXTURE_COMPUTE_WRITE |
                                                         BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                                         BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
                // A fresh texture holds garbage; the next blend must not read it as history.
                probe_history_valid_ = false;
            }
            return tex;
        };
        auto atlas_a = ensure_atlas("GI_PROBE_ATLAS_A");
        auto atlas_b = ensure_atlas("GI_PROBE_ATLAS_B");
        // Derived data, fully rewritten by the filter each frame: no ping-pong needed.
        auto irradiance_atlas = ensure_atlas("GI_PROBE_IRRADIANCE");
        auto& probe_parity = rview.data_get_or_emplace("GI_PROBE_PARITY", 0u);
        const bool even_probe_frame = (probe_parity & 1u) == 0u;
        ++probe_parity;
        auto write_atlas = even_probe_frame ? atlas_a : atlas_b;
        auto read_atlas = even_probe_frame ? atlas_b : atlas_a;
        // DOUBLE buffered: reprojection needs last frame's meta and counts resident alongside
        // this frame's, and the halves swap with the atlas parity. Each half holds every LAYER's
        // full lattice.
        const uint32_t probe_count = probes_x * probes_y;
        const uint32_t records_per_half = probe_count * probe_layers;
        const uint32_t required_probe_vec4 = 2u * records_per_half * probe_vec4_stride;
        if(!bgfx::isValid(probe_buffer_) || required_probe_vec4 > probe_buffer_capacity_)
        {
            if(bgfx::isValid(probe_buffer_))
            {
                gfx::destroy(probe_buffer_);
            }
            probe_buffer_capacity_ = required_probe_vec4 + required_probe_vec4 / 2u;
            probe_buffer_ = gfx::create_dynamic_vertex_buffer(probe_buffer_capacity_,
                                                              get_probe_vec4_layout(),
                                                              BGFX_BUFFER_COMPUTE_READ_WRITE);
            // Fresh meta is garbage too, and the validity test reads it before the overwrite.
            probe_history_valid_ = false;
        }
        // Reprojection addresses the READ half by the previous frame's lattice; a lattice change
        // makes every such address wrong, which is worse than one fresh frame.
        if(probes_x != probe_grid_x_ || probes_y != probe_grid_y_)
        {
            probe_grid_x_ = probes_x;
            probe_grid_y_ = probes_y;
            probe_history_valid_ = false;
        }
        const uint32_t write_probe_offset = even_probe_frame ? 0u : records_per_half;
        const uint32_t read_probe_offset = even_probe_frame ? records_per_half : 0u;
        // x = the history CAP (the shader grows a per-probe count toward it and blends at
        // 1/count -- a true mean, which settles, never a fixed-weight EMA, which shimmers
        // forever). Forced to 1 while the buffers are untrusted so garbage cannot blend in.
        // y = the probe debug view mode, consumed by the integrate pass.
        const float probe_temporal[4] = {probe_history_valid_ ? math::max(s.probe_history_frames, 1.0f)
                                                              : 1.0f,
                                         float(params.probe_debug),
                                         float(write_probe_offset),
                                         float(read_probe_offset)};
        // GI v2 gather (plan phase 5): the same lattice, atlases and record buffer, driven by
        // three constant-driven programs reading the world structures instead of the radiance
        // hash. The v1 machinery below stays reachable by flipping use_v2_gather - the A/B seam
        // Phase 8 removes together with the hash itself.
        const bool v2_ready = s.use_v2_gather && v2_trace_program_.is_valid() &&
                              v2_filter_program_.is_valid() && v2_integrate_program_.is_valid() &&
                              clipmap_ready && clipmap_gpu.has_world_probes() &&
                              static_cast<bool>(clipmap_gpu.get_light_voxel_texture());
        if(v2_ready)
        {
            v2_active_ = true;
            const float v2_camera[4] = {camera_position.x,
                                        camera_position.y,
                                        camera_position.z,
                                        float(gfx::get_render_frame())};
            const auto& view_clipmap = params.view_cache->get_clipmap();
            const float wp_base_spacing =
                view_clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
            const float wp_params[4] = {wp_base_spacing, float(gfx::get_render_frame()), 1.0f, 0.0f};
            const auto radiance_atlas_tex = clipmap_gpu.get_world_probe_radiance();
            const float wp_radiance_atlas[4] = {1.0f / float(radiance_atlas_tex->info.width),
                                                1.0f / float(radiance_atlas_tex->info.height),
                                                0.0f,
                                                0.0f};
            const float light_voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 1.0f};
            {
                gfx::render_pass pass("GI/Probe Trace");
                pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
                v2_trace_program_.program->begin();
                gfx::set_texture(v2_trace_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
                gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
                gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
                gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
                gfx::set_texture(v2_trace_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
                gfx::set_image(5, write_atlas->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
                gfx::set_texture(v2_trace_program_.s_world_probe_radiance_read,
                                 6,
                                 clipmap_gpu.get_world_probe_radiance());
                gfx::set_buffer(7, probe_buffer_, gfx::access::ReadWrite);
                gfx::set_texture(v2_trace_program_.s_gi_depth, 8, params.g_buffer->get_texture(4));
                gfx::set_texture(v2_trace_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
                gfx::set_texture(v2_trace_program_.s_light_voxels, 10, clipmap_gpu.get_light_voxel_texture());
                gfx::set_texture(v2_trace_program_.s_world_probe_irradiance,
                                 11,
                                 clipmap_gpu.get_world_probe_irradiance());
                gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
                gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
                gfx::set_texture(v2_trace_program_.s_gi_env_sh, 14, env_sh_tex);
                gfx::set_texture(v2_trace_program_.s_world_probe_depth,
                                 15,
                                 clipmap_gpu.get_world_probe_depth());
                gfx::set_uniform(v2_trace_program_.u_sdf_params, sdf_params);
                gfx::set_uniform(v2_trace_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
                gfx::set_uniform(v2_trace_program_.u_sdf_clipmap_levels,
                                 clipmap_gpu.get_level_params(),
                                 global_sdf_clipmap::level_count);
                gfx::set_uniform(v2_trace_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
                gfx::set_uniform(v2_trace_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(v2_trace_program_.u_gi_probe_screen, probe_screen);
                gfx::set_uniform(v2_trace_program_.u_gi_probe_temporal, probe_temporal);
                gfx::set_uniform(v2_trace_program_.u_gi_v2_camera, v2_camera);
                const auto v2_prev_view_proj = params.cam->get_prev_view_projection();
                gfx::set_uniform(v2_trace_program_.u_gi_prev_view_proj, v2_prev_view_proj.get_matrix());
                gfx::set_uniform(v2_trace_program_.u_gi_light_voxel_params, light_voxel_params);
                gfx::set_uniform(v2_trace_program_.u_gi_world_probe_params, wp_params);
                gfx::set_uniform(v2_trace_program_.u_gi_world_probe_atlas,
                                 clipmap_gpu.get_world_probe_atlas_params());
                gfx::set_uniform(v2_trace_program_.u_gi_world_probe_radiance_atlas, wp_radiance_atlas);
                gfx::dispatch(pass.id, v2_trace_program_.program->native_handle(), probes_x, probes_y, 1);
                v2_trace_program_.program->end();
            }
            {
                gfx::render_pass pass("GI/Probe Filter");
                v2_filter_program_.program->begin();
                gfx::set_texture(v2_filter_program_.s_probe_radiance, 0, write_atlas);
                gfx::set_image(2, irradiance_atlas->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
                // ReadWrite: the filter writes the importance mip into the record slots.
                gfx::set_buffer(7, probe_buffer_, gfx::access::ReadWrite);
                gfx::set_uniform(v2_filter_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(v2_filter_program_.u_gi_probe_screen, probe_screen);
                gfx::set_uniform(v2_filter_program_.u_gi_probe_temporal, probe_temporal);
                gfx::dispatch(pass.id, v2_filter_program_.program->native_handle(), probes_x, probes_y, 1);
                v2_filter_program_.program->end();
            }
            {
                gfx::render_pass pass("GI/Probe Integrate");
                pass.bind(trace_fbo.get());
                pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
                v2_integrate_program_.program->begin();
                gfx::set_texture(v2_integrate_program_.s_probe_irradiance, 2, irradiance_atlas);
                gfx::set_buffer(7, probe_buffer_, gfx::access::Read);
                gfx::set_texture(v2_integrate_program_.s_gi_depth, 8, params.g_buffer->get_texture(4));
                gfx::set_texture(v2_integrate_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
                gfx::set_texture(v2_integrate_program_.s_world_probe_irradiance,
                                 11,
                                 clipmap_gpu.get_world_probe_irradiance());
                gfx::set_texture(v2_integrate_program_.s_world_probe_depth,
                                 15,
                                 clipmap_gpu.get_world_probe_depth());
                gfx::set_uniform(v2_integrate_program_.u_sdf_clipmap_levels,
                                 clipmap_gpu.get_level_params(),
                                 global_sdf_clipmap::level_count);
                gfx::set_uniform(v2_integrate_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
                gfx::set_uniform(v2_integrate_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(v2_integrate_program_.u_gi_probe_screen, probe_screen);
                gfx::set_uniform(v2_integrate_program_.u_gi_probe_temporal, probe_temporal);
                gfx::set_uniform(v2_integrate_program_.u_gi_v2_camera, v2_camera);
                gfx::set_uniform(v2_integrate_program_.u_gi_world_probe_params, wp_params);
                gfx::set_uniform(v2_integrate_program_.u_gi_world_probe_atlas,
                                 clipmap_gpu.get_world_probe_atlas_params());
                auto topology = gfx::clip_quad(1.0f);
                gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB |
                               BGFX_STATE_WRITE_A);
                gfx::submit(pass.id, v2_integrate_program_.program->native_handle());
                gfx::set_state(BGFX_STATE_DEFAULT);
                v2_integrate_program_.program->end();
            }
            // The v1 probe-space history is not maintained by v2; a later flip back to v1 must
            // start from a fresh accumulation rather than blend into stale tiles.
            probe_history_valid_ = false;
            gfx::discard();
        }
        else
        {
        {
            gfx::render_pass pass("GI/Probe Trace");
            // u_invViewProj for depth reconstruction, exactly as the cache insert dispatch.
            pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
            probe_trace_program_.program->begin();
            gfx::set_texture(probe_trace_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
            gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
            gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
            gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
            gfx::set_texture(probe_trace_program_.s_sdf_clipmap,
                             4,
                             clipmap_ready ? clipmap_gpu.get_texture() : atlas.get_atlas_texture());
            gfx::set_image(5, write_atlas->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
            gfx::set_buffer(6, cache.get_keys_buffer(), gfx::access::Read);
            gfx::set_buffer(7, cache.get_data_buffer(), gfx::access::Read);
            gfx::set_texture(probe_trace_program_.s_gi_depth, 8, params.g_buffer->get_texture(4));
            gfx::set_texture(probe_trace_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
            gfx::set_buffer(10, probe_buffer_, gfx::access::ReadWrite);
            // Safe to bind even while untrusted: alpha 1 makes the blend ignore what it reads.
            gfx::set_texture(probe_trace_program_.s_probe_history, 11, read_atlas);
            gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
            gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
            gfx::set_texture(probe_trace_program_.s_gi_env_sh, 14, env_sh_tex);
            gfx::set_uniform(probe_trace_program_.u_gi_resolve_env, env_params);
            gfx::set_uniform(probe_trace_program_.u_sdf_params, sdf_params);
            gfx::set_uniform(probe_trace_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
            gfx::set_uniform(probe_trace_program_.u_sdf_clipmap_levels,
                             clipmap_gpu.get_level_params(),
                             global_sdf_clipmap::level_count);
            gfx::set_uniform(probe_trace_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
            gfx::set_uniform(probe_trace_program_.u_gi_cache_params, cache_params);
            gfx::set_uniform(probe_trace_program_.u_gi_cache_params2, cache_params2);
            gfx::set_uniform(probe_trace_program_.u_gi_resolve_params, resolve_params);
            gfx::set_uniform(probe_trace_program_.u_gi_resolve_trace, trace_params);
            gfx::set_uniform(probe_trace_program_.u_gi_resolve_camera, camera);
            gfx::set_uniform(probe_trace_program_.u_gi_resolve_filter, filter_params);
            gfx::set_uniform(probe_trace_program_.u_gi_probe_params, probe_params);
            gfx::set_uniform(probe_trace_program_.u_gi_probe_screen, probe_screen);
            gfx::set_uniform(probe_trace_program_.u_gi_probe_temporal, probe_temporal);
            // The previous frame's matrices, exactly as the screen temporal uses them: history
            // reprojection projects this frame's anchors into last frame's probe lattice.
            const auto prev_view_proj = params.cam->get_prev_view_projection();
            gfx::set_uniform(probe_trace_program_.u_gi_prev_view_proj, prev_view_proj.get_matrix());
            gfx::dispatch(pass.id,
                          probe_trace_program_.program->native_handle(),
                          probes_x,
                          probes_y,
                          probe_layers);
            probe_trace_program_.program->end();
            probe_history_valid_ = true;
        }
        {
            gfx::render_pass pass("GI/Probe Filter");
            probe_filter_program_.program->begin();
            gfx::set_texture(probe_filter_program_.s_probe_radiance, 0, write_atlas);
            gfx::set_buffer(1, probe_buffer_, gfx::access::ReadWrite);
            gfx::set_image(2, irradiance_atlas->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
            gfx::set_uniform(probe_filter_program_.u_gi_probe_params, probe_params);
            gfx::set_uniform(probe_filter_program_.u_gi_probe_screen, probe_screen);
            gfx::set_uniform(probe_filter_program_.u_gi_probe_temporal, probe_temporal);
            gfx::dispatch(pass.id,
                          probe_filter_program_.program->native_handle(),
                          probes_x,
                          probes_y,
                          probe_layers);
            probe_filter_program_.program->end();
        }
        {
            gfx::render_pass pass("GI/Probe Integrate");
            pass.bind(trace_fbo.get());
            pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
            probe_integrate_program_.program->begin();
            // The full tracing set, because starved pixels run the per-pixel gather right here.
            gfx::set_texture(probe_integrate_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
            gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
            gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
            gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
            gfx::set_texture(probe_integrate_program_.s_sdf_clipmap,
                             4,
                             clipmap_ready ? clipmap_gpu.get_texture() : atlas.get_atlas_texture());
            // Read only by the debug views; bound always because an unbound sampler is undefined
            // on some backends.
            gfx::set_texture(probe_integrate_program_.s_probe_radiance, 5, write_atlas);
            gfx::set_buffer(6, cache.get_keys_buffer(), gfx::access::Read);
            gfx::set_buffer(7, cache.get_data_buffer(), gfx::access::Read);
            gfx::set_texture(probe_integrate_program_.s_gi_depth, 8, params.g_buffer->get_texture(4));
            gfx::set_texture(probe_integrate_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
            gfx::set_buffer(10, probe_buffer_, gfx::access::Read);
            gfx::set_texture(probe_integrate_program_.s_probe_irradiance, 11, irradiance_atlas);
            gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
            gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
            gfx::set_texture(probe_integrate_program_.s_gi_env_sh, 14, env_sh_tex);
            gfx::set_uniform(probe_integrate_program_.u_gi_resolve_env, env_params);
            gfx::set_uniform(probe_integrate_program_.u_sdf_params, sdf_params);
            gfx::set_uniform(probe_integrate_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
            gfx::set_uniform(probe_integrate_program_.u_sdf_clipmap_levels,
                             clipmap_gpu.get_level_params(),
                             global_sdf_clipmap::level_count);
            gfx::set_uniform(probe_integrate_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
            gfx::set_uniform(probe_integrate_program_.u_gi_cache_params, cache_params);
            gfx::set_uniform(probe_integrate_program_.u_gi_cache_params2, cache_params2);
            gfx::set_uniform(probe_integrate_program_.u_gi_resolve_params, resolve_params);
            gfx::set_uniform(probe_integrate_program_.u_gi_resolve_trace, trace_params);
            gfx::set_uniform(probe_integrate_program_.u_gi_resolve_camera, camera);
            gfx::set_uniform(probe_integrate_program_.u_gi_resolve_filter, filter_params);
            gfx::set_uniform(probe_integrate_program_.u_gi_probe_params, probe_params);
            gfx::set_uniform(probe_integrate_program_.u_gi_probe_screen, probe_screen);
            gfx::set_uniform(probe_integrate_program_.u_gi_probe_temporal, probe_temporal);
            auto topology = gfx::clip_quad(1.0f);
            gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_WRITE_A);
            gfx::submit(pass.id, probe_integrate_program_.program->native_handle());
            gfx::set_state(BGFX_STATE_DEFAULT);
            probe_integrate_program_.program->end();
        }
        gfx::discard();
        }
    }
    else
    {
        gfx::render_pass pass("GI/Resolve Pass");
        pass.bind(trace_fbo.get());
        // The shader reconstructs world positions from depth via u_invViewProj, which bgfx
        // supplies per view.
        pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
        resolve_program_.program->begin();
        gfx::set_texture(resolve_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
        gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
        gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
        gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
        // Bound even when unavailable so the sampler always has a valid texture;
        // u_sdf_clipmap_params.w tells the shader whether to consult it.
        gfx::set_texture(resolve_program_.s_sdf_clipmap,
                         4,
                         clipmap_ready ? clipmap_gpu.get_texture() : atlas.get_atlas_texture());
        gfx::set_buffer(6, cache.get_keys_buffer(), gfx::access::Read);
        gfx::set_buffer(7, cache.get_data_buffer(), gfx::access::Read);
        gfx::set_texture(resolve_program_.s_gi_depth, 8, params.g_buffer->get_texture(4));
        gfx::set_texture(resolve_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
        // Read only by the debug branch, to cancel the albedo the consumer multiplies the output
        // by. Bound unconditionally because an unbound sampler is undefined on some backends even
        // when the branch that reads it is not taken.
        gfx::set_texture(resolve_program_.s_gi_base_color, 10, params.g_buffer->get_texture(0));
        // The moments the temporal pass will READ this frame -- i.e. what it WROTE last frame --
        // located by peeking the same parity run_temporal derives its targets from, WITHOUT
        // incrementing it. One frame of lag is what makes this safe to read while the current
        // frame's moments are still being produced.
        const uint32_t history_parity = rview.data_get_or_emplace("GI_HISTORY_PARITY", 0u);
        const char* prev_moments_name =
            (history_parity & 1u) == 0u ? "GI_HISTORY_B_MOMENTS" : "GI_HISTORY_A_MOMENTS";
        auto prev_moments = rview.tex_safe_get(prev_moments_name);
        const bool adaptive_rays = s.adaptive_ray_count && s.enable_temporal && prev_moments &&
                                   prev_moments->get_size().width == target_size.width &&
                                   prev_moments->get_size().height == target_size.height;
        gfx::set_texture(resolve_program_.s_gi_prev_moments,
                         11,
                         adaptive_rays ? prev_moments : params.g_buffer->get_texture(0));
        gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
        gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
        gfx::set_texture(resolve_program_.s_gi_env_sh, 14, env_sh_tex);
        gfx::set_uniform(resolve_program_.u_gi_resolve_env, env_params);
        gfx::set_uniform(resolve_program_.u_sdf_params, sdf_params);
        gfx::set_uniform(resolve_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
        gfx::set_uniform(resolve_program_.u_sdf_clipmap_levels,
                         clipmap_gpu.get_level_params(),
                         global_sdf_clipmap::level_count);
        gfx::set_uniform(resolve_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        gfx::set_uniform(resolve_program_.u_gi_cache_params, cache_params);
        gfx::set_uniform(resolve_program_.u_gi_cache_params2, cache_params2);
        gfx::set_uniform(resolve_program_.u_gi_resolve_params, resolve_params);
        gfx::set_uniform(resolve_program_.u_gi_resolve_trace, trace_params);
        gfx::set_uniform(resolve_program_.u_gi_resolve_camera, camera);
        gfx::set_uniform(resolve_program_.u_gi_resolve_filter, filter_params);
        // The settled threshold is a relative luminance sigma. 0.25 was chosen as comfortably
        // above the residual flicker of a converged history and comfortably below the deviation a
        // genuine lighting change produces; a constant rather than a setting because the
        // mechanism is self-correcting in both directions (see settings::adaptive_ray_count).
        constexpr float adaptive_sigma_threshold = 0.25f;
        const float debug_params[4] = {float(s.debug_ray_diagnostics),
                                       adaptive_rays ? float(math::max(s.min_ray_count, 1)) : 0.0f,
                                       adaptive_sigma_threshold,
                                       0.0f};
        gfx::set_uniform(resolve_program_.u_gi_resolve_debug, debug_params);
        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB |
                       BGFX_STATE_WRITE_A);
        gfx::submit(pass.id, resolve_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        resolve_program_.program->end();
        gfx::discard();
    }
    // A probe debug view IS the deliverable: return it crisp, before temporal and the filters
    // smear a diagnostic readout as though it were lighting.
    if(probe_path && params.probe_debug != 0)
    {
        return trace_tex;
    }
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
    const float temporal_clamp[4] = {v2_active_ ? 0.0f : s.history_clamp_sigma, 0.0f, 0.0f, 0.0f};
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
