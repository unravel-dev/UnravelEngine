#include "gi_resolve_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/pipeline/passes/gtao_pass.h>

#include <graphics/graphics.h>
#include <logging/logging.h>

#include <cmath>

namespace unravel
{
namespace
{
/// Mirror of GI_PROBE_DIR_EDGE / GI_PROBE_STRIDE in gi/gi_probe_common.sh.
constexpr uint32_t probe_dir_edge = 8;
constexpr uint32_t probe_vec4_stride = 12;
/// LATENCY GATE for the adaptive-ray trace: packing four probes per group cuts rays about
/// in half but also cuts GROUP COUNT four-fold and lengthens each lane's serial chain (the
/// round-robin ray pull). On a small lattice the dispatch is latency-bound, not ray-bound,
/// and the packed form measured SLOWER (FHD at spacing 32, ~2k probes: 0.50 -> 0.75 ms)
/// while the full form's many short, half-culled waves hide their own latency. Adaptive
/// therefore engages only when the lattice is large enough to be occupancy-bound
/// (4K at spacing 32, ~8.2k probes: 0.95 -> 0.75 ms measured).
constexpr uint32_t adaptive_rays_min_probes = 4096;
/// Mirror of GI_PROBE_LAYERS: majority-surface probe plus the adaptive minority-surface one.
/// Single layer (Phase 8): the gather anchors one probe per tile. Must match
/// GI_PROBE_LAYERS in gi_probe_common.sh.
constexpr uint32_t probe_layers = 1;

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
                                              gfx::texture::ptr& out_tex,
                                              bool compute_write) -> gfx::frame_buffer::ptr
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
                                             BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                                 (compute_write ? BGFX_TEXTURE_COMPUTE_WRITE : 0ull));
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
                                                  gfx::texture::ptr& out_moments,
                                                  gfx::texture::ptr& out_fast) -> gfx::frame_buffer::ptr
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
    auto& fast = rview.tex_get_or_emplace(name + "_FAST");
    if(gfx::needs_recreate(fast, size))
    {
        fast.reset();
        fast = std::make_shared<gfx::texture>(size.width, size.height, false, 1,
                                              gfx::texture_format::RGBA16F, flags);
    }
    auto& fbo = rview.fbo_get_or_emplace(name);
    if(gfx::needs_recreate(fbo, size))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({color, moments, fast});
    }
    out_color = color;
    out_moments = moments;
    out_fast = fast;
    return fbo;
}

gi_resolve_pass::~gi_resolve_pass()
{
    if(bgfx::isValid(probe_buffer_))
    {
        gfx::destroy(probe_buffer_);
        probe_buffer_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(probe_traced_))
    {
        gfx::destroy(probe_traced_);
        probe_traced_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(probe_args_))
    {
        gfx::destroy(probe_args_);
        probe_args_ = {bgfx::kInvalidHandle};
    }
}

auto gi_resolve_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    // GI gather programs (plan phase 5). Their absence falls back to the v1 probe path.
    auto cs_place = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_place.sc");
    place_program_.cache_uniforms();
    place_program_.program = std::make_unique<gpu_program>(cs_place);
    auto cs_classify = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_classify.sc");
    classify_program_.cache_uniforms();
    classify_program_.program = std::make_unique<gpu_program>(cs_classify);
    auto cs_args = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_args.sc");
    args_program_.cache_uniforms();
    args_program_.program = std::make_unique<gpu_program>(cs_args);
    auto cs_trace_full =
        am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_trace_full.sc");
    auto cs_trace_adaptive =
        am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_trace_adaptive.sc");
    trace_program_.cache_uniforms();
    trace_program_.program = std::make_unique<gpu_program>(cs_trace_full);
    trace_program_.adaptive_program = std::make_unique<gpu_program>(cs_trace_adaptive);
    if(!trace_program_.is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI probe-trace program failed to load. GI is disabled "
                       "for this run.");
    }
    if(!trace_program_.adaptive_program || !trace_program_.adaptive_program->is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI adaptive-ray trace program failed to load. The "
                       "Adaptive Rays checkbox falls back to the full 64-ray trace.");
    }
    auto cs_interp = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_interp.sc");
    interp_program_.cache_uniforms();
    interp_program_.program = std::make_unique<gpu_program>(cs_interp);
    auto cs_filter = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_screen_probe_filter.sc");
    filter_program_.cache_uniforms();
    filter_program_.program = std::make_unique<gpu_program>(cs_filter);
    auto fs_integrate = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_probe_integrate.sc");
    integrate_program_.cache_uniforms();
    integrate_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_integrate);
    // The fused form is created AFTER both uniform caches so every uniform it reflects
    // already exists (the GL create-uniforms-before-programs contract); temporal_program_'s
    // cache runs below, so the program itself is created there.
    if(!has_gather_programs())
    {
        APPLOG_WARNING("[SurfaceCache] GI gather programs failed to load. GI is disabled for "
                       "this run.");
    }
    auto fs_gi_temporal = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_temporal.sc");
    temporal_program_.cache_uniforms();
    temporal_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_gi_temporal);
    auto fs_integrate_temporal =
        am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_probe_integrate_temporal.sc");
    integrate_program_.fused_program = std::make_unique<gpu_program>(vs_clip_quad, fs_integrate_temporal);
    if(!integrate_program_.fused_program || !integrate_program_.fused_program->is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI fused integrate+temporal failed to load; the split "
                       "pair pays a full-target round trip per frame.");
    }
    auto fs_gi_upsample = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_upsample.sc");
    upsample_program_.cache_uniforms();
    upsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_gi_upsample);
    if(!upsample_program_.is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI upsample program failed to load. The gather will be "
                       "reconstructed bilinearly and will fringe at silhouettes.");
    }
    auto fs_gi_denoise = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_denoise.sc");
    denoise_program_.cache_uniforms();
    denoise_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_gi_denoise);
    auto cs_gi_denoise = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_denoise.sc");
    denoise_program_.compute_program = std::make_unique<gpu_program>(cs_gi_denoise);
    if(!denoise_program_.compute_program || !denoise_program_.compute_program->is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI compute denoise failed to load; the fragment "
                       "fallback runs at ~8x the fetch traffic per pass.");
    }
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
    return has_gather_programs() && temporal_program_.is_valid();
}

auto gi_resolve_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/GI/Resolve Pass");
    if(!has_gather_programs() || !params.g_buffer || !params.cam || !params.surface_cache ||
       !params.view_cache)
    {
        return {};
    }
    auto& surface_cache = *params.surface_cache;
    if(!surface_cache.is_enabled())
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
    // The standalone gather target serves only the SPLIT paths (temporal off, or the fused
    // program missing) and the not-ready clear; the fused path keeps the gather in registers
    // and never touches it, so it is created on demand rather than held as 8 bytes per
    // trace-resolution pixel of unused allocation.
    gfx::texture::ptr trace_tex;
    gfx::frame_buffer::ptr trace_fbo;
    const auto ensure_trace_target = [&]()
    {
        if(!trace_fbo)
        {
            trace_fbo = create_or_update_target(rview, "GI_TRACE", target_size, trace_tex);
        }
    };

    const bool clipmap_ready = clipmap_gpu.is_valid();
    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(instances.size()),
                                 float(surface_cache.get_emitters().size())};
    const auto camera_position = params.cam->get_position();
    // Environment SH for the completion fallback sky; black until the irradiance pass has
    // produced it, which the shader treats as a dark sky rather than reading garbage.
    const auto env_sh_tex =
        params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture();

    // FUSED INTEGRATE + TEMPORAL (G5): with temporal on and the fused program linked, the
    // integrate pass emits the history MRT directly and this frame's gather never round-trips
    // through GI_TRACE. Filled by the integrate block below, consumed where the split
    // temporal would otherwise run.
    const bool fuse_temporal = s.enable_temporal && integrate_program_.fused_program &&
                               integrate_program_.fused_program->is_valid();
    bool fused_temporal_ran = false;
    gfx::texture::ptr fused_out;
    gfx::texture::ptr fused_moments;

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
        // Radiance atlas: one 8x8 octahedral tile per probe, single-buffered and fully
        // rewritten every frame (traced, interpolated or cleared) - the firefly governor
        // reads last frame's texel from it before the overwrite.
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
                // A fresh texture holds garbage the filter would read; one full-weight frame
                // covers it (the importance path validates through the record metas anyway).
                records_trusted_ = false;
            }
            return tex;
        };
        auto& probe_parity = rview.data_get_or_emplace("GI_PROBE_PARITY", 0u);
        const bool even_probe_frame = (probe_parity & 1u) == 0u;
        ++probe_parity;
        auto probe_atlas = ensure_atlas("GI_PROBE_ATLAS");
        // Derived data, fully rewritten by the filter each frame: no ping-pong needed.
        auto irradiance_atlas = ensure_atlas("GI_PROBE_IRRADIANCE");
        // DOUBLE buffered: the importance reprojection needs last frame's meta and mip
        // resident alongside this frame's, and the halves swap each frame. Each half holds
        // every LAYER's full lattice.
        const uint32_t probe_count = probes_x * probes_y;
        const uint32_t records_per_half = probe_count * probe_layers;
        // Both record halves plus the traced-LIST region (one bit-cast coordinate per vec4 -
        // see GiProbeTracedListBase): the trace has no spare binding stage for a dedicated
        // list buffer, so the list rides in this one.
        const uint32_t required_probe_vec4 =
            2u * records_per_half * probe_vec4_stride + probe_count;
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
            // Fresh record memory is garbage; the trace skips importance reprojection until a
            // full frame has written both halves.
            records_trusted_ = false;
        }
        // Reprojection addresses the READ half by the previous frame's lattice; a lattice change
        // makes every such address wrong, which is worse than one fresh frame.
        if(probes_x != probe_grid_x_ || probes_y != probe_grid_y_)
        {
            probe_grid_x_ = probes_x;
            probe_grid_y_ = probes_y;
            records_trusted_ = false;
        }
        // Compaction bookkeeping: the counter buffer ([0] alone - the coordinates live in
        // the probe buffer's list region) and the indirect args the trace launches from.
        if(!bgfx::isValid(probe_traced_))
        {
            probe_traced_capacity_ = 4u;
            probe_traced_ = gfx::create_dynamic_index_buffer(probe_traced_capacity_,
                                                             BGFX_BUFFER_COMPUTE_READ_WRITE |
                                                                 BGFX_BUFFER_INDEX32);
        }
        if(!bgfx::isValid(probe_args_))
        {
            // Entry 0: one 8x8 group per traced probe (the full program). Entry 1:
            // four probes per group (the adaptive program). The args shader writes both.
            probe_args_ = gfx::create_indirect_buffer(2);
        }
        const uint32_t write_probe_offset = even_probe_frame ? 0u : records_per_half;
        const uint32_t read_probe_offset = even_probe_frame ? records_per_half : 0u;
        // STUCK-HOT diagnostic: sustained hotness keeps the temporal fast caps engaged, and
        // no estimator change can fully calm GI in that state - the report attributes it.
        const bool lighting_hot = params.view_cache->get_lighting_quiet_frames() <
                                  surface_cache_view::quiescence_settle_frames;
        if(lighting_hot)
        {
            if(++lighting_hot_streak_ == lighting_hot_report_frames)
            {
                APPLOG_DEBUG("[SurfaceCache] GI lighting-change signal hot for {} consecutive "
                            "frames: the temporal caps are pinned at their fast (noisier) "
                            "values. Expected while a light or emissive keeps animating; "
                            "otherwise a churning light hash or content epoch is holding GI "
                            "in its reactive mode.",
                            lighting_hot_report_frames);
            }
        }
        else
        {
            lighting_hot_streak_ = 0;
        }
        // x = records trusted (gates the trace's importance reprojection),
        // y = unused, zw = the double-buffered record offsets.
        const float probe_temporal[4] = {records_trusted_ ? 1.0f : 0.0f,
                                         0.0f,
                                         float(write_probe_offset),
                                         float(read_probe_offset)};
        // The one gather (plan phase 8: the v1 paths and the radiance hash are gone). Without
        // the world structures there is nothing correct to gather from, so the output clears
        // to zero weight and the consumer's environment term covers the frame.
        const bool gather_ready = clipmap_ready && clipmap_gpu.has_world_probes() &&
                              static_cast<bool>(clipmap_gpu.get_light_voxel_texture());
        if(!gather_ready)
        {
            ensure_trace_target();
            gfx::render_pass pass("GI/Probe Trace");
            pass.bind(trace_fbo.get());
            pass.clear(BGFX_CLEAR_COLOR, 0x00000000u, 1.0f, 0);
            gfx::discard();
            return trace_tex;
        }
        {
            const float gi_camera[4] = {camera_position.x,
                                        camera_position.y,
                                        camera_position.z,
                                        float(gfx::get_render_frame())};
            // The frame's R2 offset in DOUBLE: fract(R2 x float(frame)) in the shader had
            // 1/128 precision after ~1e5 frames and 1/16 after 1e6, so a long session's cone
            // and interpolation jitter collapsed to a few positions (the reflection pass
            // already computes its offset here for the same reason).
            const double frame_index = double(gfx::get_render_frame());
            const float gi_jitter[4] = {float(std::fmod(0.754877666 * frame_index, 1.0)),
                                        float(std::fmod(0.569840291 * frame_index, 1.0)),
                                        0.0f,
                                        0.0f};
            const auto& view_clipmap = params.view_cache->get_clipmap();
            const float wp_base_spacing =
                view_clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
            const float wp_params[4] = {wp_base_spacing,
                                        float(gfx::get_render_frame()),
                                        1.0f,
                                        s.probe_visibility_variance_gate};
            // Once per frame, ahead of the trace: the screen tier declines last frame's
            // composite at hits inside a dirty region (gi_dirty_regions.sh), so the regions
            // and the camera motion are bound before the trace dispatch as well as for the
            // temporal.
            camera_motion_ = measure_camera_motion(params);
            bind_dirty_regions(params, wp_base_spacing);
            const auto radiance_atlas_tex = clipmap_gpu.get_world_probe_radiance();
            const float wp_radiance_atlas[4] = {1.0f / float(radiance_atlas_tex->info.width),
                                                1.0f / float(radiance_atlas_tex->info.height),
                                                0.0f,
                                                0.0f};
            const float light_voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 1.0f};
            // Hoisted out of the trace: placement and reconstruction share them.
            const bool screen_trace = params.hiz && params.hiz->is_valid() && s.enable_screen_trace;
            const auto hiz_or_depth = screen_trace ? params.hiz : params.g_buffer->get_texture(4);
            const bool adaptive = s.adaptive_probes;
            const bool has_prev_color = params.prev_color && params.prev_color->is_valid();
            // The HDR snapshot (scene_history_pass) carries each pixel's view depth in alpha;
            // the LDR fallback cannot, and the kernel then reads history unvalidated.
            const bool prev_color_carries_depth =
                has_prev_color && params.prev_color->info.format == bgfx::TextureFormat::RGBA16F;
            // TAA-JITTER-FREE matrices for the whole gather chain: every pass here
            // reconstructs world positions from depth (probe anchors, trace origins,
            // integration, reprojection), and the jittered projection's sub-pixel wobble
            // made those positions - and with them the probe anchors and the reprojection
            // of a parked camera - march to the TAA sequence every frame (the same defect
            // measured and fixed in the reflection chain). The previous pair is the
            // TAA-unjittered record for the same reason: still camera, exact reprojection.
            const math::transform gather_projection = params.cam->get_projection_unjittered();
            const auto gather_prev_view_proj = params.cam->get_prev_view_projection_unjittered();
            const float screen_trace_params[4] = {screen_trace ? 1.0f : 0.0f,
                                                  float(s.debug_view),
                                                  adaptive ? 1.0f : 0.0f,
                                                  has_prev_color ? (prev_color_carries_depth ? 2.0f : 1.0f)
                                                                 : 0.0f};
            {
                // PLACEMENT (adaptive gather): every probe's anchor lands in the records
                // before the trace runs - the only ordering under which a probe can test
                // itself against its parents' anchors. One thread per probe; noise next to
                // the trace it gates.
                gfx::render_pass pass("GI/Probe Place");
                pass.set_view_proj(params.cam->get_view(), gather_projection);
                place_program_.program->begin();
                gfx::set_texture(place_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
                gfx::set_buffer(6, probe_traced_, gfx::access::Write);
                gfx::set_buffer(7, probe_buffer_, gfx::access::ReadWrite);
                gfx::set_texture(place_program_.s_hiz, 8, hiz_or_depth);
                gfx::set_texture(place_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
                gfx::set_uniform(place_program_.u_sdf_clipmap_levels,
                                 clipmap_gpu.get_level_params(),
                                 global_sdf_clipmap::level_count);
                gfx::set_uniform(place_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
                gfx::set_uniform(place_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(place_program_.u_gi_probe_screen, probe_screen);
                gfx::set_uniform(place_program_.u_gi_probe_temporal, probe_temporal);
                gfx::set_uniform(place_program_.u_gi_camera, gi_camera);
                gfx::set_uniform(place_program_.u_gi_world_probe_params, wp_params);
                gfx::dispatch(pass.id,
                              place_program_.program->native_handle(),
                              (probes_x + 7u) / 8u,
                              (probes_y + 7u) / 8u,
                              1);
                place_program_.program->end();
            }
            {
                // CLASSIFY + COMPACT: traced/interpolated per probe, traced coordinates
                // appended densely. The trace launches exactly that count via the args pass.
                gfx::render_pass pass("GI/Probe Classify");
                classify_program_.program->begin();
                gfx::set_buffer(6, probe_traced_, gfx::access::ReadWrite);
                gfx::set_buffer(7, probe_buffer_, gfx::access::ReadWrite);
                gfx::set_uniform(classify_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(classify_program_.u_gi_probe_temporal, probe_temporal);
                gfx::set_uniform(classify_program_.u_gi_screen_trace, screen_trace_params);
                gfx::dispatch(pass.id,
                              classify_program_.program->native_handle(),
                              (probes_x + 7u) / 8u,
                              (probes_y + 7u) / 8u,
                              1);
                classify_program_.program->end();
            }
            {
                // Also stages the traced count into the list head for the kernel's
                // bounds check, so it needs the probe buffer and the lattice descriptor.
                gfx::render_pass pass("GI/Probe Args");
                args_program_.program->begin();
                gfx::set_buffer(5, probe_args_, gfx::access::Write);
                gfx::set_buffer(6, probe_traced_, gfx::access::Read);
                gfx::set_buffer(7, probe_buffer_, gfx::access::ReadWrite);
                gfx::set_uniform(args_program_.u_gi_probe_params, probe_params);
                gfx::dispatch(pass.id, args_program_.program->native_handle(), 1, 1, 1);
                args_program_.program->end();
            }
            {
                gfx::render_pass pass("GI/Probe Trace");
                pass.set_view_proj(params.cam->get_view(), gather_projection);
                gpu_program* trace_cs = trace_program_.select(
                    s.adaptive_rays && probe_count >= adaptive_rays_min_probes);
                if(trace_cs == nullptr || !trace_cs->is_valid())
                {
                    ensure_trace_target();
                    gfx::discard();
                    return trace_tex;
                }
                // The adaptive program packs four probes per group and launches from args
                // entry 1; the full program is one probe per group, entry 0.
                const bool adaptive_selected = trace_cs == trace_program_.adaptive_program.get();
                trace_cs->begin();
                gfx::set_texture(trace_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
                gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
                gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
                gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
                gfx::set_texture(trace_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
                gfx::set_image(5,
                                 probe_atlas->native_handle(),
                                 0,
                                 gfx::access::ReadWrite,
                                 gfx::texture_format::RGBA16F);
                gfx::set_texture(trace_program_.s_world_probe_radiance_read,
                                 6,
                                 clipmap_gpu.get_world_probe_radiance());
                gfx::set_buffer(7, probe_buffer_, gfx::access::ReadWrite);
                // Hi-Z when present (mip 0 is the device depth, so the anchor reads it the
                // same); raw depth otherwise, with the screen tier switched off below.
                gfx::set_texture(trace_program_.s_hiz, 8, hiz_or_depth);
                gfx::set_texture(trace_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
                gfx::set_texture(trace_program_.s_light_voxels, 10, clipmap_gpu.get_light_voxel_texture());
                // Stage 11 carries LAST frame's composited output (far-field radiance beyond
                // the cascades) - freed from the irradiance cage the trace never read
                // (GI_WORLD_PROBE_SKIP_IRRADIANCE); the traced list rides in the probe
                // buffer's list region at stage 7.
                gfx::set_texture(trace_program_.s_gi_prev_color,
                                 11,
                                 has_prev_color ? params.prev_color
                                                : default_textures::get().black_texture());
                gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
                gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
                gfx::set_texture(trace_program_.s_gi_env_sh, 14, env_sh_tex);
                gfx::set_texture(trace_program_.s_world_probe_depth,
                                 15,
                                 clipmap_gpu.get_world_probe_depth());
                gfx::set_uniform(trace_program_.u_sdf_params, sdf_params);
                gfx::set_uniform(trace_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
                gfx::set_uniform(trace_program_.u_sdf_clipmap_levels,
                                 clipmap_gpu.get_level_params(),
                                 global_sdf_clipmap::level_count);
                gfx::set_uniform(trace_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
                gfx::set_uniform(trace_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(trace_program_.u_gi_probe_screen, probe_screen);
                gfx::set_uniform(trace_program_.u_gi_probe_temporal, probe_temporal);
                gfx::set_uniform(trace_program_.u_gi_camera, gi_camera);
                gfx::set_uniform(trace_program_.u_gi_jitter, gi_jitter);
                gfx::set_uniform(trace_program_.u_gi_screen_trace, screen_trace_params);
                gfx::set_uniform(trace_program_.u_gi_prev_view_proj, gather_prev_view_proj.get_matrix());
                gfx::set_uniform(trace_program_.u_gi_light_voxel_params, light_voxel_params);
                gfx::set_uniform(trace_program_.u_gi_world_probe_params, wp_params);
                gfx::set_uniform(trace_program_.u_gi_world_probe_atlas,
                                 clipmap_gpu.get_world_probe_atlas_params());
                gfx::set_uniform(trace_program_.u_gi_world_probe_radiance_atlas, wp_radiance_atlas);
                gfx::dispatch_indirect(pass.id,
                                       trace_cs->native_handle(),
                                       probe_args_,
                                       adaptive_selected ? 1 : 0,
                                       1);
                trace_cs->end();
                records_trusted_ = true;
            }
            {
                // RECONSTRUCTION + CLEAR: interpolated probes' tiles rebuilt from their
                // parents, dead probes' tiles cleared to black - everything the compacted
                // trace no longer visits. Parents are always trace-written (evens never
                // interpolate), so one read-write image binding carries no intra-pass hazard.
                gfx::render_pass pass("GI/Probe Interp");
                interp_program_.program->begin();
                gfx::set_image(5,
                               probe_atlas->native_handle(),
                               0,
                               gfx::access::ReadWrite,
                               gfx::texture_format::RGBA16F);
                // Read-write: the interp pass mirrors its parents' screen share into the
                // interpolated probe's record (GI_PROBE_SCREEN_SHARE).
                gfx::set_buffer(7, probe_buffer_, gfx::access::ReadWrite);
                gfx::set_uniform(interp_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(interp_program_.u_gi_probe_temporal, probe_temporal);
                gfx::set_uniform(interp_program_.u_gi_screen_trace, screen_trace_params);
                gfx::dispatch(pass.id, interp_program_.program->native_handle(), probes_x, probes_y, 1);
                interp_program_.program->end();
            }
            {
                gfx::render_pass pass("GI/Probe Filter");
                filter_program_.program->begin();
                gfx::set_texture(filter_program_.s_probe_radiance, 0, probe_atlas);
                gfx::set_image(2, irradiance_atlas->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
                // ReadWrite: the filter writes the importance mip into the record slots.
                gfx::set_buffer(7, probe_buffer_, gfx::access::ReadWrite);
                gfx::set_uniform(filter_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(filter_program_.u_gi_probe_screen, probe_screen);
                gfx::set_uniform(filter_program_.u_gi_probe_temporal, probe_temporal);
                gfx::dispatch(pass.id, filter_program_.program->native_handle(), probes_x, probes_y, 1);
                filter_program_.program->end();
            }
            {
                // Fused: bind the history MRT and blend in-register; split: write GI_TRACE
                // for the standalone temporal (or as the final gather when temporal is off).
                history_targets history;
                if(fuse_temporal)
                {
                    history = acquire_history(rview, params, target_size);
                }
                else
                {
                    ensure_trace_target();
                }
                gpu_program* integrate = fuse_temporal ? integrate_program_.fused_program.get()
                                                       : integrate_program_.program.get();
                gfx::render_pass pass(fuse_temporal ? "GI/Probe Integrate+Temporal"
                                                    : "GI/Probe Integrate");
                pass.bind(fuse_temporal ? history.write_fbo.get() : trace_fbo.get());
                pass.set_view_proj(params.cam->get_view(), gather_projection);
                integrate->begin();
                // Never sampled here, bound for OpenGL's benefit (see the struct note).
                gfx::set_texture(integrate_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
                gfx::set_texture(integrate_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
                gfx::set_texture(integrate_program_.s_probe_irradiance, 2, irradiance_atlas);
                gfx::set_buffer(7, probe_buffer_, gfx::access::Read);
                gfx::set_texture(integrate_program_.s_gi_depth, 8, params.g_buffer->get_texture(4));
                gfx::set_texture(integrate_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
                // GTAO bent normal for the lookup direction (the kernel's u_gi_intensity note);
                // white stands in when the pass did not run and the flag lane keeps it unread.
                const auto gtao_tex = rview.tex_safe_get("GTAO");
                const auto* gtao_settings = rview.data().try_get<gtao_pass::settings>("GTAO_SETTINGS");
                gfx::set_texture(integrate_program_.s_gi_gtao,
                                 13,
                                 gtao_tex ? gtao_tex : default_textures::get().white_texture());
                gfx::set_texture(integrate_program_.s_world_probe_irradiance,
                                 11,
                                 clipmap_gpu.get_world_probe_irradiance());
                gfx::set_texture(integrate_program_.s_world_probe_depth,
                                 15,
                                 clipmap_gpu.get_world_probe_depth());
                gfx::set_uniform(integrate_program_.u_sdf_clipmap_levels,
                                 clipmap_gpu.get_level_params(),
                                 global_sdf_clipmap::level_count);
                gfx::set_uniform(integrate_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
                gfx::set_uniform(integrate_program_.u_gi_probe_params, probe_params);
                gfx::set_uniform(integrate_program_.u_gi_probe_screen, probe_screen);
                gfx::set_uniform(integrate_program_.u_gi_probe_temporal, probe_temporal);
                gfx::set_uniform(integrate_program_.u_gi_camera, gi_camera);
                gfx::set_uniform(integrate_program_.u_gi_jitter, gi_jitter);
                const float gi_intensity[4] = {math::max(s.intensity, 0.0f),
                                               gtao_tex ? 1.0f : 0.0f,
                                               gtao_settings ? gtao_settings->bent_normal_strength : 0.0f,
                                               0.0f};
                gfx::set_uniform(integrate_program_.u_gi_intensity, gi_intensity);
                gfx::set_uniform(integrate_program_.u_gi_world_probe_params, wp_params);
                gfx::set_uniform(integrate_program_.u_gi_world_probe_atlas,
                                 clipmap_gpu.get_world_probe_atlas_params());
                if(fuse_temporal)
                {
                    // The temporal half's history samplers on the fused stage map (5/6/10);
                    // its uniforms ride the temporal_program_ handles - bgfx uniforms are
                    // name-global. The no-history dummies are never sampled (the kernel
                    // answers fresh before touching them) but D3D wants a binding.
                    const auto black = default_textures::get().black_texture();
                    gfx::set_texture(temporal_program_.s_gi_history,
                                     5,
                                     history.has_history ? history.read_tex : black);
                    gfx::set_texture(temporal_program_.s_gi_prev_depth,
                                     6,
                                     params.prev_depth ? params.prev_depth
                                                       : params.g_buffer->get_texture(4));
                    gfx::set_texture(temporal_program_.s_gi_history_moments,
                                     10,
                                     history.has_history ? history.read_moments : black);
                    gfx::set_texture(temporal_program_.s_gi_history_fast,
                                     12,
                                     history.has_history ? history.read_fast : black);
                    // This frame's velocity buffer, passed explicitly by the pipeline (a
                    // valid texture IS the enable). Stage 14 - the only register free of
                    // both the fused sampler map and its t-register buffers; black stands
                    // in when absent - the kernel's flag keeps it unread then.
                    const bool use_velocity = params.velocity != nullptr;
                    gfx::set_texture(temporal_program_.s_gi_velocity,
                                     14,
                                     use_velocity ? params.velocity : black,
                                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
                    // The TAA-unjittered previous pair, matching the unjittered current
                    // reconstruction above - a still camera must reproject onto itself.
                    const auto prev_view_proj = params.cam->get_prev_view_projection_unjittered();
                    gfx::set_uniform(temporal_program_.u_gi_prev_view_proj, prev_view_proj.get_matrix());
                    const auto prev_inv_view_proj = glm::inverse(prev_view_proj.get_matrix());
                    gfx::set_uniform(temporal_program_.u_gi_prev_inv_view_proj, prev_inv_view_proj);
                    // LIGHTING-CHANGE-AWARE SLOW LANE: the light hash + content epoch are
                    // exactly the inputs whose change makes accumulated lighting stale
                    // (camera travel is deliberately excluded - reprojection handles it and
                    // the epoch is suppressed while origins move). After an edit the slow
                    // lane collapses to the FAST cap, held for quiescence_settle_frames so
                    // the stale energy actually flushes before the long window returns; a
                    // still world then earns it and the amortization waves average out. The
                    // per-pixel 3-sigma detector snaps only the bright core of a change - the
                    // DIM PENUMBRA of a moved emissive sits below the lane noise and, at the
                    // old hot cap of GI_TEMPORAL_MAX_FRAMES, decayed as the visible
                    // second-long trail (see the constant's justification) - so the global
                    // signal owns the whole flush.
                    // REGION-LOCAL for instance changes: the changed placements' bounds ride
                    // u_gi_temporal_bounds and the kernel drops to the fast cap only around
                    // them (soft over one probe spacing); more regions than the budget holds
                    // falls back to the screen-wide cap - a scene changing everywhere.
                    const bool dirty_overflow = bind_dirty_regions(params, wp_base_spacing);
                    const float slow_cap =
                        (dirty_overflow || params.view_cache->get_lighting_quiet_frames() <
                                               surface_cache_view::quiescence_settle_frames)
                            ? float(gi::GI_TEMPORAL_FAST_FRAMES)
                            : s.temporal_slow_frames;
                    const float temporal_params[4] = {s.reprojection_tolerance,
                                                      float(gi::GI_TEMPORAL_FAST_FRAMES),
                                                      slow_cap,
                                                      history.has_history ? 1.0f : 0.0f};
                    gfx::set_uniform(temporal_program_.u_gi_temporal_params, temporal_params);
                    const float temporal_texel[4] = {1.0f / float(target_size.width),
                                                     1.0f / float(target_size.height),
                                                     float(target_size.width),
                                                     float(target_size.height)};
                    gfx::set_uniform(temporal_program_.u_gi_temporal_texel, temporal_texel);
                    const float temporal_camera[4] = {camera_position.x,
                                                      camera_position.y,
                                                      camera_position.z,
                                                      use_velocity ? 1.0f : 0.0f};
                    gfx::set_uniform(temporal_program_.u_gi_temporal_camera, temporal_camera);
                }
                auto topology = gfx::clip_quad(1.0f);
                gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB |
                               BGFX_STATE_WRITE_A);
                gfx::submit(pass.id, integrate->native_handle());
                gfx::set_state(BGFX_STATE_DEFAULT);
                integrate->end();
                if(fuse_temporal)
                {
                    fused_temporal_ran = true;
                    fused_out = history.write_tex;
                    fused_moments = history.write_moments;
                }
            }
            gfx::discard();
        }
    }

    // Falling back to the un-accumulated gather is correct here: it is noisy but valid, whereas
    // dispatching an invalid program would leave the history target holding whatever it held two
    // frames ago and publish that as the result.
    auto accumulated = trace_tex;
    gfx::texture::ptr moments;
    if(fused_temporal_ran)
    {
        accumulated = fused_out;
        moments = fused_moments;
    }
    else if(s.enable_temporal && temporal_program_.is_valid())
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
    // World positions are reconstructed from depth - jitter-free, like the whole chain.
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
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
    auto fbo_a = create_or_update_target(rview, "GI_DENOISE_A", target_size, target_a, true);
    auto fbo_b = create_or_update_target(rview, "GI_DENOISE_B", target_size, target_b, true);
    auto source = input;
    const bool use_compute =
        denoise_program_.compute_program && denoise_program_.compute_program->is_valid();
    // World positions are reconstructed from depth - jitter-free, like the whole chain.
    const math::transform denoise_projection = params.cam->get_projection_unjittered();
    const auto camera_position = params.cam->get_position();
    const float denoise_camera[4] = {camera_position.x, camera_position.y, camera_position.z, 0.0f};
    const float texel[4] = {1.0f / float(target_size.width),
                            1.0f / float(target_size.height),
                            float(target_size.width),
                            float(target_size.height)};
    // The compute kernel's staging arrays are sized for spacings up to 1 << 2 (three
    // a-trous levels); deeper chains keep the fragment form for the extra levels.
    constexpr int compute_max_pass = 3;
    for(int i = 0; i < s.denoise_passes; ++i)
    {
        const bool into_a = (i % 2) == 0;
        const auto& fbo = into_a ? fbo_a : fbo_b;
        const auto& result = into_a ? target_a : target_b;
        // Spacing doubles each pass, so the reach grows exponentially for a linear cost.
        const float step = float(1 << i);
        const float denoise_params[4] = {step,
                                         s.denoise_normal_power,
                                         s.denoise_plane_tolerance,
                                         moments ? s.denoise_luma_phi : 0.0f};
        // y arms the converged early-out with the accumulation cap (the slow lane's - the
        // count the moments actually accumulate to); only meaningful when a real moments
        // texture is bound (the same condition that arms the luminance stop).
        const float denoise_params2[4] = {s.denoise_low_count_boost,
                                          (moments && s.denoise_converged_early_out && s.enable_temporal)
                                              ? s.temporal_slow_frames
                                              : 0.0f,
                                          math::max(s.denoise_luma_floor, 0.0f),
                                          0.0f};
        if(use_compute && i < compute_max_pass)
        {
            gfx::render_pass pass("GI/Denoise Pass");
            pass.set_view_proj(params.cam->get_view(), denoise_projection);
            denoise_program_.compute_program->begin();
            gfx::set_texture(denoise_program_.s_gi_input, 0, source);
            gfx::set_texture(denoise_program_.s_gi_depth, 1, params.g_buffer->get_texture(4));
            gfx::set_texture(denoise_program_.s_gi_normal, 2, params.g_buffer->get_texture(1));
            gfx::set_texture(denoise_program_.s_gi_moments, 3, moments ? moments : input);
            gfx::set_image(4, result->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
            gfx::set_uniform(denoise_program_.u_gi_denoise_params, denoise_params);
            gfx::set_uniform(denoise_program_.u_gi_denoise_texel, texel);
            gfx::set_uniform(denoise_program_.u_gi_denoise_params2, denoise_params2);
            gfx::set_uniform(denoise_program_.u_gi_denoise_camera, denoise_camera);
            gfx::dispatch(pass.id,
                          denoise_program_.compute_program->native_handle(),
                          (target_size.width + 7u) / 8u,
                          (target_size.height + 7u) / 8u,
                          1);
            denoise_program_.compute_program->end();
            source = result;
            continue;
        }
        gfx::render_pass pass("GI/Denoise Pass");
        pass.bind(fbo.get());
        pass.set_view_proj(params.cam->get_view(), denoise_projection);
        denoise_program_.program->begin();
        gfx::set_texture(denoise_program_.s_gi_input, 0, source);
        gfx::set_texture(denoise_program_.s_gi_depth, 1, params.g_buffer->get_texture(4));
        gfx::set_texture(denoise_program_.s_gi_normal, 2, params.g_buffer->get_texture(1));
        // Without temporal accumulation there is no variance estimate, and the pass below is told
        // to skip the luminance stop rather than be fed a meaningless one.
        gfx::set_texture(denoise_program_.s_gi_moments, 3, moments ? moments : input);
        gfx::set_uniform(denoise_program_.u_gi_denoise_params, denoise_params);
        gfx::set_uniform(denoise_program_.u_gi_denoise_texel, texel);
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
    // REVEAL PASS (the ReBLUR history-fix idea): pixels whose accumulation count is still
    // below GI_DENOISE_REVEAL_COUNT get one more a-trous pass at twice the widest regular
    // spacing; converged pixels pass through at the cost of one moments fetch. A just-revealed
    // region otherwise showed one to eight frames of a 64-ray gather at the fixed 8-texel
    // reach and stayed 3-4x noisier than its surroundings for a dozen frames (measured).
    if(moments && s.enable_temporal && s.denoise_passes > 0)
    {
        const bool into_a = (s.denoise_passes % 2) == 0;
        const auto& fbo = into_a ? fbo_a : fbo_b;
        const auto& result = into_a ? target_a : target_b;
        const float denoise_params[4] = {float(gi::GI_DENOISE_REVEAL_STEP),
                                         s.denoise_normal_power,
                                         s.denoise_plane_tolerance,
                                         s.denoise_luma_phi};
        const float denoise_params2[4] = {s.denoise_low_count_boost,
                                          0.0f,
                                          math::max(s.denoise_luma_floor, 0.0f),
                                          float(gi::GI_DENOISE_REVEAL_COUNT)};
        gfx::render_pass pass("GI/Denoise Reveal");
        pass.bind(fbo.get());
        pass.set_view_proj(params.cam->get_view(), denoise_projection);
        denoise_program_.program->begin();
        gfx::set_texture(denoise_program_.s_gi_input, 0, source);
        gfx::set_texture(denoise_program_.s_gi_depth, 1, params.g_buffer->get_texture(4));
        gfx::set_texture(denoise_program_.s_gi_normal, 2, params.g_buffer->get_texture(1));
        gfx::set_texture(denoise_program_.s_gi_moments, 3, moments);
        gfx::set_uniform(denoise_program_.u_gi_denoise_params, denoise_params);
        gfx::set_uniform(denoise_program_.u_gi_denoise_texel, texel);
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

auto gi_resolve_pass::bind_dirty_regions(const run_params& params, float margin) -> bool
{
    constexpr uint32_t max_regions = uint32_t(gi::GI_TEMPORAL_DIRTY_MAX_BOUNDS);
    float bounds[max_regions * 2u * 4u] = {};
    uint32_t count = 0;
    bool overflow = false;
    if(params.surface_cache != nullptr)
    {
        count = params.surface_cache->pack_dirty_regions(bounds, max_regions);
        overflow = params.surface_cache->get_dirty_regions().size() > size_t(max_regions);
    }
    const float dirty_params[4] = {float(count), math::max(margin, 1e-3f), camera_motion_, 0.0f};
    gfx::set_uniform(temporal_program_.u_gi_temporal_dirty, dirty_params);
    // Attribution log, throttled, for the OVER-BUDGET case only: it means more placements
    // changed than the region budget holds and the screen-wide fast cap is back. Legitimate
    // for a scene load or a crowd; a parked scene reporting it means a placement's pose or
    // material hash churns every frame (measured once: vec3 padding bytes in the hash).
    if(overflow && (gfx::get_render_frame() % 120u) == 0u && params.surface_cache != nullptr)
    {
        const auto& regions = params.surface_cache->get_dirty_regions();
        const auto& first = regions.front().bounds;
        APPLOG_DEBUG("[SurfaceCache] {} dirty GI region(s) this frame{}; first spans ({:.1f}, {:.1f}, {:.1f}) "
                     "to ({:.1f}, {:.1f}, {:.1f}), last change frame {}.",
                     regions.size(),
                     overflow ? " (over budget: screen-wide fast cap)" : "",
                     first.min.x,
                     first.min.y,
                     first.min.z,
                     first.max.x,
                     first.max.y,
                     first.max.z,
                     regions.front().last_change_frame);
    }
    gfx::set_uniform(temporal_program_.u_gi_temporal_bounds, bounds, uint16_t(max_regions * 2u));
    return overflow;
}

auto gi_resolve_pass::measure_camera_motion(const run_params& params) -> float
{
    const math::vec3 position = params.cam->get_position();
    // Normalised here: the view axis comes out of the inverse view transform with a length
    // a little off one, and acos of a dot near one turns that into a standing "turn" of
    // most of a degree per frame (measured 0.7-0.8 of the full rate on a parked camera).
    // The chord of two unit axes is exactly zero for an unchanged pose.
    const math::vec3 axis = math::normalize(params.cam->z_unit_axis());
    float motion = 0.0f;
    if(has_prev_camera_)
    {
        const float travel = math::length(position - prev_camera_position_);
        const float half_chord = math::clamp(0.5f * math::length(axis - prev_camera_axis_), 0.0f, 1.0f);
        const float turn_degrees = math::degrees(2.0f * std::asin(half_chord));
        motion = math::max(travel / gi::GI_TEMPORAL_CAMERA_MOTION_FULL,
                           turn_degrees / gi::GI_TEMPORAL_CAMERA_ROTATION_FULL);
    }
    prev_camera_position_ = position;
    prev_camera_axis_ = axis;
    has_prev_camera_ = true;
    return math::clamp(motion, 0.0f, 1.0f);
}

auto gi_resolve_pass::acquire_history(gfx::render_view& rview,
                                      const run_params& params,
                                      const usize32_t& target_size) -> history_targets
{
    // Ping-pong: this frame reads what the previous frame wrote and writes the other target,
    // because one texture cannot be sampled and rendered to in the same pass.
    //
    // The parity counter belongs to the RENDER VIEW and advances once per acquisition (exactly
    // one temporal form runs per frame - fused or split). Deriving it from a global frame
    // counter instead assumes that counter steps exactly once per execution -- and if it ever
    // steps by two, the parity never alternates, the read target is never written, and history
    // is unavailable forever. That failure is silent: the pass simply outputs the
    // un-accumulated gather, which is indistinguishable from temporal being disabled.
    auto& parity = rview.data_get_or_emplace("GI_HISTORY_PARITY", 0u);
    const bool even_frame = (parity & 1u) == 0u;
    ++parity;
    const char* write_name = even_frame ? "GI_HISTORY_A" : "GI_HISTORY_B";
    const char* read_name = even_frame ? "GI_HISTORY_B" : "GI_HISTORY_A";
    history_targets history;
    history.write_fbo = create_or_update_target_mrt(rview,
                                                    write_name,
                                                    target_size,
                                                    history.write_tex,
                                                    history.write_moments,
                                                    history.write_fast);
    history.read_tex = rview.tex_safe_get(read_name);
    history.read_moments = rview.tex_safe_get(std::string(read_name) + "_MOMENTS");
    history.read_fast = rview.tex_safe_get(std::string(read_name) + "_FAST");
    // No history on the first frame, after a resize, or without a previous depth to validate
    // against. Signalled to the shader rather than papered over by binding something neutral,
    // because there is no neutral history: whatever is bound gets blended in as if it were real.
    history.has_history = history.read_tex && history.read_moments && history.read_fast &&
                          params.prev_depth &&
                          history.read_tex->get_size().width == target_size.width &&
                          history.read_tex->get_size().height == target_size.height;
    // Report a history that never becomes available. Without accumulation the resolve silently
    // degrades to its un-accumulated gather, which is indistinguishable from temporal being
    // switched off -- so the failure has to announce itself rather than be inferred from noise.
    if(history.has_history)
    {
        frames_without_history_ = 0;
    }
    else if(++frames_without_history_ == history_warning_frames)
    {
        APPLOG_WARNING("GI resolve has had no temporal history for {} frames. read target '{}' {}, "
                       "previous depth {}. Accumulation is disabled until this resolves.",
                       history_warning_frames,
                       read_name,
                       history.read_tex ? "present" : "MISSING",
                       params.prev_depth ? "present" : "MISSING");
    }
    return history;
}

auto gi_resolve_pass::run_temporal(gfx::render_view& rview,
                                   const run_params& params,
                                   const gfx::texture::ptr& current,
                                   const usize32_t& target_size,
                                   gfx::texture::ptr& out_moments) -> gfx::texture::ptr
{
    const auto& s = params.settings;
    const history_targets history = acquire_history(rview, params, target_size);
    const auto& write_fbo = history.write_fbo;
    const auto& write_tex = history.write_tex;
    const auto& read_tex = history.read_tex;
    const auto& read_moments = history.read_moments;
    const bool has_history = history.has_history;
    out_moments = history.write_moments;

    gfx::render_pass temporal_pass("GI/Temporal Pass");
    temporal_pass.bind(write_fbo.get());
    // Jitter-free reconstruction, TAA-unjittered previous pair below: a still camera
    // must reproject onto itself exactly (the reflection chain's measured lesson).
    temporal_pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    temporal_program_.program->begin();
    gfx::set_texture(temporal_program_.s_gi_current, 0, current);
    gfx::set_texture(temporal_program_.s_gi_history, 1, has_history ? read_tex : current);
    gfx::set_texture(temporal_program_.s_gi_depth, 2, params.g_buffer->get_texture(4));
    gfx::set_texture(temporal_program_.s_gi_prev_depth,
                     3,
                     params.prev_depth ? params.prev_depth : params.g_buffer->get_texture(4));
    gfx::set_texture(temporal_program_.s_gi_normal, 4, params.g_buffer->get_texture(1));
    gfx::set_texture(temporal_program_.s_gi_history_moments, 5, has_history ? read_moments : current);
    gfx::set_texture(temporal_program_.s_gi_history_fast,
                     6,
                     has_history ? history.read_fast : current);
    // This frame's velocity buffer, passed explicitly by the pipeline (a valid texture IS
    // the enable). Stage 14, matching the kernel declaration - see the t-register note
    // there; the current gather stands in when absent - the kernel's flag keeps it unread.
    const bool use_velocity = params.velocity != nullptr;
    gfx::set_texture(temporal_program_.s_gi_velocity,
                     14,
                     use_velocity ? params.velocity : current,
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    // get_matrix(), NOT the address of the transform: math::transform is a class with its own
    // members, so handing its address to a mat4 uniform uploads whatever happens to sit in the
    // first 64 bytes. The shader then reprojects to nonsense and rejects every pixel's history,
    // which looks exactly like temporal accumulation that is switched off.
    const auto prev_view_proj = params.cam->get_prev_view_projection_unjittered();
    gfx::set_uniform(temporal_program_.u_gi_prev_view_proj, prev_view_proj.get_matrix());
    const auto prev_inv_view_proj = glm::inverse(prev_view_proj.get_matrix());
    gfx::set_uniform(temporal_program_.u_gi_prev_inv_view_proj, prev_inv_view_proj);
    // Lighting-change-aware slow cap, exactly as the fused form (see the note there): while
    // the light set is hot the slow lane collapses to the FAST cap so a moved source's dim
    // penumbra - invisible to the 3-sigma detector - flushes instead of trailing; instance
    // changes flush region-locally through the dirty bounds.
    const float dirty_margin = params.view_cache->get_clipmap().get_level(0).voxel_size *
                               float(gi::GI_WORLD_PROBE_DIVISOR);
    const bool dirty_overflow = bind_dirty_regions(params, dirty_margin);
    const float slow_cap =
        (dirty_overflow || params.view_cache->get_lighting_quiet_frames() <
                               surface_cache_view::quiescence_settle_frames)
            ? float(gi::GI_TEMPORAL_FAST_FRAMES)
            : s.temporal_slow_frames;
    const float temporal_params[4] = {s.reprojection_tolerance,
                                      float(gi::GI_TEMPORAL_FAST_FRAMES),
                                      slow_cap,
                                      has_history ? 1.0f : 0.0f};
    gfx::set_uniform(temporal_program_.u_gi_temporal_params, temporal_params);
    // No neighbourhood clamp: it fights the placement jitter and eats history under
    // motion; depth rejection is the whole gate [S21 s98].
    const float temporal_clamp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(temporal_program_.u_gi_temporal_clamp, temporal_clamp);
    const float temporal_texel[4] = {1.0f / float(target_size.width),
                                     1.0f / float(target_size.height),
                                     float(target_size.width),
                                     float(target_size.height)};
    gfx::set_uniform(temporal_program_.u_gi_temporal_texel, temporal_texel);
    const auto camera_position = params.cam->get_position();
    const float temporal_camera[4] = {camera_position.x,
                                      camera_position.y,
                                      camera_position.z,
                                      use_velocity ? 1.0f : 0.0f};
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
