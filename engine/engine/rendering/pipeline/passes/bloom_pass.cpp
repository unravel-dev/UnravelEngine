#include "bloom_pass.h"
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
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

    downsample_program_.cache_uniforms();
    downsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_downsample);

    upsample_program_.cache_uniforms();
    upsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_upsample);

    combine_program_.cache_uniforms();
    combine_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_combine);

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
        uint32_t w = viewport_size.width >> (i + 1);
        uint32_t h = viewport_size.height >> (i + 1);
        if(w < 1)
            w = 1;
        if(h < 1)
            h = 1;

        auto tex_name = "BLOOM_MIP_" + std::to_string(i);
        auto& tex = rview.tex_get_or_emplace(tex_name);

        bool tex_changed = false;
        if(gfx::needs_recreate(tex, {w, h}))
        {
            tex.reset();
            tex = std::make_shared<gfx::texture>(w, h, false, 1, gfx::texture_format::RGBA16F, flags);
            tex_changed = true;
        }

        auto fbo_name = "BLOOM_MIP_FBO_" + std::to_string(i);
        auto& fbo = rview.fbo_get_or_emplace(fbo_name);
        if(!fbo || tex_changed)
        {
            fbo.reset();
            fbo = std::make_shared<gfx::frame_buffer>();
            gfx::fbo_attachment att;
            att.texture = tex;
            att.generate_mips = false;
            fbo->populate({att});
        }
    }
}

auto bloom_pass::get_mip_fbo(gfx::render_view& rview, int mip_index) -> const gfx::frame_buffer::ptr&
{
    return rview.fbo_get("BLOOM_MIP_FBO_" + std::to_string(mip_index));
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
        gfx::fbo_attachment att;
        att.texture = output_tex;
        att.generate_mips = false;
        output_fbo->populate({att});
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

    // First downsample: full-res input -> half-res MIP_0.
    // Uses Karis-weighted 13-tap to suppress sub-pixel specular flicker,
    // combined with threshold/soft-knee prefilter.
    {
        const auto& mip0_fbo = get_mip_fbo(rview, 0);
        auto mip0_size = mip0_fbo->get_size();

        gfx::render_pass pass("Bloom/Karis Downsample");
        pass.bind(mip0_fbo.get());
        pass.set_view_proj({}, {});
        pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

        downsample_program_.program->begin();

        float pixel_size[4] = {1.0f / float(viewport_size.width),
                               1.0f / float(viewport_size.height),
                               0.0f,
                               0.0f};
        gfx::set_uniform(downsample_program_.u_pixel_size, pixel_size);

        float params_data[4] = {config.threshold, 0.0f, config.soft_knee, config.clamp};
        gfx::set_uniform(downsample_program_.u_params, params_data);

        gfx::set_texture(downsample_program_.s_tex, 0, input->get_texture());
        gfx::set_texture(downsample_program_.s_exposure, 1,
                         params.exposure_texture ? params.exposure_texture : default_textures::get().white_texture());

        irect32_t rect(0, 0, mip0_size.width, mip0_size.height);
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(pass.id, downsample_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        downsample_program_.program->end();
    }

    // Downsample pyramid: standard 13-tap tent filter, no threshold.
    for(int i = 0; i < mip_count - 1; ++i)
    {
        uint32_t src_w = viewport_size.width >> (i + 1);
        uint32_t src_h = viewport_size.height >> (i + 1);
        uint32_t out_w = viewport_size.width >> (i + 2);
        uint32_t out_h = viewport_size.height >> (i + 2);
        if(src_w < 1)
            src_w = 1;
        if(src_h < 1)
            src_h = 1;
        if(out_w < 1)
            out_w = 1;
        if(out_h < 1)
            out_h = 1;

        const auto& fbo = get_mip_fbo(rview, i + 1);

        gfx::render_pass pass("Bloom/Downsample Pass");
        pass.bind(fbo.get());
        pass.set_view_proj({}, {});
        pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

        downsample_program_.program->begin();

        float pixel_size[4] = {1.0f / float(src_w), 1.0f / float(src_h), 0.0f, 0.0f};
        gfx::set_uniform(downsample_program_.u_pixel_size, pixel_size);

        float params_data[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        gfx::set_uniform(downsample_program_.u_params, params_data);

        gfx::set_texture(downsample_program_.s_tex, 0, rview.tex_get("BLOOM_MIP_" + std::to_string(i)));

        irect32_t rect(0, 0, out_w, out_h);
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(pass.id, downsample_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        downsample_program_.program->end();
    }

    // SCATTER mode (threshold == 0): the pyramid is an energy-conserving blur of the
    // scene. Each upsample hop LERPS the coarser level into the finer one, so MIP_0
    // keeps its own downsampled content as the recursion base and total energy stays
    // at scene level while the halo widens.
    //
    // LEGACY mode (threshold > 0): only thresholded highlights entered the pyramid;
    // MIP_0 is cleared so the additive accumulation is purely the multi-scale bloom.
    const bool scatter_mode = config.threshold <= 0.0f;
    if(!scatter_mode)
    {
        const auto& mip0_fbo = get_mip_fbo(rview, 0);
        gfx::render_pass clear_pass("Bloom/Clear MIP0 Pass");
        clear_pass.bind(mip0_fbo.get());
        clear_pass.set_view_proj(nullptr, nullptr);
        clear_pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);
    }

    // Upsample from smallest mip back to MIP_0: lerp-blend in scatter mode,
    // additive accumulation in legacy mode.
    for(int i = 0; i < mip_count - 1; ++i)
    {
        int src_idx = mip_count - 1 - i;
        int out_idx = mip_count - 2 - i;
        uint32_t out_w = viewport_size.width >> (out_idx + 1);
        uint32_t out_h = viewport_size.height >> (out_idx + 1);
        if(out_w < 1)
            out_w = 1;
        if(out_h < 1)
            out_h = 1;

        const auto& fbo = get_mip_fbo(rview, out_idx);

        gfx::render_pass pass("Bloom/Upsample Pass");
        pass.bind(fbo.get());
        pass.set_view_proj({}, {});

        upsample_program_.program->begin();

        float pixel_size[4] = {1.0f / float(out_w), 1.0f / float(out_h), 0.0f, 0.0f};
        gfx::set_uniform(upsample_program_.u_pixel_size, pixel_size);

        // Per-mip weight only. The global intensity is applied ONCE in the combine
        // pass: applying it here would compound it at every cascade hop, scaling
        // mip k's contribution by intensity^k instead of linearly.
        const auto& mip_tint = config.get_mip_tint(src_idx);
        float tint[4] = {mip_tint.value.x, mip_tint.value.y, mip_tint.value.z, mip_tint.value.w};
        gfx::set_uniform(upsample_program_.u_tint, tint);

        // Scatter mode: per-hop lerp factor (mip alpha modulates it); the shader emits
        // premultiplied (rgb * s, s) and ONE/INV_SRC_ALPHA blending realizes
        // dst = mix(dst, src, s) exactly. Legacy: 0 selects the additive path.
        const float hop_scatter =
            scatter_mode ? math::clamp(config.scatter * mip_tint.value.w, 0.0f, 1.0f) : 0.0f;
        float upsample_params[4] = {hop_scatter, 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(upsample_program_.u_upsample_params, upsample_params);

        gfx::set_texture(upsample_program_.s_tex, 0, rview.tex_get("BLOOM_MIP_" + std::to_string(src_idx)));

        irect32_t rect(0, 0, out_w, out_h);
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        const uint64_t blend_state =
            scatter_mode
                ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                : BGFX_STATE_BLEND_ADD;
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | blend_state);
        gfx::submit(pass.id, upsample_program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        upsample_program_.program->end();
    }

    // Combine: add half-res bloom (MIP_0) to full-res scene.
    // The texture sampler bilinearly upscales the half-res bloom.
    auto output = create_or_update_output_fb(rview, input, params.output);

    gfx::render_pass pass("Bloom/Combine Pass");
    pass.bind(output.get());
    pass.set_view_proj({}, {});

    combine_program_.program->begin();

    // x = intensity (mix fraction in scatter mode, additive multiplier in legacy),
    // y = scatter-mode flag, z = lens dirt intensity.
    float combine_params[4] = {config.intensity,
                               scatter_mode ? 1.0f : 0.0f,
                               config.dirt_intensity,
                               0.0f};
    gfx::set_uniform(combine_program_.u_combine_params, combine_params);

    gfx::set_texture(combine_program_.s_scene, 0, input->get_texture());
    gfx::set_texture(combine_program_.s_bloom, 1, rview.tex_get("BLOOM_MIP_0"));
    // Black fallback keeps the dirt term an exact no-op when no mask is assigned.
    auto dirt_tex = config.dirt_texture.get();
    gfx::set_texture(combine_program_.s_dirt, 2, dirt_tex ? dirt_tex : default_textures::get().black_texture());

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

void bloom_pass::release_resources(gfx::render_view& rview)
{
    for(int i = 0; i < max_mip_count; ++i)
    {
        rview.fbo_remove("BLOOM_MIP_FBO_" + std::to_string(i));
        rview.tex_remove("BLOOM_MIP_" + std::to_string(i));
    }
    rview.fbo_remove("BLOOM_OUTPUT");
    rview.tex_remove("BLOOM_OUTPUT");
}

} // namespace unravel
