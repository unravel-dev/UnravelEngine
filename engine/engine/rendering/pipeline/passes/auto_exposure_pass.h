#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <bgfx/bgfx.h>
#include <cstdint>

namespace unravel
{

/// Spatial weighting applied to pixels when building the luminance histogram.
enum class exposure_metering_mode : std::uint8_t
{
    /// Every pixel contributes equally (uniform full-frame metering).
    average = 0,
    /// Smooth radial falloff toward the screen edges (Gaussian). Default.
    center_weighted = 1,
    /// Only pixels inside a central circle contribute; everything else is ignored.
    spot = 2,
};

class auto_exposure_pass
{
public:
    auto_exposure_pass() = default;
    ~auto_exposure_pass()
    {
        shutdown();
    }
    struct settings
    {
        /// Lower clamp for metered scene brightness in EV100. A LOWER value lets the
        /// system BRIGHTEN dark scenes more (eye adaptation walking into shadow).
        /// -3 keeps genuinely dark interiors reading dark (how much lift actually
        /// happens is further limited by dark_adaptation, which scales the deficit
        /// below the neutral point): probe-GI leaks are bounded but never zero, and
        /// deeper adaptation exposes whatever tiny residual a sealed room contains
        /// toward mid-gray (measured on the sealed-box test: -6 turned a sub-percent
        /// leak into a full wash). Lower this per volume only for content where deep
        /// night adaptation is the point, accepting that it will surface GI residuals.
        float min_ev = -3.0f;
        /// Upper clamp for metered scene brightness in EV100. A HIGHER value lets the
        /// system DARKEN bright scenes more. Since the linear-color pipeline landed,
        /// the meter is trusted to float freely: 16 covers physical daylight (EV100
        /// 14-16) and is effectively "no clamp" for current scene luminances, so the
        /// exposure anchors the image instead of a hand-pinned reference level.
        float max_ev = 16.0f;
        /// Exposure bias in EV stops applied on top of the metered result (+1 = 2x brighter).
        /// With this pass's K=12.5 / exp2(-EV)/1.2 formulation, an unclamped meter pins
        /// the metered band at ~10.4% post-exposure. +3 (~83%) is the look that reaches
        /// AgX display-white with this engine's lighting scale: the boost sits before
        /// grading and the tone curve, which is punchier than baking the same 2 stops
        /// into the operator. Use this slider for scene-to-scene bias around that look.
        float compensation = 3.0f;
        /// Fraction of a dark scene's EV deficit the eye adapts away (the single-slope
        /// version of UE's Exposure Compensation Curve / Unity HDRP's Curve Remapping).
        /// 1 = full adaptation: any dark scene is lifted back to the mid-gray anchor
        /// (until Min EV stops it) and dark rooms read bright. 0 = no adaptation: dark
        /// scenes render at their true relative darkness. The 0.1 default keeps
        /// adaptation subtle -- a scene 5 stops under neutral is lifted only half a
        /// stop, so darkness reads as darkness. Applies only BELOW the neutral point;
        /// bright-scene metering is unaffected.
        float dark_adaptation = 0.1f;
        /// Time constant in seconds for the exposure to INCREASE (scene getting darker).
        /// Larger = slower adaptation. Adaptation is performed in log2/EV space.
        float adaptation_speed_up = 3.0f;
        /// Time constant in seconds for the exposure to DECREASE (scene getting brighter).
        float adaptation_speed_down = 1.0f;
        /// Fraction of the darkest (weighted) pixels excluded from the average.
        /// 0.80 meters only the brightest quintile (classic highlight-protecting
        /// metering, as in UE's histogram defaults): shadows can no longer drag the
        /// exposure up and blow out lit areas in high-contrast scenes.
        float low_percentile = 0.80f;
        /// Upper fraction kept before excluding the brightest (weighted) pixels.
        /// 0.98 lets sky participate in metering (so it exposes saturated, not washed)
        /// while still rejecting sun disc / specular pinpoints.
        float high_percentile = 0.98f;
        /// How pixels are spatially weighted when metering scene luminance.
        exposure_metering_mode metering_mode = exposure_metering_mode::center_weighted;
        /// Relative radius (in normalized device coords) of the metering region.
        /// For center_weighted this is the Gaussian sigma; for spot it is the hard cutoff radius.
        float metering_area = 0.7f;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        settings config{};
        float delta_time = 0.0f;
    };

    auto init(rtti::context& ctx) -> bool;
    auto shutdown() -> int32_t;
    /// Runs histogram + temporal average to produce a 1x1 adapted exposure texture.
    void run(gfx::render_view& rview, const run_params& params);

    /// Returns the current adapted exposure value's texture (1x1 R32F).
    /// Usable by the tonemapping pass to read the exposure.
    auto get_exposure_texture(gfx::render_view& rview) const -> gfx::texture::ptr;

    void release_resources(gfx::render_view& rview);

private:
    static constexpr float min_log_lum = -10.0f;
    static constexpr float max_log_lum = 20.0f;
    static constexpr int histogram_bins = 256;
    /// Metering is performed on a subsampled grid capped to this many texels on the
    /// long edge. Cuts bandwidth and atomic contention versus full-resolution metering
    /// while leaving the resulting histogram statistically unchanged.
    static constexpr std::uint32_t max_metering_dim = 512;

    void ensure_resources(gfx::render_view& rview);
    void run_histogram(gfx::render_view& rview, const settings& config, const gfx::frame_buffer::ptr& input);
    void run_average(gfx::render_view& rview, const settings& config, float dt);

    struct histogram_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_hdr_input;
        gfx::program::uniform_ptr u_histogram_params;
        gfx::program::uniform_ptr u_metering_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_hdr_input, "s_hdr_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_histogram_params, "u_histogram_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_metering_params, "u_metering_params", gfx::uniform_type::Vec4);
        }
    } histogram_program_;

    struct average_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_average_params0;
        gfx::program::uniform_ptr u_average_params1;
        gfx::program::uniform_ptr u_average_params2;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_average_params0, "u_average_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_average_params1, "u_average_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_average_params2, "u_average_params2", gfx::uniform_type::Vec4);
        }
    } average_program_;

    bgfx::DynamicIndexBufferHandle histogram_buffer_ = BGFX_INVALID_HANDLE;
    /// False until the first average dispatch has consumed (and zeroed) the histogram.
    /// The buffer's initial contents are undefined and cannot be seeded from the CPU
    /// (bgfx forbids update() on COMPUTE_WRITE buffers), so run_average discards the
    /// first measurement instead of adapting toward it.
    bool histogram_bins_valid_ = false;
};

} // namespace unravel
