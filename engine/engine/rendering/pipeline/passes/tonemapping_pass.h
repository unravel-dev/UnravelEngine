#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>

namespace unravel
{

enum class tonemapping_method : uint8_t
{
    none = 0,
    exponential,
    reinhard,
    reinhard_lum,
    hable,
    filmic,
    aces,
    aces_lum,
    reinhard2,
    unreal3,
    lottes,
    uchimura,
    neutral,
    agx,
    agx_golden,
    agx_punchy
};

class tonemapping_pass
{
public:
    struct settings
    {
        float exposure = 1.0f;
        /// AgX is the engine's hero transform: robust hue preservation under bright
        /// lights (no red->orange / blue->cyan skew), smooth highlight rolloff, no
        /// per-channel clipping. It is a neutral base intended to be graded on top
        /// of; agx_punchy/agx_golden are built-in stronger looks, and aces gives the
        /// UE-family punch, at the cost of hue skews.
        tonemapping_method method = tonemapping_method::agx;

        // -- Color grading, evaluated in LINEAR space after exposure, before the
        //    tone curve (the same stage UE/Unity grade at).
        /// White balance: warm (+) / cool (-) shift. Range [-1, 1].
        float temperature = 0.0f;
        /// White balance: magenta (+) / green (-) shift. Range [-1, 1].
        float tint = 0.0f;
        /// Log-space contrast pivoting on 18% mid-gray: mids hold their exposure
        /// while stops above/below expand (>1) or compress (<1).
        float contrast = 1.0f;
        /// Saturation around Rec.709 luma. 0 = grayscale, 1 = neutral.
        float saturation = 1.0f;

        /// Triangular-PDF dither before 8-bit quantization. Costs nothing visible
        /// and removes banding in smooth gradients (skies, walls).
        bool dithering = true;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        gfx::frame_buffer::ptr output;
        gfx::texture::ptr exposure_texture;

        settings config{};
    };

    auto init(rtti::context& ctx) -> bool;
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;
    void release_resources(gfx::render_view& rview);

private:
    auto create_or_update_output_fb(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& input,
                                    const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr;

    struct tonemapping_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_tonemapping, "u_tonemapping", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_grading, "u_grading", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_wb_lms, "u_wb_lms", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_input, "s_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_exposure, "s_exposure", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr u_tonemapping;
        gfx::program::uniform_ptr u_grading;
        gfx::program::uniform_ptr u_wb_lms;
        gfx::program::uniform_ptr s_input;
        gfx::program::uniform_ptr s_exposure;

        std::unique_ptr<gpu_program> program;

    } tonemapping_program_;
};
} // namespace unravel
