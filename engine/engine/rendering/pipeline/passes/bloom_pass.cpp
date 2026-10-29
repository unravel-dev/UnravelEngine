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

    downsample_program_.cache_uniforms();
    downsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_downsample);

    upsample_program_.cache_uniforms();
    upsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_upsample);

    return downsample_program_.program->is_valid() && upsample_program_.program->is_valid();
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
        const auto& tex =
            rview.tex_acquire(tex_name, {.size = {w, h}, .format = bgfx::TextureFormat::RGBA16F, .flags = flags});

        auto fbo_name = "BLOOM_MIP_FBO_" + std::to_string(i);
        auto& fbo = rview.fbo_acquire(fbo_name);
        if(gfx::needs_recreate(fbo, {tex}))
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

auto bloom_pass::run(gfx::render_view& rview, const run_params& params) -> composite
{
    APP_SCOPE_PERF("Rendering/Bloom Pass");

    const auto& input = params.input;
    const auto& config = params.config;

    int mip_count = math::clamp(config.mip_count, 2, max_mip_count);
    const auto viewport_size = input->get_size();

    create_or_resize_mip_chain(rview, viewport_size, mip_count);

    // Every downsample draw binds it: only the first pass reads it, but the program declares
    // the sampler, and D3D11 flags an empty slot on each draw of the pyramid.
    const gfx::texture::ptr exposure_texture =
        params.exposure_texture ? params.exposure_texture : default_textures::get().white_texture();

    // First downsample: full-res input -> half-res MIP_0.
    // Uses a luma-weighted 13-tap to suppress sub-pixel specular flicker,
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

        // The shader divides the threshold by the adapted exposure to compare it against the
        // input; the input is pre-exposed, so the threshold carries the pre-exposure too, and the
        // test is the exposed luminance against the threshold whatever the pre-exposure.
        float params_data[4] = {config.threshold * params.pre_exposure.value, 0.0f, config.soft_knee, config.clamp};
        gfx::set_uniform(downsample_program_.u_params, params_data);

        gfx::set_texture(downsample_program_.s_tex, 0, input->get_texture());
        gfx::set_texture(downsample_program_.s_exposure, 1, exposure_texture);
        params.pre_exposure.bind();
        const float bloom_exposure[4] = {std::log2(std::max(params.manual_exposure, 1e-12f)), 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(downsample_program_.u_bloom_exposure, bloom_exposure);
        downsample_program_.local_exposure.submit(params.local_exposure, params.pre_exposure.value);

        irect32_t rect(0, 0, mip0_size.width, mip0_size.height);
        bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(pass.id, downsample_program_.program->native_handle());
        bgfx::setState(BGFX_STATE_DEFAULT);
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
        gfx::set_texture(downsample_program_.s_exposure, 1, exposure_texture);
        params.pre_exposure.bind();
        // Only the first downsample applies local exposure; the stand-ins keep the slots valid.
        downsample_program_.local_exposure.submit({}, params.pre_exposure.value);

        irect32_t rect(0, 0, out_w, out_h);
        bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(pass.id, downsample_program_.program->native_handle());
        bgfx::setState(BGFX_STATE_DEFAULT);
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

        // Per-mip weight only. The global intensity is applied ONCE where the tonemapper
        // adds the pyramid: applying it here would compound it at every cascade hop, scaling
        // mip k's contribution by intensity^k instead of linearly.
        const auto& mip_tint = config.get_mip_tint(src_idx);
        float tint[4] = {mip_tint.value.x, mip_tint.value.y, mip_tint.value.z, mip_tint.value.w};
        gfx::set_uniform(upsample_program_.u_tint, tint);

        // Scatter mode: per-hop lerp factor (mip alpha modulates it); the shader emits
        // premultiplied (rgb * s, s) and ONE/INV_SRC_ALPHA blending realizes
        // dst = mix(dst, src, s) exactly -- including s == 0, which the mode flag in
        // .y keeps on the premultiplied path (an identity hop, not the legacy output).
        const float hop_scatter =
            scatter_mode ? math::clamp(config.scatter * mip_tint.value.w, 0.0f, 1.0f) : 0.0f;
        float upsample_params[4] = {hop_scatter, scatter_mode ? 1.0f : 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(upsample_program_.u_upsample_params, upsample_params);

        gfx::set_texture(upsample_program_.s_tex, 0, rview.tex_get("BLOOM_MIP_" + std::to_string(src_idx)));

        irect32_t rect(0, 0, out_w, out_h);
        bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        const uint64_t blend_state =
            scatter_mode
                ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                : BGFX_STATE_BLEND_ADD;
        bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | blend_state);
        bgfx::submit(pass.id, upsample_program_.program->native_handle());
        bgfx::setState(BGFX_STATE_DEFAULT);
        upsample_program_.program->end();
    }

    // The tonemapper adds MIP_0 to the scene as it reads it, bilinearly: the bloom writes no full-size target of
    // its own for the tonemapper to read again. mip0_tint is the tint * weight of the assembled pyramid (the upsample
    // cascade indexes tints by source mip 1..N-1, so the half-res band has no hop of its own).
    composite result;
    result.pyramid = rview.tex_get("BLOOM_MIP_0");
    const auto& tint0 = config.mip0_tint.value;
    result.tint = math::vec3(tint0.x * tint0.w, tint0.y * tint0.w, tint0.z * tint0.w);
    result.intensity = config.intensity;
    // Black keeps the dirt term an exact no-op when no mask is assigned or it failed to load. asset_handle::get()
    // never returns null (misses resolve to the canonical empty asset, whose invalid bgfx handle would bind the
    // MAGENTA debug fallback at draw time), so the validity check must be on the texture.
    auto dirt_tex = config.dirt_texture.get();
    const bool dirt_ok = dirt_tex && dirt_tex->is_valid();
    result.dirt = dirt_ok ? dirt_tex : default_textures::get().black_texture();
    result.dirt_intensity = dirt_ok ? config.dirt_intensity : 0.0f;
    return result;
}

void bloom_pass::release_resources(gfx::render_view& rview)
{
    for(int i = 0; i < max_mip_count; ++i)
    {
        rview.fbo_remove("BLOOM_MIP_FBO_" + std::to_string(i));
        rview.tex_remove("BLOOM_MIP_" + std::to_string(i));
    }
}

} // namespace unravel
