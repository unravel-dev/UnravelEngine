/*
 * Luminance histogram compute shader for auto-exposure.
 *
 * Builds a 256-bin histogram of log2(luminance) from the HDR scene buffer.
 * Uses shared memory for a per-workgroup local histogram, then atomically
 * merges into the global histogram buffer.
 *
 * Reference: "Automatic Exposure Using a Luminance Histogram" (Frostbite),
 *            Unreal Engine 4/5, Unity HDRP.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_hdr_input, 0);
BUFFER_RW(s_histogram, uint, 1);

uniform vec4 u_histogram_params;

#define u_min_log_lum    u_histogram_params.x
#define u_inv_log_range  u_histogram_params.y
#define u_input_width    u_histogram_params.z
#define u_input_height   u_histogram_params.w

SHARED uint shared_histogram[256];

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

    if (gid.x < uint(u_input_width) && gid.y < uint(u_input_height))
    {
        vec2 uv = (vec2(gid) + vec2(0.5, 0.5)) / vec2(u_input_width, u_input_height);
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

        atomicAdd(shared_histogram[bin_idx], 1u);
    }

    barrier();

    if (local_idx < 256u)
    {
        atomicAdd(s_histogram[local_idx], shared_histogram[local_idx]);
    }
}
