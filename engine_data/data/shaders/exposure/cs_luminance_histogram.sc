/*
 * Luminance histogram compute shader for auto exposure.
 *
 * Builds a 256-bin histogram of log2(luminance) from the HDR scene buffer on a metering grid
 * of one cell per 4x4 source texels. Each cell averages its block with four bilinear taps on
 * the block's inner corners, so every source texel contributes (UE meters every texel of a
 * filtered downsample). Each sample carries a spatial weight (average / center-weighted /
 * spot) and is split linearly between the two bins around its position (UE Histogram.usf),
 * which keeps the metered value continuous as luminance moves across bin boundaries.
 * Luminance is UE's eye adaptation luminance: uniform RGB weights
 * (r.AutoExposure.LuminanceMethod 0), so saturated blue or red light meters as UE meters it.
 *
 * Bin layout: bin 0 holds black samples (the average ignores it, like UE's
 * r.EyeAdaptation.BlackHistogramBucketInfluence = 0); bins 1..255 cover the log2 luminance
 * range, with position = t * 254 + 1 for t in [0, 1] and bin i centred at t = (i - 1) / 254.
 * Black is UE's: anything at or below the bottom of the range. UE's histogram has 64 buckets
 * and its bucket 0 carries no weight, so a sample within one bucket width of the floor keeps
 * only the share it splits into bucket 1; the floor ramp below reproduces that.
 *
 * Uses shared memory for a per-workgroup local histogram, then atomically merges into the
 * global histogram buffer.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_hdr_input, 0);
BUFFER_RW(s_histogram, uint, 1);
// Images take i_ names, never a sampler's: the OpenGL backend uploads every registered uniform
// a program declares, so an image named like a sampler is rebound to that sampler's stage.
/// Per metering cell, its log2 SCENE luminance (pre-exposure removed) - the input the local
/// exposure chain bins into its bilateral grid (cs_local_exposure_grid.sc). Written for EVERY
/// cell, including the ones the metering weight drops: local exposure covers the whole frame,
/// while the histogram only meters where the mode says. One texel per cell against a gate
/// uniform and a branch is the cheaper trade.
IMAGE2D_WO(i_exposure_log_lum, r16f, 2);

uniform vec4 u_histogram_params;
uniform vec4 u_metering_params;
uniform vec4 u_metering_cell;

#define u_min_log_lum        u_histogram_params.x
#define u_inv_log_range      u_histogram_params.y
#define u_meter_width        u_histogram_params.z
#define u_meter_height       u_histogram_params.w

#define u_metering_mode      int(u_metering_params.x)
#define u_metering_area      u_metering_params.y
#define u_aspect_ratio       u_metering_params.z
#define u_log2_pre_exposure  u_metering_params.w

#define u_cell_texels        u_metering_cell.xy
#define u_texel_size         u_metering_cell.zw

// Fixed-point scale for fractional sample weights (spatial weight x bin share). 1024 x the
// 1024x576 grid cap stays below 2^32 even if every sample lands in one bin.
#define WEIGHT_SCALE 1024.0
// UE's histogram bucket count (FHistogramAtomicCS::HistogramSize): the floor ramp spans one of
// its buckets.
#define UE_HISTOGRAM_BUCKETS 64.0
#define FIRST_LUMINANCE_BIN 1.0
#define LUMINANCE_BIN_SPAN 254.0
#define LAST_BIN 255u

SHARED uint shared_histogram[256];

float metering_weight(vec2 uv)
{
    // u_metering_mode: 0 = average (uniform), 1 = center-weighted (Gaussian), 2 = spot (hard circle).
    if (u_metering_mode == 0)
    {
        return 1.0;
    }

    // Distance in units of half the screen height, so the region stays round at any aspect.
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.x *= u_aspect_ratio;
    float r = length(ndc);
    float area = max(u_metering_area, 1e-3);

    if (u_metering_mode == 2)
    {
        return (r <= area) ? 1.0 : 0.0;
    }

    return exp(-(r * r) / (2.0 * area * area));
}

vec3 sample_cell_color(vec2 cell_center_texel)
{
    vec2 offset = u_cell_texels * 0.25;
    vec3 color = texture2DLod(s_hdr_input, (cell_center_texel + vec2(-offset.x, -offset.y)) * u_texel_size, 0.0).rgb;
    color += texture2DLod(s_hdr_input, (cell_center_texel + vec2( offset.x, -offset.y)) * u_texel_size, 0.0).rgb;
    color += texture2DLod(s_hdr_input, (cell_center_texel + vec2(-offset.x,  offset.y)) * u_texel_size, 0.0).rgb;
    color += texture2DLod(s_hdr_input, (cell_center_texel + vec2( offset.x,  offset.y)) * u_texel_size, 0.0).rgb;
    return color * 0.25;
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
        vec2 cell = vec2(gid) + vec2(0.5, 0.5);
        float spatial_weight = metering_weight(cell / vec2(u_meter_width, u_meter_height));
        vec3 color = sample_cell_color(cell * u_cell_texels);
        float lum = dot(color, vec3_splat(1.0 / 3.0));
        // NaN or negative lighting reads as black.
        lum = (lum > 0.0) ? lum : 0.0;
        // Scene luminance: the input may carry the view's pre-exposure. UE floors it at the bottom
        // of the histogram range (CalculateEyeAdaptationLuminance's LuminanceMin).
        float log_lum = max(log2(max(lum, 1e-30)) - u_log2_pre_exposure, u_min_log_lum);
        imageStore(i_exposure_log_lum, ivec2(gid), vec4(log_lum, 0.0, 0.0, 0.0));

        float histogram_position = (log_lum - u_min_log_lum) * u_inv_log_range;
        float metered_share = saturate(histogram_position * (UE_HISTOGRAM_BUCKETS - 1.0));
        float metered_weight = spatial_weight * metered_share;
        if (spatial_weight > 0.0)
        {
            atomicAdd(shared_histogram[0], uint((spatial_weight - metered_weight) * WEIGHT_SCALE + 0.5));
        }
        if (metered_weight > 0.0)
        {
            float position = saturate(histogram_position) * LUMINANCE_BIN_SPAN + FIRST_LUMINANCE_BIN;
            float lower_position = floor(position);
            float upper_share = position - lower_position;
            uint lower_bin = min(uint(lower_position), LAST_BIN);
            uint upper_bin = min(lower_bin + 1u, LAST_BIN);
            atomicAdd(shared_histogram[lower_bin], uint(metered_weight * (1.0 - upper_share) * WEIGHT_SCALE + 0.5));
            atomicAdd(shared_histogram[upper_bin], uint(metered_weight * upper_share * WEIGHT_SCALE + 0.5));
        }
    }

    barrier();

    if (local_idx < 256u)
    {
        atomicAdd(s_histogram[local_idx], shared_histogram[local_idx]);
    }
}
