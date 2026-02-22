#include "bloom_pass.h"
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <math/math.h>

namespace unravel
{

auto bloom_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_downsample = am.get_asset<gfx::shader>("engine:/data/shaders/bloom/fs_bloom_downsample.sc");
    auto fs_upsample = am.get_asset<gfx::shader>("engine:/data/shaders/bloom/fs_bloom_upsample.sc");
    auto fs_combine = am.get_asset<gfx::shader>("engine:/data/shaders/bloom/fs_bloom_combine.sc");

    downsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_downsample);
    downsample_program_.cache_uniforms();

    upsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_upsample);
    upsample_program_.cache_uniforms();

    combine_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_combine);
    combine_program_.cache_uniforms();

    return downsample_program_.program->is_valid() && upsample_program_.program->is_valid() &&
           combine_program_.program->is_valid();
}

auto bloom_pass::create_or_resize_mip_chain(gfx::render_view& rview,
                                            const usize32_t& viewport_size,
                                            int mip_count) -> void
{
    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    for(int i = 0; i < mip_count; ++i)
    {
        uint32_t w = viewport_size.width >> i;
        uint32_t h = viewport_size.height >> i;
        if(w < 1)
            w = 1;
        if(h < 1)
            h = 1;

        auto name = "BLOOM_MIP_" + std::to_string(i);
        auto& tex = rview.tex_get_or_emplace(name);

        if(!tex || tex->get_size().width != w || tex->get_size().height != h)
        {
            tex = std::make_shared<gfx::texture>(w, h, false, 1, gfx::texture_format::RGBA16F, flags);
        }
    }
}

auto bloom_pass::create_or_update_output_fb(gfx::render_view& rview,
                                            const gfx::frame_buffer::ptr& input,
                                            const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr
{
    if(output)
    {
        return output;
    }
    auto input_sz = input->get_size();
    auto& output_tex = rview.tex_get_or_emplace("BLOOM_OUTPUT");
    if(!output_tex || output_tex->get_size() != input_sz)
    {
        output_tex = std::make_shared<gfx::texture>(input_sz.width,
                                                    input_sz.height,
                                                    false,
                                                    1,
                                                    gfx::texture_format::RGBA16F,
                                                    BGFX_TEXTURE_RT);
    }
    auto& output_fbo = rview.fbo_get_or_emplace("BLOOM_OUTPUT");
    if(!output_fbo || output_fbo->get_size() != input_sz)
    {
        output_fbo = std::make_shared<gfx::frame_buffer>();
        output_fbo->populate({output_tex});
    }
    return output_fbo;
}

auto bloom_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Bloom Pass");

    const auto& input = params.input;
    const auto& config = params.config;

    int mip_count = math::clamp(config.mip_count, 2, max_mip_count);
    const auto viewport_size = input->get_size();

    create_or_resize_mip_chain(rview, viewport_size, mip_count);

    // Unity HDRP-style: full-resolution prefilter pass (13-tap + threshold) before pyramid.
    // Smoothes small specular highlights at full-res density, reducing flicker when downsampling.
    {
        uint32_t prefilter_w = viewport_size.width;
        uint32_t prefilter_h = viewport_size.height;

        auto prefilter_fbo = std::make_shared<gfx::frame_buffer>();
        prefilter_fbo->populate({rview.tex_get("BLOOM_MIP_0")});

        gfx::render_pass prefilter_pass("bloom_prefilter");
        prefilter_pass.bind(prefilter_fbo.get());
        prefilter_pass.set_view_proj({}, {});
        prefilter_pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

        downsample_program_.program->begin();

        float pixel_size[4] = {1.0f / float(prefilter_w), 1.0f / float(prefilter_h), 0.0f, 0.0f};
        gfx::set_uniform(downsample_program_.u_pixel_size, pixel_size);

        float params_data[4] = {config.threshold, 0.0f, config.soft_knee, config.clamp};
        gfx::set_uniform(downsample_program_.u_params, params_data);

        gfx::set_texture(downsample_program_.s_tex, 0, input->get_texture());

        irect32_t rect(0, 0, prefilter_w, prefilter_h);
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(prefilter_pass.id, downsample_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        downsample_program_.program->end();
    }

    // Downsample pyramid (no threshold; prefilter already applied)
    for(int i = 0; i < mip_count - 1; ++i)
    {
        uint32_t out_w = viewport_size.width >> (i + 1);
        uint32_t out_h = viewport_size.height >> (i + 1);
        if(out_w < 1)
            out_w = 1;
        if(out_h < 1)
            out_h = 1;

        const auto& out_tex = rview.tex_get("BLOOM_MIP_" + std::to_string(i + 1));
        auto fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({out_tex});

        gfx::render_pass pass("bloom_downsample");
        pass.bind(fbo.get());
        pass.set_view_proj({}, {});
        pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

        downsample_program_.program->begin();

        float pixel_size[4] = {1.0f / float(out_w), 1.0f / float(out_h), 0.0f, 0.0f};
        gfx::set_uniform(downsample_program_.u_pixel_size, pixel_size);

        float params_data[4] = {0.0f, 1.0f, 0.0f, 0.0f}; // mip_level=1: no threshold
        gfx::set_uniform(downsample_program_.u_params, params_data);

        const auto& src_tex = rview.tex_get("BLOOM_MIP_" + std::to_string(i));
        gfx::set_texture(downsample_program_.s_tex, 0, src_tex);

        irect32_t rect(0, 0, out_w, out_h);
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(pass.id, downsample_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        downsample_program_.program->end();
    }

    {
        auto clear_fbo = std::make_shared<gfx::frame_buffer>();
        clear_fbo->populate({rview.tex_get("BLOOM_MIP_0")});
        gfx::render_pass clear_pass("bloom_clear_mip0");
        clear_pass.bind(clear_fbo.get());
        clear_pass.set_view_proj(nullptr, nullptr);
        clear_pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);
    }

    for(int i = 0; i < mip_count - 1; ++i)
    {
        int out_idx = mip_count - 2 - i;
        uint32_t out_w = viewport_size.width >> out_idx;
        uint32_t out_h = viewport_size.height >> out_idx;
        if(out_w < 1)
            out_w = 1;
        if(out_h < 1)
            out_h = 1;

        const auto& out_tex = rview.tex_get("BLOOM_MIP_" + std::to_string(out_idx));
        auto fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({out_tex});

        gfx::render_pass pass("bloom_upsample");
        pass.bind(fbo.get());
        pass.set_view_proj({}, {});

        upsample_program_.program->begin();

        float pixel_size[4] = {1.0f / float(out_w), 1.0f / float(out_h), 0.0f, 0.0f};
        gfx::set_uniform(upsample_program_.u_pixel_size, pixel_size);

        float intensity[4] = {config.intensity, 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(upsample_program_.u_intensity, intensity);

        const auto& src_tex = rview.tex_get("BLOOM_MIP_" + std::to_string(mip_count - 1 - i));
        gfx::set_texture(upsample_program_.s_tex, 0, src_tex);

        irect32_t rect(0, 0, out_w, out_h);
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ADD);
        gfx::submit(pass.id, upsample_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        upsample_program_.program->end();
    }

    auto output = create_or_update_output_fb(rview, input, params.output);

    gfx::render_pass pass("bloom_combine");
    pass.bind(output.get());
    pass.set_view_proj({}, {});

    combine_program_.program->begin();

    gfx::set_texture(combine_program_.s_scene, 0, input->get_texture());
    gfx::set_texture(combine_program_.s_bloom, 1, rview.tex_get("BLOOM_MIP_0"));

    const auto output_size = output->get_size();
    irect32_t rect(0, 0, output_size.width, output_size.height);
    gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, combine_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    combine_program_.program->end();

    gfx::discard();

    return output;
}

} // namespace unravel
