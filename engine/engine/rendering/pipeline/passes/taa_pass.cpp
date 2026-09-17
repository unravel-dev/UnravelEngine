#include "taa_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

#include <bgfx/bgfx.h>

#include <cmath>

namespace unravel
{
namespace
{
/// Avoid WRAP at RT edges when sampling history / scene color in TAA (reduces border streaks).
constexpr std::uint32_t k_taa_sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
/// Largest per-element difference between this frame's and last frame's unjittered view-projection that still
/// counts as a PARKED camera (a parked camera rebuilds bit-identical matrices; this only absorbs float noise).
constexpr float k_taa_parked_matrix_epsilon = 1e-5f;

/// True when the camera did not move since last frame: its unjittered view-projections match per element.
auto is_camera_parked(const camera& cam) -> bool
{
    const auto current = cam.get_view_projection_unjittered();
    const auto previous = cam.get_prev_view_projection_unjittered();
    const auto& current_matrix = current.get_matrix();
    const auto& previous_matrix = previous.get_matrix();
    for(int column = 0; column < 4; ++column)
    {
        for(int row = 0; row < 4; ++row)
        {
            if(std::abs(current_matrix[column][row] - previous_matrix[column][row]) > k_taa_parked_matrix_epsilon)
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace

auto taa_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs = am.get_asset<gfx::shader>("engine:/data/shaders/taa/fs_taa.sc");
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(vs, fs);
    return program_.program->is_valid();
}

auto taa_pass::create_or_update_history_tex(gfx::render_view& rview,
                                            const gfx::frame_buffer::ptr& reference_color) -> gfx::texture::ptr
{
    const auto sz = reference_color->get_size();
    const auto fmt = reference_color->get_texture(0)->info.format;
    auto& history_tex = rview.tex_get_or_emplace("TAA_HISTORY");
    if(gfx::needs_recreate(history_tex, sz, fmt))
    {
        history_tex.reset();
        history_tex = std::make_shared<gfx::texture>(sz.width,
                                                     sz.height,
                                                     false,
                                                     1,
                                                     fmt,
                                                     BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST);
    }
    return history_tex;
}

auto taa_pass::create_or_update_temp_fb(gfx::render_view& rview,
                                        const gfx::frame_buffer::ptr& reference_color) -> gfx::frame_buffer::ptr
{
    const auto sz = reference_color->get_size();
    const auto fmt = reference_color->get_texture(0)->info.format;
    // Attachment 0 is the displayed resolve (sharpened), attachment 1 the history resolve (never
    // sharpened): the sharpen must stay out of the history feedback loop (fs_taa.sc).
    bool is_recreated = false;
    for(const char* id : {"TAA_TEMP", "TAA_TEMP_HISTORY"})
    {
        auto& tex = rview.tex_get_or_emplace(id);
        if(gfx::needs_recreate(tex, sz, fmt))
        {
            tex.reset();
            tex = std::make_shared<gfx::texture>(sz.width, sz.height, false, 1, fmt, BGFX_TEXTURE_RT);
            is_recreated = true;
        }
    }
    auto& fbo = rview.fbo_get_or_emplace("TAA_TEMP");
    if(is_recreated || gfx::needs_recreate(fbo, sz))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({rview.tex_safe_get("TAA_TEMP"), rview.tex_safe_get("TAA_TEMP_HISTORY")});
    }
    return fbo;
}

auto taa_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    const auto& input = params.input;
    if(!program_.program || !program_.program->is_valid() || !input || !params.cam || !params.g_buffer)
    {
        return input;
    }

    auto old_history = rview.tex_safe_get("TAA_HISTORY");
    auto history_tex = create_or_update_history_tex(rview, input);
    auto temp_fbo = create_or_update_temp_fb(rview, input);

    if(history_tex != old_history)
    {
        gfx::render_pass init_pass("TAA/History Init");
        gfx::blit(init_pass.id,
                  history_tex->native_handle(),
                  0,
                  0,
                  input->get_texture(0)->native_handle(),
                  0,
                  0);
        return input;
    }

    APP_SCOPE_PERF("Rendering/TAA Pass");

    gfx::render_pass pass("TAA/Resolve Pass");
    pass.bind(temp_fbo.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());

    if(!program_.program->begin())
    {
        return input;
    }

    gfx::set_texture(program_.s_curr, 0, input->get_texture(0), k_taa_sampler_flags);
    gfx::set_texture(program_.s_history, 1, history_tex, k_taa_sampler_flags);
    gfx::set_texture(program_.s_depth, 2, params.g_buffer->get_texture(4), k_taa_sampler_flags);
    gfx::set_texture(program_.s_prev_depth,
                     3,
                     params.prev_depth ? params.prev_depth : params.g_buffer->get_texture(4),
                     k_taa_sampler_flags);
    // Reproject through the velocity buffer when the pipeline passed one; a valid texture
    // IS the enable. The depth texture stands in as an inert placeholder so the sampler
    // slot is always bound (never read with the flag at 0).
    const bool use_velocity = params.velocity != nullptr;
    gfx::set_texture(program_.s_velocity,
                     4,
                     use_velocity ? params.velocity : params.g_buffer->get_texture(4),
                     k_taa_sampler_flags);

    const auto prev_vp = params.cam->get_prev_view_projection_unjittered();
    gfx::set_uniform(program_.u_prev_view_proj, prev_vp.get_matrix());

    const float taa_params[4] = {params.config.history_blend,
                                 params.config.sharpen,
                                 params.config.depth_reject_scale,
                                 params.config.variance_clip_scale};
    gfx::set_uniform(program_.u_taa_params, taa_params);

    // y = 1 while the camera is parked: the shader's display average of still pixels with last frame's history.
    const float taa_params2[4] = {use_velocity ? 1.0f : 0.0f, is_camera_parked(*params.cam) ? 1.0f : 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(program_.u_taa_params2, taa_params2);
    gfx::set_uniform(program_.u_pre_exposure, params.pre_exposure.to_uniform().data());

    const auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, program_.program->native_handle());

    gfx::set_state(BGFX_STATE_DEFAULT);
    program_.program->end();
    gfx::discard();

    gfx::render_pass hist_pass("TAA/History Blit");
    gfx::blit(hist_pass.id,
              history_tex->native_handle(),
              0,
              0,
              temp_fbo->get_texture(1)->native_handle(),
              0,
              0);

    if(params.output)
    {
        gfx::render_pass out_pass("TAA/Output Blit");
        gfx::blit(out_pass.id,
                  params.output->get_texture(0)->native_handle(),
                  0,
                  0,
                  temp_fbo->get_texture(0)->native_handle(),
                  0,
                  0);
        return params.output;
    }

    return temp_fbo;
}

void taa_pass::release_resources(gfx::render_view& rview)
{
    rview.tex_remove("TAA_HISTORY");
    rview.fbo_remove("TAA_TEMP");
    rview.tex_remove("TAA_TEMP");
    rview.tex_remove("TAA_TEMP_HISTORY");
}

} // namespace unravel
