/*
 * Histogram average compute shader for auto exposure (UE 5.8 model).
 *
 * Reads and zeroes the 256-bin luminance histogram, trims it to the low..high percentile band
 * of the non-black weight, takes the log-average L and adapts the exposure toward
 *   exposure = 0.18 * 2^compensation / L          (S/PostProcessEyeAdaptation.usf:168-200)
 * with EV100 = log2(L / 0.18) clamped to [min_ev, max_ev]. Below the neutral point
 * (EV100 == compensation, exposure 1) only dark_adaptation of the deficit is adapted away.
 *
 * Adaptation follows UE's ComputeEyeAdaptation (S/PostProcessHistogramCommon.ush:222-249) in
 * log2 space: farther than the transition distance from the target the exposure moves linearly
 * at speed stops per second; closer it settles exponentially with a slope matched to the
 * linear phase. A falling exposure (scene got brighter) uses speed_up.
 *
 * Output AUTO_EXPOSURE: r = adapted exposure, g = target exposure, b = applied bias in stops
 * (compensation plus the dark-adaptation shift), a = average local exposure (kept).
 * Instruments: the exposure history ring and the normalized histogram.
 */

#include "bgfx_compute.sh"

BUFFER_RW(s_histogram, uint, 0);
// Images take i_ names, never a sampler's: the OpenGL backend uploads every registered uniform
// a program declares, so an image named like a sampler is rebound to that sampler's stage.
IMAGE2D_RW(i_exposure, rgba32f, 1);
IMAGE2D_WO(i_exposure_history, rgba32f, 2);
IMAGE2D_WO(i_histogram_display, r32f, 3);

uniform vec4 u_average_params0;
uniform vec4 u_average_params1;
uniform vec4 u_average_params2;
uniform vec4 u_average_params3;
uniform vec4 u_average_params4;

#define u_min_log_lum        u_average_params0.x
#define u_log_lum_range      u_average_params0.y
#define u_low_percentile     u_average_params0.z
#define u_high_percentile    u_average_params0.w

#define u_min_ev             u_average_params1.x
#define u_max_ev             u_average_params1.y
#define u_compensation       u_average_params1.z
#define u_dark_adaptation    u_average_params1.w

#define u_delta_time         u_average_params2.x
#define u_speed_up           u_average_params2.y
#define u_speed_down         u_average_params2.z
#define u_force_target       u_average_params2.w

#define u_slope_match_up     u_average_params3.x
#define u_slope_match_down   u_average_params3.y
#define u_transition_stops   u_average_params3.z
#define u_history_texel      u_average_params3.w

// Local exposure's shape, for the AVERAGE local exposure below (the detail strength plays no
// part in it - see the loop).
#define u_local_highlight_contrast u_average_params4.x
#define u_local_shadow_contrast    u_average_params4.y
#define u_local_middle_grey_bias   u_average_params4.z

#define HISTOGRAM_BINS 256u
#define FIRST_LUMINANCE_BIN 1.0
#define LUMINANCE_BIN_SPAN 254.0
// log2(0.18): the metered average is anchored to 18% grey (EV100 = log2(L / 0.18)).
#define LOG2_MIDDLE_GREY -2.4739311883
// Exposure values at or above this are treated as garbage storage.
#define MAX_EXPOSURE 1.0e10

float bin_log_luminance(uint bin)
{
    return u_min_log_lum + (float(bin) - FIRST_LUMINANCE_BIN) / LUMINANCE_BIN_SPAN * u_log_lum_range;
}

NUM_THREADS(1, 1, 1)
void main()
{
    // Bin 0 holds black samples, which do not meter.
    float total = 0.0;
    for (uint i = 1u; i < HISTOGRAM_BINS; ++i)
    {
        total += float(s_histogram[i]);
    }

    float high_count = total * saturate(u_high_percentile);
    float low_count = min(total * saturate(u_low_percentile), high_count);
    float inv_total = (total > 0.0) ? 1.0 / total : 0.0;

    float cumulative = 0.0;
    float weighted_sum = 0.0;
    float weight_sum = 0.0;
    // The bins are consumed and zeroed in one pass, so the average local exposure - which needs
    // them again, after the adapted exposure is known - keeps its own copy.
    float bin_weights[HISTOGRAM_BINS];
    for (uint bin = 0u; bin < HISTOGRAM_BINS; ++bin)
    {
        float bin_weight = float(s_histogram[bin]);
        bin_weights[bin] = bin_weight;
        s_histogram[bin] = 0u;

        float display_share = 0.0;
        if (bin > 0u)
        {
            float active_start = max(cumulative, low_count);
            float active_end = min(cumulative + bin_weight, high_count);
            if (active_end > active_start)
            {
                float active_weight = active_end - active_start;
                weighted_sum += bin_log_luminance(bin) * active_weight;
                weight_sum += active_weight;
            }
            cumulative += bin_weight;
            display_share = bin_weight * inv_total;
        }
        imageStore(i_histogram_display, ivec2(int(bin), 0), vec4(display_share, 0.0, 0.0, 0.0));
    }

    vec4 previous = imageLoad(i_exposure, ivec2(0, 0));
    bool previous_valid = (previous.x == previous.x) && previous.x > 0.0 && previous.x < MAX_EXPOSURE;

    bool has_measurement = weight_sum > 0.0;
    float metered_log_luminance = has_measurement ? weighted_sum / weight_sum : 0.0;

    // UE takes min(min, max): a min above max pins the range to max.
    float metered_ev = metered_log_luminance - LOG2_MIDDLE_GREY;
    float clamped_ev = clamp(metered_ev, min(u_min_ev, u_max_ev), u_max_ev);

    float dark_deficit = max(0.0, u_compensation - clamped_ev);
    float dark_shift = dark_deficit * (1.0 - saturate(u_dark_adaptation));
    float applied_bias = u_compensation - dark_shift;
    float target_log_exposure = applied_bias - clamped_ev;

    float previous_log_exposure = previous_valid ? log2(previous.x) : target_log_exposure;
    if (!has_measurement)
    {
        // Nothing to meter (an all-black or empty frame): hold the current exposure.
        target_log_exposure = previous_log_exposure;
        applied_bias = previous_valid ? previous.z : u_compensation;
    }

    float difference = target_log_exposure - previous_log_exposure;
    bool scene_brighter = difference < 0.0;
    float speed = scene_brighter ? u_speed_up : u_speed_down;
    float slope_match = scene_brighter ? u_slope_match_up : u_slope_match_down;

    float linear_step = min(abs(difference), u_delta_time * speed);
    float linear_log_exposure = previous_log_exposure + sign(difference) * linear_step;

    float exponential_factor = min((1.0 - exp2(-u_delta_time * speed)) * slope_match, 1.0);
    float exponential_log_exposure = previous_log_exposure + difference * exponential_factor;

    float adapted_log_exposure = (abs(difference) > u_transition_stops) ? linear_log_exposure : exponential_log_exposure;
    adapted_log_exposure = mix(adapted_log_exposure, target_log_exposure, saturate(u_force_target));

    float adapted_exposure = exp2(adapted_log_exposure);
    if ((adapted_exposure != adapted_exposure) || adapted_exposure <= 0.0 || adapted_exposure >= MAX_EXPOSURE)
    {
        adapted_exposure = 1.0;
        adapted_log_exposure = 0.0;
    }

    float target_exposure = exp2(target_log_exposure);
    if ((target_exposure != target_exposure) || target_exposure <= 0.0 || target_exposure >= MAX_EXPOSURE)
    {
        target_exposure = adapted_exposure;
        target_log_exposure = adapted_log_exposure;
    }

    // AVERAGE LOCAL EXPOSURE (UE ComputeAverageLocalExposure, PostProcessEyeAdaptation.usf:202-228).
    // The pre-exposure multiplies by it, so the scene-color scale follows what the tonemapper
    // will actually do on average instead of drifting from it. Each bin stands for pixels at
    // that luminance; with no spatial term a bin's own level IS its neighbourhood base, so the
    // detail term cancels and only the contrast scales move the result - which is why a setup
    // that changes detail alone leaves this at 1.
    float average_local_exposure = 1.0;
    if (u_local_highlight_contrast != 1.0 || u_local_shadow_contrast != 1.0)
    {
        float pivot = LOG2_MIDDLE_GREY + applied_bias + u_local_middle_grey_bias;
        float local_sum = 0.0;
        float local_weight = 0.0;
        for (uint local_bin = 1u; local_bin < HISTOGRAM_BINS; ++local_bin)
        {
            float bin_weight = bin_weights[local_bin];
            if (bin_weight <= 0.0)
            {
                continue;
            }
            // The bin's luminance as the tonemapper will see it: after the adapted exposure.
            float luminance_log = bin_log_luminance(local_bin) + adapted_log_exposure;
            float contrast = luminance_log > pivot ? u_local_highlight_contrast : u_local_shadow_contrast;
            float target_log = pivot + (luminance_log - pivot) * contrast;
            local_sum += exp2(target_log - luminance_log) * bin_weight;
            local_weight += bin_weight;
        }
        average_local_exposure = local_weight > 0.0 ? local_sum / local_weight : 1.0;
    }

    imageStore(i_exposure, ivec2(0, 0), vec4(adapted_exposure, target_exposure, applied_bias, average_local_exposure));
    imageStore(i_exposure_history, ivec2(int(u_history_texel), 0),
               vec4(adapted_log_exposure, target_log_exposure, metered_log_luminance, applied_bias));
}
