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
    struct settings
    {
        /// Lower clamp for metered scene brightness in EV100. A LOWER value lets the
        /// system BRIGHTEN dark scenes more (eye adaptation walking into shadow).
        /// -1 keeps brightening subtle (~1 stop), which matches this engine's authored look.
        float min_ev = -4.0f;
        /// Upper clamp for metered scene brightness in EV100. A HIGHER value lets the
        /// system DARKEN bright scenes more. This engine's lighting is authored at a
        /// fixed reference exposure rather than calibrated to physical cd/m2, so the
        /// default pins the bright end at the authored peak (exposure ~= 1/1.2) to keep
        /// daylight looking like daylight. Raise it only if you want bright scenes
        /// (e.g. staring at the sky) to tone down below the authored level.
        float max_ev = 0.0f;
        /// Exposure bias in EV stops applied on top of the metered result (+1 = 2x brighter).
        float compensation = 0.0f;
        /// Time constant in seconds for the exposure to INCREASE (scene getting darker).
        /// Larger = slower adaptation. Adaptation is performed in log2/EV space.
        float adaptation_speed_up = 3.0f;
        /// Time constant in seconds for the exposure to DECREASE (scene getting brighter).
        float adaptation_speed_down = 1.0f;
        /// Fraction of the darkest (weighted) pixels excluded from the average.
        float low_percentile = 0.50f;
        /// Upper fraction kept before excluding the brightest (weighted) pixels.
        float high_percentile = 0.95f;
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
};

} // namespace unravel
