#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <math/math.h>
#include <bgfx/bgfx.h>
#include <array>
#include <cstdint>
#include <vector>

namespace unravel
{

/// Spatial weighting applied to pixels when building the luminance histogram.
enum class exposure_metering_mode : std::uint8_t
{
    /// Every pixel contributes equally (uniform full-frame metering without a mask). Default.
    average = 0,
    /// Smooth radial falloff toward the screen edges (Gaussian).
    center_weighted = 1,
    /// Only pixels inside a central circle contribute; everything else is ignored.
    spot = 2,
};

/**
 * @brief Histogram auto exposure with temporal eye adaptation and local exposure.
 *
 * A compute pass meters a luminance histogram of the post-TAA HDR image, a second one trims
 * it to a percentile band, takes the log-average and adapts the exposure toward
 * 0.18 * 2^bias / average (linear in stops, exponential near the target), where the bias is
 * the compensation plus the compensation curve at the metered EV100. The result is a 1x1
 * RGBA32F texture, AUTO_EXPOSURE: r = adapted exposure, g = target exposure, b = applied bias
 * in stops, a = average local exposure.
 *
 * The adapted exposure also reaches the CPU, a few frames late and without a GPU sync,
 * through occlusion queries (resolve_exposure_readback): the pipeline's pre-exposure is built
 * from this late value.
 */
class auto_exposure_pass
{
public:
    auto_exposure_pass() = default;
    ~auto_exposure_pass()
    {
        shutdown();
    }

    /// Serialized with the settings. Data saved without it, or with an older value, is ignored
    /// on load and the settings keep their defaults: version 3 is the current default set with
    /// full adaptation and the compensation curve, and values tuned for an earlier model do not
    /// carry over.
    static constexpr std::uint32_t settings_version = 3;

    /// The compensation curve is baked into a LUT of compensation_curve_samples samples over
    /// this EV100 range. Metered values outside the range read the end samples.
    static constexpr float compensation_curve_min_ev = -10.0f;
    static constexpr float compensation_curve_max_ev = 20.0f;
    static constexpr std::uint16_t compensation_curve_samples = 64;

    /// A camera move longer than this, or a turn wider than camera_cut_degrees about any of
    /// its axes, in one frame is a cut: the exposure snaps to its target instead of adapting.
    /// The pipeline's GI gather and reflections start their histories over on a cut too.
    static constexpr float camera_cut_distance = 100.0f;
    static constexpr float camera_cut_degrees = 75.0f;

    struct settings
    {
        /// Exposure bias in stops. The metered log-average luminance L is mapped to
        /// 0.18 * 2^compensation before grading and the tone curve:
        /// exposure = 0.18 * 2^compensation / L. +1 is the default, which the default tone
        /// curve is built around.
        float compensation = 1.0f;
        /// Extra bias in stops as a function of the metered scene brightness: x = metered EV100,
        /// y = stops added to the compensation. Keys sorted by x, linear between them and
        /// constant past the ends; empty adds nothing.
        /// The usual use keeps a night scene dark, e.g. (-6, -2), (0, 0).
        std::vector<math::vec2> compensation_curve;
        /// Lowest metered scene brightness auto exposure adapts to, in EV100 = log2(L / 0.18).
        /// Darker scenes stop brightening here.
        float min_ev = -10.0f;
        /// Highest metered scene brightness auto exposure adapts to, in EV100. Brighter scenes
        /// stop darkening here. Min >= max holds a fixed exposure.
        float max_ev = 20.0f;
        /// Fraction of the darkest histogram weight excluded from the average.
        float low_percentile = 0.10f;
        /// Fraction of the histogram weight kept before the brightest is excluded.
        float high_percentile = 0.90f;
        /// Adaptation speed in stops per second while the scene gets brighter. The exposure
        /// moves at this rate until it is within 1.5 stops of the target, then settles
        /// exponentially.
        float speed_up = 3.0f;
        /// Adaptation speed in stops per second while the scene gets darker.
        float speed_down = 1.0f;
        /// How pixels are spatially weighted when metering scene luminance.
        ///
        /// Average is the default. Measured on GI_TestSuite, centre weighting moves the
        /// exposure by 4% outdoors (darker - it meters the bright courtyard centre harder) and
        /// 1.4% indoors (brighter), so it buys little and makes the meter depend on where
        /// content happens to sit in frame. Its lower exterior clipping (0.0019 against 0.0054)
        /// is an exposure-LEVEL difference, which compensation owns.
        exposure_metering_mode metering_mode = exposure_metering_mode::average;
        /// Radius of the metering region relative to half the screen height. Gaussian sigma for
        /// center_weighted, hard cutoff for spot.
        float metering_area = 0.7f;

        // -- LOCAL EXPOSURE (a bilateral grid of the metered log luminance). One exposure for
        //    the whole frame has to choose between a sunlit exterior and the interior around
        //    it; local exposure keeps the global choice and redistributes CONTRAST around each
        //    pixel's own neighbourhood level, so both stay readable without the flat look of a
        //    tone curve pulled down. Neutral (every scale 1) is a no-op and costs nothing.
        //
        //    The contrast scales default to 0.8 / 0.8, so every scene starts with a mild
        //    compression of both highlights and shadows. The shadow lift also raises whatever
        //    little light an unlit room holds.
        /// Contrast scale applied to neighbourhoods ABOVE the middle-grey pivot; below 1
        /// compresses highlights toward the pivot (0.6-1.0 recommended). 1 = off.
        float local_highlight_contrast = 0.8f;
        /// Contrast scale for neighbourhoods BELOW the pivot; below 1 lifts shadows. 1 = off.
        float local_shadow_contrast = 0.8f;
        /// How much of each pixel's detail (its distance from its neighbourhood level) survives
        /// the contrast change. 1 keeps detail exactly.
        float local_detail_strength = 1.0f;
        /// Blend from the edge-aware bilateral level toward the plain blurred level. Higher
        /// softens halos at strong edges; lower keeps local contrast.
        float local_blurred_blend = 0.6f;
        /// Kernel diameter of the blurred level, as a percentage of the view width.
        float local_blurred_kernel_percent = 50.0f;
        /// Shifts the pivot the contrast scales turn around, in stops.
        float local_middle_grey_bias = 0.0f;

        /// Local exposure runs only when a contrast scale or the detail strength is not 1.
        /// Everything else about it is shape, so a neutral setup must cost nothing.
        auto is_local_exposure_enabled() const -> bool
        {
            return local_highlight_contrast != 1.0f || local_shadow_contrast != 1.0f ||
                   local_detail_strength != 1.0f;
        }
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        settings config{};
        float delta_time = 0.0f;
        /// The camera jumped this frame: the exposure snaps to its target.
        bool camera_cut = false;
        /// The view's pre-exposure this frame; the input carries it and metering removes it.
        float pre_exposure = 1.0f;
    };

    auto init(rtti::context& ctx) -> bool;
    auto shutdown() -> int32_t;
    /// Runs histogram + temporal average to update AUTO_EXPOSURE and the instrument textures,
    /// then submits this frame's exposure readback queries.
    void run(gfx::render_view& rview, const run_params& params);

    /**
     * @brief The newest exposure value the occlusion-query channel has delivered:
     * adapted exposure x average local exposure, a few frames old. Never blocks. Returns the
     * previous value while a submission is still resolving, and 1 before the first one or
     * when the occlusion query pool is exhausted.
     */
    auto resolve_exposure_readback(gfx::render_view& rview) -> float;

    /**
     * @brief The occlusion-query readback channel of ONE render view, stored in that view's
     *        data under @ref view_key.
     *
     * PER VIEW on purpose. This state used to live on the pass, which one pipeline instance
     * runs for every view it renders: a reflection probe face or a thumbnail rendered through
     * a camera's pipeline took the no-exposure path, whose release destroyed the CAMERA's
     * queries and reset its readback to 1, so the camera's next frame pre-exposed at the manual
     * value while its own exposure texture still held the adapted one, and the queries then
     * came back a few frames later - a dark / normal alternation whenever such captures
     * interleaved with the camera, every flip rescaling the TAA and GI histories by the whole
     * adaptation ratio (user-found 2026-09-18 on GI_TestSuite at play start). The queries die
     * with the view.
     */
    /// Readback layout: tag bits, then code bits, then the tag bits again. Queries resolve in
    /// submission order, so equal leading and trailing tags prove every query in between came
    /// from the same submission.
    static constexpr std::uint32_t readback_tag_bits = 2;
    static constexpr std::uint32_t readback_code_bits = 10;
    static constexpr std::uint32_t readback_query_count = readback_tag_bits * 2 + readback_code_bits;
    /// log2 range of the encoded value; 10 bits over 40 stops step 0.04 stops.
    static constexpr float readback_min_log2 = -28.0f;
    static constexpr float readback_max_log2 = 12.0f;

    struct readback_state
    {
        static constexpr const char* view_key = "AUTO_EXPOSURE_READBACK_STATE";
        readback_state();
        ~readback_state();
        readback_state(const readback_state&) = delete;
        auto operator=(const readback_state&) -> readback_state& = delete;
        /// Destroys the queries and forgets the value (the view goes back to manual exposure).
        void reset();
        std::array<bgfx::OcclusionQueryHandle, readback_query_count> queries{};
        bool created = false;
        /// Submissions so far; the tag is its low bits.
        std::uint32_t submissions = 0;
        /// Last value decoded for this view.
        float value = 1.0f;
    };

    /// Returns the AUTO_EXPOSURE texture (1x1 RGBA32F, layout in the class comment).
    auto get_exposure_texture(gfx::render_view& rview) const -> gfx::texture::ptr;
    /// Returns the exposure history ring (history_length x 1 RGBA32F): per frame log2 adapted
    /// exposure, log2 target exposure, metered log2 luminance, applied bias.
    auto get_history_texture(gfx::render_view& rview) const -> gfx::texture::ptr;
    /// Returns the last histogram (histogram_bins x 1 R32F): each bin's share of the metered
    /// weight; bin 0 (black) is always 0.
    auto get_histogram_texture(gfx::render_view& rview) const -> gfx::texture::ptr;
    /// The ring slot the NEXT frame writes - which is also where the oldest kept frame sits, so
    /// a reader walks the history in time order from it (the exposure debug view).
    auto get_history_index(gfx::render_view& rview) const -> std::uint32_t;

    /**
     * @brief The local exposure chain's two lookups for the tonemapper, or nulls when local
     *        exposure is off for this view (settings::is_local_exposure_enabled).
     *
     * @c grid is the RGBA32F bilateral grid flattened to 2D (tiles x * @ref local_exposure_slices,
     * tiles y), each texel holding sum(log2 luminance) and sum(weight) of the metering cells in
     * that tile and luminance slice (raw sums). @c blurred is the R16F Gaussian of the log
     * luminance at 1/32 of the view resolution, sampled bilinearly.
     */
    struct local_exposure_view
    {
        gfx::texture::ptr grid;
        gfx::texture::ptr blurred;
        /// Tiles across and down; the tonemapper maps screen uv onto them.
        float tiles_x = 0.0f;
        float tiles_y = 0.0f;
        /// The share of the tile grid and of the blurred texture the view covers per axis (the
        /// last tile / texel is partial): screen uv times this addresses them.
        math::vec2 grid_uv_scale{1.0f, 1.0f};
        math::vec2 blurred_uv_scale{1.0f, 1.0f};
    };
    auto get_local_exposure_view(gfx::render_view& rview) const -> local_exposure_view;

    /// Luminance slices per bilateral-grid column. Mirrors LOCAL_EXPOSURE_SLICES in
    /// cs_local_exposure_grid.sc.
    static constexpr std::uint32_t local_exposure_slices = 32;
    /// Metering cells per bilateral-grid tile edge (128 view pixels). Mirrors
    /// LOCAL_EXPOSURE_TILE_CELLS there.
    static constexpr std::uint32_t local_exposure_tile_cells = 32;
    /// Metering cells per blurred log luminance texel edge: 32 view pixels, so the blur runs at
    /// 1/32 of the view resolution. Mirrors LOCAL_EXPOSURE_BLUR_CELLS in
    /// cs_local_exposure_downsample.sc.
    static constexpr std::uint32_t local_exposure_blur_cells = 8;

    void release_resources(gfx::render_view& rview);

    /// log2 luminance range covered by the histogram.
    static constexpr float min_log_lum = -10.0f;
    static constexpr float max_log_lum = 20.0f;
    static constexpr int histogram_bins = 256;
    /// Frames kept in the exposure history ring.
    static constexpr std::uint16_t history_length = 256;

private:
    /// Source texels per metering cell along each axis. Four bilinear taps at the cell's inner
    /// corners average a 4x4 block, so every source texel contributes.
    static constexpr std::uint32_t metering_cell_texels = 4;
    /// Long-edge cap of the metering grid; larger inputs spread the four taps over bigger cells.
    static constexpr std::uint32_t max_metering_dim = 1024;
    /// Stops from the target at which adaptation switches from linear to exponential.
    static constexpr float exponential_transition_stops = 1.5f;
    /// Frame time at which the exponential phase's slope is matched to the linear phase.
    static constexpr float slope_match_frame_time = 1.0f / 60.0f;


    void ensure_resources(gfx::render_view& rview);
    /// The metering grid for an input of @p input_size: one cell per @ref metering_cell_texels
    /// square, capped on the long edge. The histogram and the local exposure grid must agree on
    /// it exactly - the second reads what the first wrote, cell for cell.
    static auto compute_metering_size(const usize32_t& input_size) -> usize32_t;
    void run_histogram(gfx::render_view& rview, const run_params& params);
    /// Builds the bilateral grid and its blurred companion from the metering grid's log
    /// luminance. Skipped entirely when the settings are neutral; the textures are then
    /// released so the tonemapper takes its own no-op path.
    void run_local_exposure(gfx::render_view& rview, const run_params& params);
    /// The blurred log luminance: the metering grid reduced to 1/32 of the view, then a
    /// separable Gaussian whose diameter is local_blurred_kernel_percent of the view width.
    void run_local_exposure_blur(gfx::render_view& rview, const settings& config, const usize32_t& meter_size);
    void run_average(gfx::render_view& rview, const run_params& params);
    /// The view's compensation curve LUT: 2^curve at compensation_curve_samples EV100 values,
    /// rebaked when the keys change. Null when the curve is empty.
    static auto update_compensation_curve(gfx::render_view& rview, const settings& config) -> gfx::texture::ptr;
    void submit_exposure_readback(gfx::render_view& rview);
    static auto ensure_readback_queries(readback_state& state) -> bool;

    struct histogram_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_hdr_input;
        gfx::program::uniform_ptr u_histogram_params;
        gfx::program::uniform_ptr u_metering_params;
        gfx::program::uniform_ptr u_metering_cell;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_hdr_input, "s_hdr_input", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_histogram_params, "u_histogram_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_metering_params, "u_metering_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_metering_cell, "u_metering_cell", bgfx::UniformType::Vec4);
        }
    } histogram_program_;

    struct average_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_average_params0;
        gfx::program::uniform_ptr u_average_params1;
        gfx::program::uniform_ptr u_average_params2;
        gfx::program::uniform_ptr u_average_params3;
        /// Local exposure's contrast shape, for the average local exposure lane.
        gfx::program::uniform_ptr u_average_params4;
        /// The compensation curve LUT's EV100 -> u scale and bias.
        gfx::program::uniform_ptr u_average_params5;
        gfx::program::uniform_ptr s_compensation_curve;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_average_params0, "u_average_params0", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params1, "u_average_params1", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params2, "u_average_params2", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params3, "u_average_params3", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params4, "u_average_params4", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params5, "u_average_params5", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_compensation_curve, "s_compensation_curve", bgfx::UniformType::Sampler);
        }
    } average_program_;

    struct local_grid_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_exposure_log_lum;
        gfx::program::uniform_ptr u_local_grid_params;
        gfx::program::uniform_ptr u_local_grid_range;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_exposure_log_lum, "s_exposure_log_lum", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_local_grid_params, "u_local_grid_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_local_grid_range, "u_local_grid_range", bgfx::UniformType::Vec4);
        }
    } local_grid_program_;

    struct local_downsample_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_exposure_log_lum;
        gfx::program::uniform_ptr u_local_downsample_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_exposure_log_lum, "s_exposure_log_lum", bgfx::UniformType::Sampler);
            cache_uniform(program.get(),
                          u_local_downsample_params,
                          "u_local_downsample_params",
                          bgfx::UniformType::Vec4);
        }
    } local_downsample_program_;

    struct local_blur_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_local_blur_input;
        gfx::program::uniform_ptr u_local_blur_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_local_blur_input, "s_local_blur_input", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_local_blur_params, "u_local_blur_params", bgfx::UniformType::Vec4);
        }
    } local_blur_program_;

    struct readback_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_exposure;
        gfx::program::uniform_ptr u_readback_bit;
        gfx::program::uniform_ptr u_readback_range;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_exposure, "s_exposure", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_readback_bit, "u_readback_bit", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_readback_range, "u_readback_range", bgfx::UniformType::Vec4);
        }
    } readback_program_;

    bgfx::DynamicIndexBufferHandle histogram_buffer_ = BGFX_INVALID_HANDLE;
    /// False until the first average dispatch has consumed (and zeroed) the histogram.
    /// The buffer's initial contents are undefined and cannot be seeded from the CPU
    /// (bgfx forbids update() on COMPUTE_WRITE buffers), so run_average discards the
    /// first measurement instead of adapting toward it.
    bool histogram_bins_valid_ = false;

};

} // namespace unravel
