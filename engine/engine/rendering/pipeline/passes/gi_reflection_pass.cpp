#include "gi_reflection_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>

#include <graphics/graphics.h>

#include <cmath>

namespace unravel
{

namespace
{
/// RGBA16F target + wrapping fbo at an explicit size, persisted in the render view by name.
auto create_or_update_target(gfx::render_view& rview,
                             const std::string& name,
                             const usize32_t& size,
                             gfx::texture::ptr& out_tex,
                             bool& out_created) -> gfx::frame_buffer::ptr
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
                                             BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
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

auto gi_reflection_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_reflection = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_reflection.sc");
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_reflection);
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
    auto raw_fbo = create_or_update_target(rview, "GI_REFL_RAW", trace_size, raw_tex, raw_created);
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
    gfx::set_texture(program_.s_hiz, 8, params.hiz);
    const bool has_gi_diffuse = params.gi_diffuse != nullptr;
    gfx::set_texture(program_.s_gi_diffuse,
                     9,
                     has_gi_diffuse ? params.gi_diffuse : default_textures::get().black_texture());
    gfx::set_texture(program_.s_light_voxels, 10, clipmap_gpu.get_light_voxel_texture());
    gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
    gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
    gfx::set_texture(program_.s_gi_env_sh,
                     14,
                     params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture());
    const auto camera_position = params.cam->get_position();
    const float reflection_camera[4] = {camera_position.x,
                                        camera_position.y,
                                        camera_position.z,
                                        has_gi_diffuse ? 1.0f : 0.0f};
    gfx::set_uniform(program_.u_gi_reflection_camera, reflection_camera);
    // R2 low-discrepancy sequence advancing per frame; the shader decorrelates per pixel.
    const double frame_index = double(gfx::get_render_frame());
    const float jitter[4] = {float(std::fmod(0.754877666 * frame_index, 1.0)),
                             float(std::fmod(0.569840291 * frame_index, 1.0)),
                             0.0f,
                             0.0f};
    gfx::set_uniform(program_.u_gi_reflection_jitter, jitter);
    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(surface_cache.get_instances().size()),
                                 0.0f};
    gfx::set_uniform(program_.u_sdf_params, sdf_params);
    gfx::set_uniform(program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
    gfx::set_uniform(program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    gfx::set_uniform(program_.u_sdf_clipmap_levels,
                     clipmap_gpu.get_level_params(),
                     global_sdf_clipmap::level_count);
    const float light_voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 1.0f};
    gfx::set_uniform(program_.u_gi_light_voxel_params, light_voxel_params);
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    program_.program->end();
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
        const float temporal_params[4] = {history_valid ? 1.0f : 0.0f,
                                          1.0f / float(trace_size.width),
                                          1.0f / float(trace_size.height),
                                          float(params.temporal_frames)};
        gfx::set_uniform(temporal_program_.u_gi_refl_temporal, temporal_params);
        // clip_quad() stages a transient vertex buffer consumed by ONE submit - every draw
        // needs its own call (reusing the trace's quad left this submit with no vertices).
        gfx::set_state(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(tpass.id, temporal_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        temporal_program_.program->end();
    }
    // COMPOSITE: full-weight OVER the authored probe layer (energy constant across
    // roughness). SSR composites the sharp on-screen result on top afterwards.
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
        gfx::set_state(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
        gfx::submit(cpass.id, composite_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        composite_program_.program->end();
    }
    gfx::discard();
    return true;
}

} // namespace unravel
