#include "gi_probe_volume.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

#include <algorithm>
#include <cmath>

namespace unravel
{

auto gi_probe_volume::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto cs_update = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_gi_probe_update.sc");
    auto fs_sample = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/fs_gi_probe_sample.sc");
    update_program_.program = std::make_unique<gpu_program>(cs_update);
    update_program_.cache_uniforms();
    sample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_sample);
    sample_program_.cache_uniforms();
    return update_program_.program && update_program_.program->is_valid() && sample_program_.program &&
           sample_program_.program->is_valid();
}

void gi_probe_volume::release()
{
    probe_tex_.reset();
    update_program_.program.reset();
    sample_program_.program.reset();
}

void gi_probe_volume::request_soft_reset(int frames)
{
    sun_soft_reset_frames_ = std::max(sun_soft_reset_frames_, std::max(frames, 1));
}

void gi_probe_volume::ensure_probe_texture()
{
    if(probe_tex_)
    {
        return;
    }
    const uint16_t width = uint16_t(std::max(NEAR_SIZE_X * NEAR_SIZE_Z, FAR_SIZE_X * FAR_SIZE_Z));
    const uint16_t height = uint16_t(NEAR_SIZE_Y + FAR_SIZE_Y);
    probe_tex_ = std::make_shared<gfx::texture>(width,
                                                height,
                                                false,
                                                1,
                                                gfx::texture_format::RGBA16F,
                                                BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                    BGFX_SAMPLER_V_CLAMP);
}

namespace
{
auto snap_to_cell(float value, float cell) -> float
{
    const float c = std::max(cell, 1e-3f);
    return std::floor(value / c) * c;
}

auto snap_vec(const math::vec3& value, const math::vec3& cell) -> math::vec3
{
    return math::vec3(snap_to_cell(value.x, cell.x),
                      snap_to_cell(value.y, cell.y),
                      snap_to_cell(value.z, cell.z));
}
} // namespace

auto gi_probe_volume::upload_grid_uniforms(const math::vec3& camera_position, const settings& cfg) -> bool
{
    // Lumen-style final gather: cascades follow the camera (world-snapped).
    // Surface-cache cards stay volume/mesh resident separately — do NOT lock
    // probes to the GI volume center (that killed IL at Bistro outskirts and
    // made local-volume GI translate with the mesh).
    const float far_ext = std::max(cfg.far_extent, 48.0f);
    const float near_ext = std::clamp(cfg.near_extent, 8.0f, far_ext * 0.55f);
    near_spacing_ = math::vec3(near_ext / float(std::max(NEAR_SIZE_X - 1u, 1u)),
                               near_ext * 0.55f / float(std::max(NEAR_SIZE_Y - 1u, 1u)),
                               near_ext / float(std::max(NEAR_SIZE_Z - 1u, 1u)));
    far_spacing_ = math::vec3(far_ext / float(std::max(FAR_SIZE_X - 1u, 1u)),
                              far_ext * 0.45f / float(std::max(FAR_SIZE_Y - 1u, 1u)),
                              far_ext / float(std::max(FAR_SIZE_Z - 1u, 1u)));
    const math::vec3 desired = snap_vec(camera_position, far_spacing_);
    // Large deadzone: rebase only after ~2 far cells so walk/look stays stable.
    const float rebase_dist = far_spacing_.x * 2.0f;
    if(!has_grid_anchor_)
    {
        camera_anchor_ = desired;
        has_grid_anchor_ = true;
    }
    else if(math::length(desired - camera_anchor_) > rebase_dist)
    {
        camera_anchor_ = desired;
    }
    const math::vec3 near_anchor = snap_vec(camera_anchor_, near_spacing_);
    const math::vec3 far_anchor = snap_vec(camera_anchor_, far_spacing_);
    near_origin_ = near_anchor - 0.5f * math::vec3(float(NEAR_SIZE_X - 1u) * near_spacing_.x,
                                                   float(NEAR_SIZE_Y - 1u) * near_spacing_.y,
                                                   float(NEAR_SIZE_Z - 1u) * near_spacing_.z);
    far_origin_ = far_anchor - 0.5f * math::vec3(float(FAR_SIZE_X - 1u) * far_spacing_.x,
                                                  float(FAR_SIZE_Y - 1u) * far_spacing_.y,
                                                  float(FAR_SIZE_Z - 1u) * far_spacing_.z);
    bool rebased = false;
    if(has_grid_anchor_ && frame_index_ > 0 &&
       (near_origin_ != last_near_origin_ || far_origin_ != last_far_origin_))
    {
        rebased = true;
    }
    last_near_origin_ = near_origin_;
    last_far_origin_ = far_origin_;
    return rebased;
}

auto gi_probe_volume::create_or_update_output(gfx::render_view& rview, const usize32_t& size) -> gfx::texture::ptr
{
    auto& tex = rview.tex_get_or_emplace("SURFACE_CACHE_GI");
    if(gfx::needs_recreate(tex, size))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(uint16_t(size.width),
                                             uint16_t(size.height),
                                             false,
                                             1,
                                             gfx::texture_format::RGBA16F,
                                             BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }
    auto& fbo = rview.fbo_get_or_emplace("SURFACE_CACHE_GI");
    if(gfx::needs_recreate(fbo, size))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }
    return tex;
}

void gi_probe_volume::update_world(gfx::render_view& rview, const update_params& params)
{
    APP_SCOPE_PERF("Rendering/GI Probes/Update");
    (void)rview;
    if(!params.cam || !params.atlas || !params.cards || !update_program_.program ||
       !update_program_.program->is_valid())
    {
        return;
    }
    ensure_probe_texture();
    const bool rebased = upload_grid_uniforms(params.cam->get_position(), params.cfg);
    if(rebased)
    {
        // Soft refill — keep history high; never dump the whole grid in one frame.
        rebase_boost_frames_ = 3;
        update_cursor_ = 0;
    }
    ++frame_index_;
    int budget = std::clamp(params.cfg.probes_per_frame, 1, int(TOTAL_PROBES));
    float history = std::clamp(params.cfg.probe_history, 0.80f, 0.95f);
    if(rebase_boost_frames_ > 0)
    {
        budget = std::min(int(TOTAL_PROBES), std::max(budget * 2, 200));
        history = std::max(history, 0.92f);
        --rebase_boost_frames_;
    }
    if(params.cfg.sun_soft_reset_frames > 0)
    {
        sun_soft_reset_frames_ = std::max(sun_soft_reset_frames_, params.cfg.sun_soft_reset_frames);
    }
    if(sun_soft_reset_frames_ > 0)
    {
        // Accept new lighting quickly after sun rotate — avoid sticky wrong IL.
        history = std::min(history, 0.35f);
        budget = std::min(int(TOTAL_PROBES), std::max(budget * 2, 256));
        --sun_soft_reset_frames_;
    }
    const uint32_t start = update_cursor_;
    update_cursor_ = (update_cursor_ + uint32_t(budget)) % TOTAL_PROBES;
    float p0[4] = {float(params.card_count),
                   params.page_uv_size,
                   params.card_thickness,
                   params.cfg.gather_intensity};
    float p1[4] = {params.cfg.gather_distance,
                   history,
                   params.cfg.seed_with_skylight ? 1.0f : 0.0f,
                   float(start)};
    float p2[4] = {float(budget), float(NEAR_COUNT), float(TOTAL_PROBES), params.cfg.cache_blend};
    float on[4] = {near_origin_.x, near_origin_.y, near_origin_.z, 0.0f};
    float sn[4] = {near_spacing_.x, near_spacing_.y, near_spacing_.z, 0.0f};
    float of[4] = {far_origin_.x, far_origin_.y, far_origin_.z, 0.0f};
    float sf[4] = {far_spacing_.x, far_spacing_.y, far_spacing_.z, 0.0f};
    float op0[4] = {params.opacity_origin.x,
                    params.opacity_origin.y,
                    params.opacity_origin.z,
                    params.opacity_voxel_size};
    float op1[4] = {params.opacity_dims.x,
                    params.opacity_dims.y,
                    params.opacity_dims.z,
                    params.opacity_enabled ? 1.0f : 0.0f};
    gfx::render_pass pass("GI Probes/Update");
    pass.bind();
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    update_program_.program->begin();
    gfx::set_uniform(update_program_.u_probe_params0, p0);
    gfx::set_uniform(update_program_.u_probe_params1, p1);
    gfx::set_uniform(update_program_.u_probe_params2, p2);
    gfx::set_uniform(update_program_.u_probe_origin_near, on);
    gfx::set_uniform(update_program_.u_probe_spacing_near, sn);
    gfx::set_uniform(update_program_.u_probe_origin_far, of);
    gfx::set_uniform(update_program_.u_probe_spacing_far, sf);
    gfx::set_uniform(update_program_.u_opacity_params0, op0);
    gfx::set_uniform(update_program_.u_opacity_params1, op1);
    gfx::set_image(0, params.atlas->native_handle(), 0, bgfx::Access::Read);
    gfx::set_texture(update_program_.s_cards, 1, params.cards);
    gfx::set_texture(update_program_.s_irradiance,
                     2,
                     params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture());
    gfx::set_image(3, probe_tex_->native_handle(), 0, bgfx::Access::ReadWrite);
    if(params.opacity_volume)
    {
        gfx::set_image(4, params.opacity_volume->native_handle(), 0, bgfx::Access::Read);
    }
    const uint16_t groups = uint16_t((budget + 63) / 64);
    bgfx::dispatch(pass.id, update_program_.program->native_handle(), groups, 1, 1);
    update_program_.program->end();
    gfx::discard();
}

auto gi_probe_volume::sample_frame(gfx::render_view& rview, const sample_params& params) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/GI Probes/Sample");
    if(!params.g_buffer || !params.cam || !sample_program_.program || !sample_program_.program->is_valid())
    {
        return {};
    }
    ensure_probe_texture();
    upload_grid_uniforms(params.cam->get_position(), params.cfg);
    const auto size = params.g_buffer->get_size();
    auto output = create_or_update_output(rview, size);
    auto& output_fbo = rview.fbo_get("SURFACE_CACHE_GI");
    const float far_ext = std::max(params.cfg.far_extent, 48.0f);
    const float near_ext = std::clamp(params.cfg.near_extent, 8.0f, far_ext * 0.55f);
    float p0[4] = {params.cfg.cache_blend,
                   params.cfg.seed_with_skylight ? 1.0f : 0.0f,
                   near_ext,
                   far_ext};
    const float tex_w = float(std::max(NEAR_SIZE_X * NEAR_SIZE_Z, FAR_SIZE_X * FAR_SIZE_Z));
    const float tex_h = float(NEAR_SIZE_Y + FAR_SIZE_Y);
    const uint32_t card_count = std::min(params.card_count, PROBE_GATHER_CARDS);
    float p1[4] = {tex_w, tex_h, float(card_count), params.page_uv_size};
    float p2[4] = {params.card_thickness,
                   params.cfg.gather_distance,
                   params.cfg.gather_intensity,
                   0.0f};
    float on[4] = {near_origin_.x, near_origin_.y, near_origin_.z, 0.0f};
    float sn[4] = {near_spacing_.x, near_spacing_.y, near_spacing_.z, 0.0f};
    float of[4] = {far_origin_.x, far_origin_.y, far_origin_.z, 0.0f};
    float sf[4] = {far_spacing_.x, far_spacing_.y, far_spacing_.z, 0.0f};
    const math::vec3 cam_pos = params.cam->get_position();
    float cam4[4] = {cam_pos.x, cam_pos.y, cam_pos.z, 0.0f};
    gfx::render_pass pass("GI Probes/Sample");
    pass.bind(output_fbo.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);
    sample_program_.program->begin();
    gfx::set_uniform(sample_program_.u_probe_params0, p0);
    gfx::set_uniform(sample_program_.u_probe_params1, p1);
    gfx::set_uniform(sample_program_.u_probe_params2, p2);
    gfx::set_uniform(sample_program_.u_probe_origin_near, on);
    gfx::set_uniform(sample_program_.u_probe_spacing_near, sn);
    gfx::set_uniform(sample_program_.u_probe_origin_far, of);
    gfx::set_uniform(sample_program_.u_probe_spacing_far, sf);
    gfx::set_uniform(sample_program_.u_camera_position, cam4);
    gfx::set_texture(sample_program_.s_gbuffer0, 0, params.g_buffer->get_texture(0));
    gfx::set_texture(sample_program_.s_gbuffer1, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(sample_program_.s_gbuffer4, 2, params.g_buffer->get_texture(4));
    gfx::set_texture(sample_program_.s_probes, 3, probe_tex_);
    gfx::set_texture(sample_program_.s_irradiance,
                     4,
                     params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture());
    gfx::set_texture(sample_program_.s_atlas,
                     5,
                     params.atlas_srv ? params.atlas_srv : default_textures::get().black_texture());
    gfx::set_texture(sample_program_.s_cards,
                     6,
                     params.cards ? params.cards : default_textures::get().black_texture());
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, sample_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    sample_program_.program->end();
    gfx::discard();
    rview.tex_get_or_emplace("GI_PROBE_VOLUME") = probe_tex_;
    return output;
}

} // namespace unravel
