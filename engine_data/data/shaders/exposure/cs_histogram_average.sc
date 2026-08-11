/*
 * Histogram average compute shader for auto-exposure.
 *
 * Reads the 256-bin luminance histogram, trims configurable low/high
 * percentiles (excluding sky, dark noise), computes the weighted average
 * log2 luminance, converts to an exposure value, and temporally adapts
 * toward the target using an exponential decay with separate bright/dark speeds.
 *
 * Runs as a single workgroup of 256 threads (one per bin).
 *
 * Reference: Frostbite Engine, Unreal Engine, Unity HDRP.
 */

#include "bgfx_compute.sh"

BUFFER_RW(s_histogram, uint, 0);
IMAGE2D_RW(s_exposure, r32f, 1);

uniform vec4 u_average_params0;
uniform vec4 u_average_params1;
uniform vec4 u_average_params2;

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

SHARED uint shared_histogram[256];

NUM_THREADS(256, 1, 1)
void main()
{
    uint local_idx = gl_LocalInvocationIndex;

    uint bin_count = s_histogram[local_idx];
    shared_histogram[local_idx] = bin_count;

    s_histogram[local_idx] = 0u;

    barrier();

    // Parallel prefix sum (inclusive) for percentile computation
    for (uint step = 1u; step < 256u; step <<= 1u)
    {
        uint val = 0u;
        if (local_idx >= step)
        {
            val = shared_histogram[local_idx - step];
        }
        barrier();
        shared_histogram[local_idx] += val;
        barrier();
    }

    if (local_idx != 0u)
    {
        return;
    }

    uint total_pixels = shared_histogram[255];

    float avg_log_lum;
    if (total_pixels == 0u)
    {
        // Empty histogram (e.g. failed dispatch): use mid-range luminance so we still
        // write a finite exposure and avoid leaving NaN/garbage in the R32F target.
        avg_log_lum = u_min_log_lum + u_log_lum_range * 0.5;
    }
    else
    {
        uint low_count  = uint(float(total_pixels) * u_low_percentile);
        uint high_count = uint(float(total_pixels) * u_high_percentile);

        float weighted_sum = 0.0;
        float weight_total = 0.0;
        uint prev_cumulative = 0u;

        for (uint i = 0u; i < 256u; ++i)
        {
            uint cumulative = shared_histogram[i];
            uint bin_val = cumulative - prev_cumulative;

            if (bin_val > 0u)
            {
                uint active_start = max(prev_cumulative, low_count);
                uint active_end   = min(cumulative, high_count);

                if (active_start < active_end)
                {
                    uint active_count = active_end - active_start;
                    // Exact inverse of the histogram binning (bin = uint(t * 254 + 1)):
                    // t = (bin - 1) / 254. Bin 0 (pure black) maps to the bottom of the range.
                    float t = clamp((float(i) - 1.0) / 254.0, 0.0, 1.0);
                    float bin_center_log_lum = u_min_log_lum + t * u_log_lum_range;

                    weighted_sum += bin_center_log_lum * float(active_count);
                    weight_total += float(active_count);
                }
            }

            prev_cumulative = cumulative;
        }

        if (weight_total > 0.0)
        {
            avg_log_lum = weighted_sum / weight_total;
        }
        else
        {
            avg_log_lum = u_min_log_lum + u_log_lum_range * 0.5;
        }
    }

    // Convert raw log2(luminance) to EV100 using the photographic calibration
    // constant K=12.5 at ISO 100: EV100 = log2(L * 100/12.5) = log2(L) + 3.
    float avg_ev100 = avg_log_lum + 3.0;

    float clamped_ev = clamp(avg_ev100, u_min_ev, u_max_ev);

    // PARTIAL dark adaptation (the single-slope version of UE's Exposure Compensation
    // Curve / Unity HDRP's Curve Remapping): below the neutral point -- the metered EV
    // at which exposure lands at exactly 1 -- only u_dark_adaptation of the deficit is
    // adapted away, so a dark scene keeps (1 - slope) of its true relative darkness
    // instead of being lifted to the mid-gray anchor. Min EV still hard-stops below.
    float neutral_ev = u_compensation - 0.263034; // exposure == 1 at this metered EV
    float dark_deficit = max(0.0, neutral_ev - clamped_ev);
    clamped_ev += dark_deficit * (1.0 - clamp(u_dark_adaptation, 0.0, 1.0));

    // Industry-standard exposure from EV100 (Frostbite/Unreal/Unity):
    // exposure = exp2(-EV100) / 1.2. We adapt in log2 space, so work with the
    // log of that target: log2(exposure) = -EV100 - log2(1.2), plus the EV bias.
    float target_log_exposure = -clamped_ev + u_compensation - 0.263034; // log2(1.2)

    float prev_exposure = imageLoad(s_exposure, ivec2(0, 0)).x;
    // NaN fails (x == x); filter Inf and non-positive garbage from uninitialized storage.
    bool prev_ok = (prev_exposure == prev_exposure) && (prev_exposure > 0.0) && (prev_exposure < 1.0e10);
    float prev_log_exposure = prev_ok ? log2(prev_exposure) : target_log_exposure;

    // Adapt in log2/EV space so the perceived adaptation rate is uniform across the
    // brightness range (the eye responds logarithmically). Brightening the image
    // (target > prev) uses the "up" time constant, darkening uses "down".
    float speed = (target_log_exposure > prev_log_exposure) ? u_speed_up : u_speed_down;
    float adaptation_factor = 1.0 - exp(-u_delta_time / max(speed, 0.001));
    float adapted_log_exposure = prev_log_exposure + (target_log_exposure - prev_log_exposure) * adaptation_factor;

    float adapted_exposure = exp2(adapted_log_exposure);
    if ((adapted_exposure != adapted_exposure) || adapted_exposure <= 0.0 || adapted_exposure >= 1.0e10)
    {
        adapted_exposure = exp2(target_log_exposure);
    }

    imageStore(s_exposure, ivec2(0, 0), vec4(adapted_exposure, 0.0, 0.0, 0.0));
}
