#pragma once

#include <engine/assets/asset_handle.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <math/color.h>

namespace unravel
{

class bloom_pass
{
public:
    struct settings
    {
        /// 0 (default) selects SCATTER mode: no threshold, the pyramid is an
        /// energy-normalized blur of the whole scene (recursive lerp, Unity/CoD
        /// style), added on top scaled by `intensity` -- everything blooms in
        /// proportion to its energy and the base image stays sharp. The meter
        /// runs before bloom, so scatter intensity is a small global lift
        /// (~0.15 EV at the default) on top of the locked exposure; treat it as
        /// part of the look, not as energy-neutral.
        /// > 0 selects the LEGACY mode: only pixels above this post-exposure
        /// luminance enter the pyramid (existing scenes keep their look).
        float threshold = 0.0f;
        float soft_knee = 0.5f;
        float clamp = 100.0f;
        /// Bloom strength: the pyramid is added as `scene + pyramid * intensity`.
        /// Scatter mode: 0.1-0.3 is a typical filmic glow (the pyramid carries
        /// scene-level energy). Legacy mode: additive multiplier as before.
        float intensity = 0.15f;
        /// Scatter mode only: per-hop lerp factor of the upsample recursion.
        /// Higher pushes energy toward the wider mips (bigger, softer halo).
        float scatter = 0.7f;
        int mip_count = 8;

        // Per-mip tint (RGB) and weight (alpha). Tints 1..5 are applied during the
        // upsample cascade (indexed by SOURCE mip). Legacy mode: alpha is that band's
        // additive weight (0 = band disabled). Scatter mode: alpha scales the hop's
        // lerp factor; 0 makes the hop an identity, truncating the WIDER bands above it.
        // mip0_tint is applied in the combine pass to the assembled pyramid (the
        // half-res band never has a hop of its own): overall bloom tint and weight.
        math::color mip0_tint{1.0f, 1.0f, 1.0f, 1.0f};  // assembled pyramid (combine)
        math::color mip1_tint{1.0f, 1.0f, 1.0f, 1.0f};  // 1/4 res
        math::color mip2_tint{1.0f, 1.0f, 1.0f, 1.0f};  // 1/8 res
        math::color mip3_tint{1.0f, 1.0f, 1.0f, 1.0f};  // 1/16 res
        math::color mip4_tint{1.0f, 1.0f, 1.0f, 1.0f};  // 1/32 res
        math::color mip5_tint{1.0f, 1.0f, 1.0f, 1.0f};  // 1/64 res - widest bloom

        /// Lens dirt: the bloom pyramid is additionally modulated by this
        /// screen-space mask and added on top (classic smudged-lens glow).
        /// 0 disables; requires @ref dirt_texture to be assigned.
        float dirt_intensity = 0.0f;
        /// Screen-space dirt/smudge mask for @ref dirt_intensity.
        asset_handle<gfx::texture> dirt_texture;

        auto get_mip_tint(int idx) const -> const math::color&
        {
            switch(idx)
            {
                case 0: return mip0_tint;
                case 1: return mip1_tint;
                case 2: return mip2_tint;
                case 3: return mip3_tint;
                case 4: return mip4_tint;
                case 5: return mip5_tint;
                default:
                {
                    static const math::color default_tint(1.0f, 1.0f, 1.0f, 1.0f);
                    return default_tint;
                }
            }
        }
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        gfx::frame_buffer::ptr output;
        settings config{};
        gfx::texture::ptr exposure_texture;
    };

    auto init(rtti::context& ctx) -> bool;
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;
    void release_resources(gfx::render_view& rview);

private:
    static constexpr int max_mip_count = 10;

    auto create_or_resize_mip_chain(gfx::render_view& rview,
                                    const usize32_t& viewport_size,
                                    int mip_count) -> void;
    auto get_mip_fbo(gfx::render_view& rview, int mip_index) -> const gfx::frame_buffer::ptr&;
    auto create_or_update_output_fb(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& input,
                                    const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr;

    struct downsample_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_pixel_size, "u_pixelSize", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_params, "u_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_tex, "s_tex", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_exposure, "s_exposure", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr u_pixel_size;
        gfx::program::uniform_ptr u_params;
        gfx::program::uniform_ptr s_tex;
        gfx::program::uniform_ptr s_exposure;
        std::unique_ptr<gpu_program> program;
    } downsample_program_;

    struct upsample_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_pixel_size, "u_pixelSize", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_tint, "u_tint", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_upsample_params, "u_upsampleParams", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_tex, "s_tex", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr u_pixel_size;
        gfx::program::uniform_ptr u_tint;
        gfx::program::uniform_ptr u_upsample_params;
        gfx::program::uniform_ptr s_tex;
        std::unique_ptr<gpu_program> program;
    } upsample_program_;

    struct combine_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_combine_params, "u_combineParams", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_combine_tint0, "u_combineTint0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_scene, "s_scene", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_bloom, "s_bloom", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_dirt, "s_dirt", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr u_combine_params;
        gfx::program::uniform_ptr u_combine_tint0;
        gfx::program::uniform_ptr s_scene;
        gfx::program::uniform_ptr s_bloom;
        gfx::program::uniform_ptr s_dirt;
        std::unique_ptr<gpu_program> program;
    } combine_program_;
};

} // namespace unravel
