#include "taa_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

#include <bgfx/bgfx.h>

namespace unravel
{
namespace
{
/// Avoid WRAP at RT edges when sampling history / scene color in TAA (reduces border streaks).
constexpr std::uint32_t k_taa_sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
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
    auto& out_tex = rview.tex_get_or_emplace("TAA_TEMP");
    if(gfx::needs_recreate(out_tex, sz, fmt))
    {
        out_tex.reset();
        out_tex = std::make_shared<gfx::texture>(sz.width, sz.height, false, 1, fmt, BGFX_TEXTURE_RT);
    }
    auto& fbo = rview.fbo_get_or_emplace("TAA_TEMP");
    if(gfx::needs_recreate(fbo, sz))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({out_tex});
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

    const auto prev_vp = params.cam->get_taa_prev_view_projection();
    gfx::set_uniform(program_.u_prev_view_proj, prev_vp.get_matrix());

    const float taa_params[4] = {params.config.history_blend,
                                 params.config.sharpen,
                                 params.config.depth_reject_scale,
                                 params.config.variance_clip_scale};
    gfx::set_uniform(program_.u_taa_params, taa_params);

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
              temp_fbo->get_texture(0)->native_handle(),
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
}

} // namespace unravel
