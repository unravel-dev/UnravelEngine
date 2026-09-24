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
        /// blue->cyan skew) and no per-channel clipping. Display-white / punch
        /// comes from Auto Exposure Compensation (default +1, UE 5.8's own, on
        /// the UE metering model - measured 2026-09-16 to put a neutral room's
        /// median at mid grey), not from remapping operators onto each other. AgX Punchy,
        /// ACES, or grading Contrast add more punch on top. aces/aces_lum give
        /// the UE-family S-curve at the cost of hue skews.
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

    /**
     * @brief Everything the per-pixel local exposure needs (auto_exposure_pass owns the two
     *        lookups and the settings; this pass only applies them).
     *
     * Inactive - the default - when the exposure pass did not build a grid, which is exactly
     * when the settings are neutral (settings::is_local_exposure_enabled).
     */
    struct local_exposure_params
    {
        /// Flattened bilateral grid: texel (tile_x * slices + slice, tile_y), rg = the slice's
        /// sum of log2 luminance and of weight per tile cell.
        gfx::texture::ptr grid;
        /// Tile-grid Gaussian of the plain tile means, the edge-blind level.
        gfx::texture::ptr blurred;
        float tiles_x = 0.0f;
        float tiles_y = 0.0f;
        float slices = 0.0f;
        /// The grid's luminance axis, matching the histogram's.
        float min_log_lum = 0.0f;
        float log_lum_range = 1.0f;
        float highlight_contrast = 1.0f;
        float shadow_contrast = 1.0f;
        float detail_strength = 1.0f;
        float blurred_blend = 0.6f;
        float middle_grey_bias = 0.0f;

        auto is_active() const -> bool
        {
            return grid && blurred && tiles_x > 0.0f && tiles_y > 0.0f && slices > 0.0f;
        }
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        gfx::frame_buffer::ptr output;
        gfx::texture::ptr exposure_texture;
        local_exposure_params local_exposure{};
        /// The view's scene-color pre-exposure (it already includes settings::exposure): the
        /// input carries it and the exposure removes it.
        float pre_exposure = 1.0f;

        settings config{};
        /// When FXAA runs after this pass, grain and TPDF dither are deferred to
        /// the FXAA shader so the AA filter does not smear them.
        bool defer_output_noise = false;
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
            cache_uniform(program.get(), u_tonemapping, "u_tonemapping", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_grading, "u_grading", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_wb_lms, "u_wb_lms", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_vignette, "u_vignette", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_lift, "u_lift", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gamma_inv, "u_gamma_inv", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gain, "u_gain", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_input, "s_input", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_exposure, "s_exposure", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_local_exposure, "u_local_exposure", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_local_exposure2, "u_local_exposure2", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_local_exposure3, "u_local_exposure3", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_local_exposure_grid, "s_local_exposure_grid", bgfx::UniformType::Sampler);
            cache_uniform(program.get(),
                          s_local_exposure_blurred,
                          "s_local_exposure_blurred",
                          bgfx::UniformType::Sampler);
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
        gfx::program::uniform_ptr u_local_exposure;
        gfx::program::uniform_ptr u_local_exposure2;
        gfx::program::uniform_ptr u_local_exposure3;
        gfx::program::uniform_ptr s_local_exposure_grid;
        gfx::program::uniform_ptr s_local_exposure_blurred;

        std::unique_ptr<gpu_program> program;

    } tonemapping_program_;
};
} // namespace unravel
