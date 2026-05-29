/*
 * Luminance histogram compute shader for auto-exposure.
 *
 * Builds a 256-bin histogram of log2(luminance) from the HDR scene buffer.
 * Metering is performed on a subsampled grid (see u_meter_width/height) and each
 * sample contributes a spatial weight (average / center-weighted / spot). Uses
 * shared memory for a per-workgroup local histogram, then atomically merges into
 * the global histogram buffer.
 *
 * Bin layout: bin 0 holds pure-black pixels; bins 1..255 hold the normalized
 * log2(luminance) range, mapped so that bin = uint(t * 254 + 1) with t in [0,1].
 * The average shader uses the exact inverse: t = (bin - 1) / 254.
 *
 * Reference: "Automatic Exposure Using a Luminance Histogram" (Frostbite),
 *            Unreal Engine 4/5, Unity HDRP.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_hdr_input, 0);
BUFFER_RW(s_histogram, uint, 1);

uniform vec4 u_histogram_params;
uniform vec4 u_metering_params;

#define u_min_log_lum    u_histogram_params.x
#define u_inv_log_range  u_histogram_params.y
#define u_meter_width    u_histogram_params.z
#define u_meter_height   u_histogram_params.w

#define u_metering_mode  int(u_metering_params.x)
#define u_metering_area  u_metering_params.y

// Fixed-point scale for accumulating fractional spatial weights into a uint histogram.
#define WEIGHT_SCALE 255.0

SHARED uint shared_histogram[256];

float metering_weight(vec2 uv)
{
    // u_metering_mode: 0 = average (uniform), 1 = center-weighted (Gaussian), 2 = spot (hard circle).
    if (u_metering_mode == 0)
    {
        return 1.0;
    }

    vec2 ndc = uv * 2.0 - 1.0;
    float r = length(ndc);
    float area = max(u_metering_area, 1e-3);

    if (u_metering_mode == 2)
    {
        return (r <= area) ? 1.0 : 0.0;
    }

    // Center-weighted: Gaussian falloff with sigma = area.
    return exp(-(r * r) / (2.0 * area * area));
}

NUM_THREADS(16, 16, 1)
void main()
{
    uint local_idx = gl_LocalInvocationIndex;
    if (local_idx < 256u)
    {
        shared_histogram[local_idx] = 0u;
    }

    barrier();

    uvec2 gid = gl_GlobalInvocationID.xy;

    if (gid.x < uint(u_meter_width) && gid.y < uint(u_meter_height))
    {
        vec2 uv = (vec2(gid) + vec2(0.5, 0.5)) / vec2(u_meter_width, u_meter_height);

        uint weight = uint(metering_weight(uv) * WEIGHT_SCALE + 0.5);
        if (weight > 0u)
        {
            vec3 color = texture2DLod(s_hdr_input, uv, 0.0).rgb;
            float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));

            uint bin_idx;
            if (lum < 1e-5)
            {
                bin_idx = 0u;
            }
            else
            {
                float log_lum = clamp((log2(lum) - u_min_log_lum) * u_inv_log_range, 0.0, 1.0);
                bin_idx = uint(log_lum * 254.0 + 1.0);
            }

            atomicAdd(shared_histogram[bin_idx], weight);
        }
    }

    barrier();

    if (local_idx < 256u)
    {
        atomicAdd(s_histogram[local_idx], shared_histogram[local_idx]);
    }
}
