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
    if (total_pixels == 0u)
    {
        return;
    }

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
                float bin_center_log_lum = u_min_log_lum + (float(i) / 255.0) * u_log_lum_range;

                weighted_sum += bin_center_log_lum * float(active_count);
                weight_total += float(active_count);
            }
        }

        prev_cumulative = cumulative;
    }

    float avg_log_lum;
    if (weight_total > 0.0)
    {
        avg_log_lum = weighted_sum / weight_total;
    }
    else
    {
        avg_log_lum = u_min_log_lum + u_log_lum_range * 0.5;
    }

    // Convert raw log2(luminance) to EV100 using the photographic calibration
    // constant K=12.5 at ISO 100: EV100 = log2(L * 100/12.5) = log2(L) + 3.
    float avg_ev100 = avg_log_lum + 3.0;

    float clamped_ev = clamp(avg_ev100, u_min_ev, u_max_ev);

    // Industry-standard exposure from EV100 (Frostbite/Unreal/Unity):
    // exposure = 1 / (1.2 * 2^EV100) = exp2(-EV100) / 1.2
    float target_exposure = exp2(-clamped_ev + u_compensation) / 1.2;

    float prev_exposure = imageLoad(s_exposure, ivec2(0, 0)).x;

    if (prev_exposure <= 0.0)
    {
        prev_exposure = target_exposure;
    }

    float speed = (target_exposure > prev_exposure) ? u_speed_up : u_speed_down;
    float adaptation_factor = 1.0 - exp(-u_delta_time / max(speed, 0.001));
    float adapted_exposure = prev_exposure + (target_exposure - prev_exposure) * adaptation_factor;

    imageStore(s_exposure, ivec2(0, 0), vec4(adapted_exposure, 0.0, 0.0, 0.0));
}
