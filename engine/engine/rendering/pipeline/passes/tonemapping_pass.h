#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <math/color.h>

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
        /// AgX is the default: hue-robust under bright light (no red->orange /
        /// blue->cyan skew), no per-channel clipping, and with the bright +3
        /// auto-exposure anchor its flat mid-section is compensated -- sunlit whites
        /// reach display white while saturated colors stay true (final calibration
        /// chosen by eye against UE on gray-box + colored scenes). aces/aces_lum give
        /// the punchier UE-family S-curve at the cost of hue skews; grading Contrast
        /// adds punch to AgX without leaving the hue-safe transform.
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

        // -- Lift / gamma / gain, applied to the DISPLAY-REFERRED image after the
        //    tone curve (classic video-grading semantics). Neutral is mid-gray
        //    (0.5, 0.5, 0.5); pushing a channel tints that tonal region, so e.g.
        //    warm gain + cool lift gives the classic orange-highlights/teal-shadows.
        /// Shadows: additive offset that fades out toward white.
        math::color lift{0.5f, 0.5f, 0.5f, 1.0f};
        /// Midtones: per-channel gamma around the neutral point.
        math::color gamma{0.5f, 0.5f, 0.5f, 1.0f};
        /// Highlights: per-channel multiplier (0.5 = 1x).
        math::color gain{0.5f, 0.5f, 0.5f, 1.0f};

        /// Lens vignette, applied in LINEAR space before the tone curve (light
        /// falloff, so darkened highlights still roll through the curve naturally).
        /// 0 disables.
        float vignette_intensity = 0.0f;
        /// How gradually the vignette falls off toward the corners.
        float vignette_smoothness = 0.5f;

        /// Animated film grain on the display-referred image, luma-weighted so
        /// highlights stay clean. 0 disables.
        float grain_intensity = 0.0f;

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
            cache_uniform(program.get(), u_vignette, "u_vignette", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_lift, "u_lift", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gamma_inv, "u_gamma_inv", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gain, "u_gain", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_input, "s_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_exposure, "s_exposure", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr u_tonemapping;
        gfx::program::uniform_ptr u_grading;
        gfx::program::uniform_ptr u_wb_lms;
        gfx::program::uniform_ptr u_vignette;
        gfx::program::uniform_ptr u_lift;
        gfx::program::uniform_ptr u_gamma_inv;
        gfx::program::uniform_ptr u_gain;
        gfx::program::uniform_ptr s_input;
        gfx::program::uniform_ptr s_exposure;

        std::unique_ptr<gpu_program> program;

    } tonemapping_program_;

    /// Monotonic frame counter used to animate the film grain.
    uint32_t frame_ = 0;
};
} // namespace unravel
