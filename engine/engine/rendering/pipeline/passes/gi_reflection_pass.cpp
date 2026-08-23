#include "gi_reflection_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>

#include <graphics/graphics.h>
#include <logging/logging.h>

#include <cmath>

namespace unravel
{

namespace
{
/// RGBA16F target + wrapping fbo at an explicit size, persisted in the render view by name.
/// compute_write additionally opens the texture for image stores (the compute trace chain
/// writes RAW as an image; the target stays an RT for the fragment fallback).
auto create_or_update_target(gfx::render_view& rview,
                             const std::string& name,
                             const usize32_t& size,
                             gfx::texture::ptr& out_tex,
                             bool& out_created,
                             bool compute_write = false) -> gfx::frame_buffer::ptr
{
    auto& tex = rview.tex_get_or_emplace(name);
    out_created = false;
    if(gfx::needs_recreate(tex, size))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(size.width,
                                             size.height,
                                             false,
                                             1,
                                             gfx::texture_format::RGBA16F,
                                             BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                                 (compute_write ? BGFX_TEXTURE_COMPUTE_WRITE : 0));
        out_created = true;
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
} // namespace

gi_reflection_pass::~gi_reflection_pass()
{
    if(bgfx::isValid(refl_list_))
    {
        gfx::destroy(refl_list_);
        refl_list_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(refl_args_))
    {
        gfx::destroy(refl_args_);
        refl_args_ = {bgfx::kInvalidHandle};
    }
}

auto gi_reflection_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_reflection = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_reflection.sc");
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_reflection);
    // The deliverable trace path: classify -> indirect args -> compacted 64-lane trace.
    auto cs_classify = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_reflection_classify.sc");
    classify_program_.cache_uniforms();
    classify_program_.program = std::make_unique<gpu_program>(cs_classify);
    auto cs_args = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_reflection_args.sc");
    args_program_.cache_uniforms();
    args_program_.program = std::make_unique<gpu_program>(cs_args);
    auto cs_trace = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_reflection_trace.sc");
    trace_program_.cache_uniforms();
    trace_program_.program = std::make_unique<gpu_program>(cs_trace);
    if(!classify_program_.is_valid() || !args_program_.is_valid() || !trace_program_.is_valid())
    {
        APPLOG_WARNING("[SurfaceCache] GI reflection compute chain failed to load. Falling back "
                       "to the fragment trace (whole waves stall on single tracing pixels).");
    }
    auto fs_temporal = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_reflection_temporal.sc");
    temporal_program_.cache_uniforms();
    temporal_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_temporal);
    auto fs_composite = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_reflection_composite.sc");
    composite_program_.cache_uniforms();
    composite_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_composite);
    return true;
}

auto gi_reflection_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    (void)rview;
    if(!program_.is_valid() || !temporal_program_.is_valid() || !composite_program_.is_valid() ||
       !params.output || !params.g_buffer || !params.hiz || !params.cam ||
       !params.surface_cache || !params.view_cache)
    {
        return false;
    }
    auto& surface_cache = *params.surface_cache;
    if(!surface_cache.is_enabled())
    {
        return false;
    }
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    if(!clipmap_gpu.is_valid() || !clipmap_gpu.has_world_probes())
    {
        return false;
    }
    APP_SCOPE_PERF("Rendering/GI/Reflections");
    auto& atlas = surface_cache.get_atlas();
    // Stochastic pipeline: trace one GGX-jittered ray into RAW, integrate into the ping-pong
    // accumulation pair, composite the integrated result over the probe layer.
    gfx::texture::ptr raw_tex;
    gfx::texture::ptr read_tex;
    gfx::texture::ptr write_tex;
    bool raw_created = false;
    bool read_created = false;
    bool write_created = false;
    // Trace + accumulation targets at the gather's shared trace resolution: the two expensive
    // stages shrink by the divisor squared, and the composite's edge-stopped kernel below
    // reconstructs full resolution as a joint bilateral upsample. Normalised uvs make the
    // shaders resolution-agnostic.
    const auto full_size = params.output->get_size();
    const usize32_t trace_size = compute_trace_size(full_size, params.resolution);
    // The compute chain is the deliverable path; the fragment program is the fallback when
    // any of its three programs failed to load. RAW opens for image stores accordingly.
    const bool compute_trace =
        classify_program_.is_valid() && args_program_.is_valid() && trace_program_.is_valid();
    auto raw_fbo =
        create_or_update_target(rview, "GI_REFL_RAW", trace_size, raw_tex, raw_created, compute_trace);
    const bool odd_frame = (gfx::get_render_frame() & 1u) != 0u;
    auto read_fbo = create_or_update_target(rview,
                                            odd_frame ? "GI_REFL_ACC_A" : "GI_REFL_ACC_B",
                                            trace_size,
                                            read_tex,
                                            read_created);
    (void)read_fbo;
    auto write_fbo = create_or_update_target(rview,
                                             odd_frame ? "GI_REFL_ACC_B" : "GI_REFL_ACC_A",
                                             trace_size,
                                             write_tex,
                                             write_created);
    // Only the READ texture's content matters for validity - RAW and WRITE are fully
    // overwritten this frame before anything samples them.
    (void)raw_created;
    (void)write_created;
    const bool history_valid = !read_created && params.temporal_frames > 1;
    // Uniform data shared by both trace forms.
    const auto camera_position = params.cam->get_position();
    const bool has_gi_diffuse = params.gi_diffuse != nullptr;
    const float reflection_camera[4] = {camera_position.x,
                                        camera_position.y,
                                        camera_position.z,
                                        has_gi_diffuse ? 1.0f : 0.0f};
    // R2 low-discrepancy sequence advancing per frame; the shader decorrelates per pixel.
    // zw arm the checkerboard: it needs a live temporal window to fill the untraced half.
    const double frame_index = double(gfx::get_render_frame());
    const bool checkerboard = params.checkerboard && params.temporal_frames > 1;
    const float frame_parity = float(gfx::get_render_frame() & 1u);
    const float jitter[4] = {float(std::fmod(0.754877666 * frame_index, 1.0)),
                             float(std::fmod(0.569840291 * frame_index, 1.0)),
                             checkerboard ? 1.0f : 0.0f,
                             frame_parity};
    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(surface_cache.get_instances().size()),
                                 0.0f};
    const float light_voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 1.0f};
    const float refl_texel[4] = {1.0f / float(trace_size.width),
                                 1.0f / float(trace_size.height),
                                 float(trace_size.width),
                                 float(trace_size.height)};
    // Transparent black (alpha 0) when the probe layer is absent collapses the shader's
    // mix(sky_sh, probe, probe.a) to the SH - the opaque-black default would answer every
    // sky miss with black instead.
    const auto probe_layer_tex =
        params.probe_layer ? params.probe_layer : default_textures::get().transparent_texture();
    const auto gi_diffuse_tex =
        has_gi_diffuse ? params.gi_diffuse : default_textures::get().black_texture();
    const auto env_sh_tex =
        params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture();
    if(compute_trace)
    {
        // CLASSIFY -> ARGS -> compacted indirect TRACE: classify answers sky / degenerate /
        // rough texels straight into RAW and appends the tracing texels to a dense list, so
        // every 64-lane trace group is fully populated with rays. The fragment form paid a
        // whole wave wherever one quad pixel traced, and its worst-case register footprint
        // throttled even the early-out pixels.
        const uint32_t required_indices = 2u + trace_size.width * trace_size.height;
        bool list_created = false;
        if(!bgfx::isValid(refl_list_) || required_indices > refl_list_capacity_)
        {
            if(bgfx::isValid(refl_list_))
            {
                gfx::destroy(refl_list_);
            }
            refl_list_capacity_ = required_indices;
            refl_list_ = gfx::create_dynamic_index_buffer(refl_list_capacity_,
                                                          BGFX_BUFFER_COMPUTE_READ_WRITE |
                                                              BGFX_BUFFER_INDEX32);
            list_created = true;
        }
        if(!bgfx::isValid(refl_args_))
        {
            refl_args_ = gfx::create_indirect_buffer(1);
        }
        if(list_created)
        {
            // Fresh list memory is garbage and the append cursor's reset lives at the END of
            // the chain (in the args pass, downstream of the cursor's last reader) - run the
            // args program once ahead of the first classify so the cursor starts defined.
            // Its other outputs are overwritten by the real args dispatch below before the
            // trace consumes them. (A CPU update cannot do this: bgfx forbids updating
            // compute-writable dynamic buffers.)
            gfx::render_pass pass("GI/Reflections List Init");
            args_program_.program->begin();
            gfx::set_buffer(0, refl_args_, gfx::access::Write);
            gfx::set_buffer(1, refl_list_, gfx::access::ReadWrite);
            gfx::dispatch(pass.id, args_program_.program->native_handle(), 1, 1, 1);
            args_program_.program->end();
        }
        {
            gfx::render_pass pass("GI/Reflections Classify");
            // The rough tier's SH fallback reconstructs world positions from depth.
            pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
            classify_program_.program->begin();
            gfx::set_texture(classify_program_.s_hiz, 0, params.hiz);
            gfx::set_texture(classify_program_.s_gi_normal, 1, params.g_buffer->get_texture(1));
            gfx::set_texture(classify_program_.s_gi_diffuse, 2, gi_diffuse_tex);
            gfx::set_texture(classify_program_.s_gi_env_sh, 3, env_sh_tex);
            gfx::set_image(4, raw_tex->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
            gfx::set_buffer(5, refl_list_, gfx::access::ReadWrite);
            gfx::set_uniform(classify_program_.u_gi_reflection_camera, reflection_camera);
            gfx::set_uniform(classify_program_.u_gi_reflection_jitter, jitter);
            gfx::set_uniform(classify_program_.u_gi_reflection_texel, refl_texel);
            gfx::dispatch(pass.id,
                          classify_program_.program->native_handle(),
                          (trace_size.width + 7u) / 8u,
                          (trace_size.height + 7u) / 8u,
                          1);
            classify_program_.program->end();
        }
        {
            gfx::render_pass pass("GI/Reflections Args");
            args_program_.program->begin();
            gfx::set_buffer(0, refl_args_, gfx::access::Write);
            gfx::set_buffer(1, refl_list_, gfx::access::ReadWrite);
            gfx::dispatch(pass.id, args_program_.program->native_handle(), 1, 1, 1);
            args_program_.program->end();
        }
        {
            gfx::render_pass pass("GI/Reflections Trace");
            // World positions reconstruct from depth, and the Hi-Z projection needs the
            // view state.
            pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
            trace_program_.program->begin();
            gfx::set_texture(trace_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
            gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
            gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
            gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
            gfx::set_texture(trace_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
            gfx::set_texture(trace_program_.s_gi_normal, 5, params.g_buffer->get_texture(1));
            gfx::set_texture(trace_program_.s_gi_probe_layer, 6, probe_layer_tex);
            // Stage 7 is the output image (OpenGL has eight image units); the list sits at 15.
            gfx::set_image(7, raw_tex->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
            gfx::set_texture(trace_program_.s_hiz, 8, params.hiz);
            gfx::set_texture(trace_program_.s_gi_diffuse, 9, gi_diffuse_tex);
            gfx::set_texture(trace_program_.s_light_voxels, 10, clipmap_gpu.get_light_voxel_texture());
            gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
            gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
            gfx::set_texture(trace_program_.s_gi_env_sh, 14, env_sh_tex);
            gfx::set_buffer(15, refl_list_, gfx::access::Read);
            gfx::set_uniform(trace_program_.u_gi_reflection_camera, reflection_camera);
            gfx::set_uniform(trace_program_.u_gi_reflection_jitter, jitter);
            gfx::set_uniform(trace_program_.u_gi_reflection_texel, refl_texel);
            gfx::set_uniform(trace_program_.u_sdf_params, sdf_params);
            gfx::set_uniform(trace_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
            gfx::set_uniform(trace_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
            gfx::set_uniform(trace_program_.u_sdf_clipmap_levels,
                             clipmap_gpu.get_level_params(),
                             global_sdf_clipmap::level_count);
            gfx::set_uniform(trace_program_.u_gi_light_voxel_params, light_voxel_params);
            gfx::dispatch_indirect(pass.id, trace_program_.program->native_handle(), refl_args_, 0, 1);
            trace_program_.program->end();
        }
    }
    else
    {
        gfx::render_pass pass("GI/Reflections Trace");
        pass.bind(raw_fbo.get());
        // World positions reconstruct from depth, and the Hi-Z projection needs the view state.
        pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
        program_.program->begin();
        gfx::set_texture(program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
        gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
        gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
        gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
        gfx::set_texture(program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
        gfx::set_texture(program_.s_gi_normal, 5, params.g_buffer->get_texture(1));
        gfx::set_texture(program_.s_gi_probe_layer, 6, probe_layer_tex);
        gfx::set_texture(program_.s_hiz, 8, params.hiz);
        gfx::set_texture(program_.s_gi_diffuse, 9, gi_diffuse_tex);
        gfx::set_texture(program_.s_light_voxels, 10, clipmap_gpu.get_light_voxel_texture());
        gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
        gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
        gfx::set_texture(program_.s_gi_env_sh, 14, env_sh_tex);
        gfx::set_uniform(program_.u_gi_reflection_camera, reflection_camera);
        gfx::set_uniform(program_.u_gi_reflection_jitter, jitter);
        gfx::set_uniform(program_.u_sdf_params, sdf_params);
        gfx::set_uniform(program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
        gfx::set_uniform(program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        gfx::set_uniform(program_.u_sdf_clipmap_levels,
                         clipmap_gpu.get_level_params(),
                         global_sdf_clipmap::level_count);
        gfx::set_uniform(program_.u_gi_light_voxel_params, light_voxel_params);
        // Fullscreen triangle, not a quad: the quad's diagonal produces partially covered
        // 2x2 quads whose helper lanes run the full trace (ssil/ssr already switched).
        auto topology = gfx::clip_fullscreen_triangle(1.0f);
        if(topology == 0)
        {
            topology = gfx::clip_quad(1.0f);
        }
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(pass.id, program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        program_.program->end();
    }
    gfx::discard();
    // TEMPORAL: integrate this frame's stochastic sample into the reprojected running mean.
    {
        gfx::render_pass tpass("GI/Reflections Temporal");
        tpass.bind(write_fbo.get());
        tpass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
        temporal_program_.program->begin();
        gfx::set_texture(temporal_program_.s_refl_raw, 0, raw_tex);
        gfx::set_texture(temporal_program_.s_refl_history, 1, history_valid ? read_tex : raw_tex);
        gfx::set_texture(temporal_program_.s_refl_depth, 2, params.hiz);
        auto prev_view_proj = params.cam->get_prev_view_projection();
        gfx::set_uniform(temporal_program_.u_gi_refl_prev_view_proj, prev_view_proj.get_matrix());
        // x packs three exact small integers: +1 history valid, +2 checkerboard, +4 parity.
        const float temporal_flags = (history_valid ? 1.0f : 0.0f) + (checkerboard ? 2.0f : 0.0f) +
                                     frame_parity * 4.0f;
        const float temporal_params[4] = {temporal_flags,
                                          1.0f / float(trace_size.width),
                                          1.0f / float(trace_size.height),
                                          float(params.temporal_frames)};
        gfx::set_uniform(temporal_program_.u_gi_refl_temporal, temporal_params);
        // The topology helpers stage a transient vertex buffer consumed by ONE submit - every
        // draw needs its own call (reusing the trace's left this submit with no vertices).
        auto ttopology = gfx::clip_fullscreen_triangle(1.0f);
        if(ttopology == 0)
        {
            ttopology = gfx::clip_quad(1.0f);
        }
        gfx::set_state(ttopology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(tpass.id, temporal_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        temporal_program_.program->end();
    }
    // COMPOSITE: src-alpha OVER the authored probe layer. Coverage is 1 for mesh-exact
    // / refined hits and 0 for an unrefined clipmap on a sharp pixel, so probes remain
    // the far-field image where the clipmap isosurface would be a wrong silhouette.
    // SSR composites the sharp on-screen result on top afterwards.
    {
        gfx::render_pass cpass("GI/Reflections Composite");
        cpass.bind(params.output.get());
        composite_program_.program->begin();
        gfx::set_texture(composite_program_.s_refl_acc, 0, write_tex);
        gfx::set_texture(composite_program_.s_gi_normal, 1, params.g_buffer->get_texture(1));
        gfx::set_texture(composite_program_.s_hiz, 2, params.hiz);
        // Offsets measure ACCUMULATION texels (equal to output texels at full res): at half
        // res the composite's edge-stopped 3x3 becomes a joint bilateral upsample for free.
        const float composite_params[4] = {1.0f / float(trace_size.width),
                                           1.0f / float(trace_size.height),
                                           0.0f,
                                           0.0f};
        gfx::set_uniform(composite_program_.u_gi_refl_composite, composite_params);
        auto ctopology = gfx::clip_fullscreen_triangle(1.0f);
        if(ctopology == 0)
        {
            ctopology = gfx::clip_quad(1.0f);
        }
        gfx::set_state(ctopology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
        gfx::submit(cpass.id, composite_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        composite_program_.program->end();
    }
    gfx::discard();
    // A FULL-RESOLUTION mirror tier lived here briefly (capped compacted list re-traced at
    // output res over the composite) and was REMOVED on the user's verdict: +0.6 ms at FHD
    // for fidelity that did not read, plus artifacts - the sharp trace ran after the
    // composite and bound RBUFFER both as its sky-fallback sampler and as its RW output
    // image in one dispatch, a read-write alias that is undefined on every backend. If it
    // returns, the sky fallback needs a pre-composite copy of the probe layer, and the
    // half-res classify should exclude the pixels the tier will overwrite.
    return true;
}

} // namespace unravel
