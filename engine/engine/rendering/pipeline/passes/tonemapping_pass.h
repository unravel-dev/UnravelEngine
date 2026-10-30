#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/bloom_pass.h>
#include <engine/rendering/pipeline/passes/local_exposure_binding.h>
#include <engine/rendering/pipeline/pre_exposure.h>
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
    agx_punchy,
    /// The default SDR curve: a filmic, ACES-derived toe and shoulder in AP1, with blue correction
    /// and gamut expansion.
    film
};

class tonemapping_pass
{
public:
    struct settings
    {
        float exposure = 1.0f;
        /// Film is the default: the curve the default auto exposure compensation (+1) is built
        /// around - the metered average lands at display 0.67, with filmic contrast and
        /// saturation. AgX keeps hue more stable under very bright saturated light and holds
        /// about two more stops of highlights, with lifted shadows.
        tonemapping_method method = tonemapping_method::film;

        // -- Color grading, evaluated in LINEAR space after exposure, before the tone curve.
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

    /// The per-pixel local exposure lookups and shape (local_exposure_binding.h).
    using local_exposure_params = unravel::local_exposure_params;

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        gfx::frame_buffer::ptr output;
        gfx::texture::ptr exposure_texture;
        local_exposure_params local_exposure{};
        /// The bloom added on top of @c input, after local exposure measured and scaled the scene; no
        /// pyramid without bloom.
        bloom_pass::composite bloom{};
        /// The view's scene-color pre-exposure (it already includes settings::exposure): the
        /// input carries it and the exposure removes it.
        pre_exposure_state pre_exposure{};

        settings config{};
        /// When FXAA runs after this pass, grain and TPDF dither are deferred to
        /// the FXAA shader so the AA filter does not smear them.
        bool defer_output_noise = false;
    };

    auto init(rtti::context& ctx) -> bool;
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;
    void release_resources(gfx::render_view& rview);

private:
    /// Entries per axis of the tone curve's table (TONEMAPPING_LUT_SIZE in tonemapping_lut.sh).
    static constexpr std::uint32_t lut_size = 52;

    auto create_or_update_output_fb(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& input,
                                    const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr;
    /// The view's table of the colour grading, tone curve and display encoding of @p config, rendered again only
    /// when they changed (fs_tonemapping_lut.sc).
    auto update_lut(gfx::render_view& rview, const settings& config) -> const gfx::texture::ptr&;

    struct tonemapping_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_tonemapping, "u_tonemapping", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_grading, "u_grading", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_vignette, "u_vignette", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_input, "s_input", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_exposure, "s_exposure", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_bloom, "u_bloom", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_bloom_tint, "u_bloom_tint", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_bloom, "s_bloom", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_dirt, "s_dirt", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_tonemapping_lut, "s_tonemapping_lut", bgfx::UniformType::Sampler);
            local_exposure.cache_uniforms();
        }

        gfx::program::uniform_ptr u_tonemapping;
        gfx::program::uniform_ptr u_grading;
        gfx::program::uniform_ptr u_vignette;
        gfx::program::uniform_ptr s_input;
        gfx::program::uniform_ptr s_exposure;
        gfx::program::uniform_ptr u_bloom;
        gfx::program::uniform_ptr u_bloom_tint;
        gfx::program::uniform_ptr s_bloom;
        gfx::program::uniform_ptr s_dirt;
        gfx::program::uniform_ptr s_tonemapping_lut;
        local_exposure_uniforms local_exposure;

        std::unique_ptr<gpu_program> program;

    } tonemapping_program_;

    struct lut_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_tonemapping, "u_tonemapping", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_grading, "u_grading", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_wb_lms, "u_wb_lms", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_lift, "u_lift", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gamma_inv, "u_gamma_inv", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gain, "u_gain", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr u_tonemapping;
        gfx::program::uniform_ptr u_grading;
        gfx::program::uniform_ptr u_wb_lms;
        gfx::program::uniform_ptr u_lift;
        gfx::program::uniform_ptr u_gamma_inv;
        gfx::program::uniform_ptr u_gain;

        std::unique_ptr<gpu_program> program;
    } lut_program_;
};
} // namespace unravel
