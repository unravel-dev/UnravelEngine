#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <bgfx/bgfx.h>
#include <array>
#include <cstdint>

namespace unravel
{

/// Spatial weighting applied to pixels when building the luminance histogram.
enum class exposure_metering_mode : std::uint8_t
{
    /// Every pixel contributes equally (uniform full-frame metering). Default, like UE without
    /// a metering mask.
    average = 0,
    /// Smooth radial falloff toward the screen edges (Gaussian).
    center_weighted = 1,
    /// Only pixels inside a central circle contribute; everything else is ignored.
    spot = 2,
};

/**
 * @brief Histogram auto exposure on UE 5.8's model.
 *
 * A compute pass meters a luminance histogram of the post-TAA HDR image, a second one trims
 * it to a percentile band, takes the log-average and adapts the exposure toward
 * 0.18 * 2^compensation / average (linear in stops, exponential near the target). The result
 * is a 1x1 RGBA32F texture, AUTO_EXPOSURE: r = adapted exposure, g = target exposure,
 * b = applied bias in stops (compensation plus the dark-adaptation shift),
 * a = average local exposure.
 *
 * The adapted exposure also reaches the CPU, a few frames late and without a GPU sync,
 * through occlusion queries (resolve_exposure_readback): the pipeline's pre-exposure is built
 * from it, as UE builds View.PreExposure from its async eye adaptation readback.
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
    /// on load and the settings keep their defaults: version 2 moved to UE 5.8's exposure model,
    /// and values tuned for the previous model do not carry over.
    static constexpr std::uint32_t settings_version = 2;

    /// A camera move longer than this in one frame is a cut: the exposure snaps to its target
    /// instead of adapting (UE bCameraCut). Matches the jump the GI world probes treat as a
    /// teleport (two 2 m probe cells).
    static constexpr float camera_cut_distance = 4.0f;

    struct settings
    {
        /// Exposure bias in stops (UE Exposure Compensation). The metered log-average luminance L
        /// is mapped to 0.18 * 2^compensation before grading and the tone curve:
        /// exposure = 0.18 * 2^compensation / L.
        float compensation = 0.0f;
        /// Lowest metered scene brightness auto exposure adapts to, in EV100 = log2(L / 0.18)
        /// (UE Min Brightness). Darker scenes stop brightening here.
        ///
        /// -4 rather than UE's -10, chosen on measurement (2026-09-16, GI_TestSuite): at -10 a
        /// SEALED, unlit room metered 3.5 stops below this floor and auto exposure amplified it
        /// by 367x, rendering a physically black box as a mid-grey room made of GI residue. At
        /// -4 that cell reads black (display mean 0.021) while a legitimately dim but lit
        /// interior is untouched (0.3990 against 0.3992), because the floor only binds scenes
        /// darker than EV100 -4 - which are unlit, not dim. Lowering dark_adaptation was the
        /// alternative and is worse: it darkens the real interior by 21% as collateral.
        float min_ev = -4.0f;
        /// Highest metered scene brightness auto exposure adapts to, in EV100 (UE Max
        /// Brightness). Brighter scenes stop darkening here. Min >= max holds a fixed exposure.
        float max_ev = 20.0f;
        /// Fraction of the darkest histogram weight excluded from the average (UE Low Percent).
        float low_percentile = 0.10f;
        /// Fraction of the histogram weight kept before the brightest is excluded (UE High
        /// Percent).
        float high_percentile = 0.90f;
        /// Adaptation speed in stops per second while the scene gets brighter (UE Speed Up).
        /// The exposure moves at this rate until it is within 1.5 stops of the target, then
        /// settles exponentially.
        float speed_up = 3.0f;
        /// Adaptation speed in stops per second while the scene gets darker (UE Speed Down).
        float speed_down = 1.0f;
        /// Fraction of the brightness deficit below the neutral point (EV100 == compensation,
        /// where the exposure is 1) that adaptation removes. 1 adapts fully (UE without an
        /// exposure compensation curve); 0 keeps dark scenes at exposure 1 so they render at
        /// their true relative darkness.
        float dark_adaptation = 0.0f;
        /// How pixels are spatially weighted when metering scene luminance.
        ///
        /// Average, as UE meters without a mask. Measured 2026-09-16 (GI_TestSuite): centre
        /// weighting moved the exposure by 4% outdoors (darker - it meters the bright courtyard
        /// centre harder) and 1.4% indoors (brighter), so it buys little and makes the meter
        /// depend on where content happens to sit in frame. Its lower exterior clipping
        /// (0.0019 against 0.0054) is an exposure-LEVEL difference, which compensation owns.
        exposure_metering_mode metering_mode = exposure_metering_mode::average;
        /// Radius of the metering region relative to half the screen height. Gaussian sigma for
        /// center_weighted, hard cutoff for spot.
        float metering_area = 0.7f;

        // -- LOCAL EXPOSURE (UE's bilateral-grid method, Scene.cpp:525-534). One exposure for
        //    the whole frame has to choose between a sunlit exterior and the interior around
        //    it; local exposure keeps the global choice and redistributes CONTRAST around each
        //    pixel's own neighbourhood level, so both stay readable without the flat look of a
        //    tone curve pulled down. Neutral (every scale 1) is a no-op and costs nothing.
        //
        //    OFF BY DEFAULT, as UE ships it, on measurement rather than caution (2026-09-16,
        //    GI_TestSuite): 0.8 / 0.8 is a real win on a lit scene - clipping 0.0053 -> 0.0019
        //    and the shadow tail (p05) 0.043 -> 0.059, with the median pinned at the pivot -
        //    but the same shadow lift raises a SEALED, unlit room's display mean by 33% (62% at
        //    0.6), and what it lifts there is GI residue, not content. That trade belongs to a
        //    scene, not to a global default. 0.8 / 0.8 is the recommended starting point when a
        //    scene wants it; drop local_highlight_contrast alone to buy highlight headroom
        //    without touching the shadows (it left p05 unmoved and still cut clipping by 64%).
        /// Contrast scale applied to neighbourhoods ABOVE the middle-grey pivot; below 1
        /// compresses highlights toward the pivot (UE Highlight Contrast Scale, 0.6-1.0
        /// recommended). 1 = off.
        float local_highlight_contrast = 1.0f;
        /// Contrast scale for neighbourhoods BELOW the pivot; below 1 lifts shadows (UE Shadow
        /// Contrast Scale). 1 = off.
        float local_shadow_contrast = 1.0f;
        /// How much of each pixel's detail (its distance from its neighbourhood level) survives
        /// the contrast change (UE Detail Strength). 1 keeps detail exactly.
        float local_detail_strength = 1.0f;
        /// Blend from the edge-aware bilateral level toward the plain blurred level (UE Blurred
        /// Luminance Blend). Higher softens halos at strong edges; lower keeps local contrast.
        float local_blurred_blend = 0.6f;
        /// Kernel of the blurred level, as a percentage of the view width (UE Blurred Luminance
        /// Kernel Size Percent).
        float local_blurred_kernel_percent = 50.0f;
        /// Shifts the pivot the contrast scales turn around, in stops (UE Middle Grey Bias).
        float local_middle_grey_bias = 0.0f;

        /// UE's enable rule (PostProcessing.cpp:799-802): local exposure runs only when a
        /// contrast scale or the detail strength is not 1. Everything else about it is shape,
        /// so a neutral setup must cost nothing.
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
     * @c grid is the RG32F bilateral grid (tiles x, tiles y, @ref local_exposure_slices), each
     * column holding sum(log2 luminance) and sum(weight) per luminance slice, both per cell of
     * the tile. @c blurred is the R32F tile-grid Gaussian of the plain tile means.
     */
    struct local_exposure_view
    {
        gfx::texture::ptr grid;
        gfx::texture::ptr blurred;
        /// Tiles across and down; the tonemapper maps screen uv onto them.
        float tiles_x = 0.0f;
        float tiles_y = 0.0f;
    };
    auto get_local_exposure_view(gfx::render_view& rview) const -> local_exposure_view;

    /// Luminance slices per bilateral-grid column. Mirrors LOCAL_EXPOSURE_SLICES in
    /// cs_local_exposure_grid.sc.
    static constexpr std::uint32_t local_exposure_slices = 32;
    /// Metering cells per bilateral-grid tile edge. Mirrors LOCAL_EXPOSURE_TILE_CELLS there.
    static constexpr std::uint32_t local_exposure_tile_cells = 32;

    void release_resources(gfx::render_view& rview);

    /// log2 luminance range covered by the histogram (UE extended-range HistogramLogMin / Max).
    static constexpr float min_log_lum = -10.0f;
    static constexpr float max_log_lum = 20.0f;
    static constexpr int histogram_bins = 256;
    /// Frames kept in the exposure history ring.
    static constexpr std::uint16_t history_length = 256;

private:
    /// Source texels per metering cell along each axis. Four bilinear taps at the cell's inner
    /// corners average a 4x4 block, so every source texel contributes (UE meters every texel of
    /// a filtered downsample).
    static constexpr std::uint32_t metering_cell_texels = 4;
    /// Long-edge cap of the metering grid; larger inputs spread the four taps over bigger cells.
    static constexpr std::uint32_t max_metering_dim = 1024;
    /// Stops from the target at which adaptation switches from linear to exponential
    /// (UE r.EyeAdaptation.ExponentialTransitionDistance).
    static constexpr float exponential_transition_stops = 1.5f;
    /// Frame time at which the exponential phase's slope is matched to the linear phase
    /// (UE kFrameTimeEps).
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
    void run_average(gfx::render_view& rview, const run_params& params);
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

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_average_params0, "u_average_params0", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params1, "u_average_params1", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params2, "u_average_params2", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params3, "u_average_params3", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_average_params4, "u_average_params4", bgfx::UniformType::Vec4);
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

    struct local_blur_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_local_exposure_mean;
        gfx::program::uniform_ptr u_local_blur_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_local_exposure_mean, "s_local_exposure_mean", bgfx::UniformType::Sampler);
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
