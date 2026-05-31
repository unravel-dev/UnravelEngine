/*
 * Geometry-aware 2x downsample for the mixed-resolution SSIL denoiser.
 *
 * The wide a-trous passes are the expensive ones: at large dilation their 24 taps scatter
 * across a huge full-res region and thrash the texture cache, so each successive pass costs
 * far more than the last. Indirect diffuse is low-frequency, though, so the WIDE blur can be
 * computed at half resolution for ~4x fewer pixels (and cache-coherent taps) with no
 * perceptible quality loss; sharp silhouettes are restored afterwards by the bilateral
 * upsample (fs_ssil_upsample), which rejects cross-edge taps using the full-res G-buffer.
 *
 * This pass produces that half-res input. A plain box downsample would average colour across
 * depth/normal discontinuities and bleed indirect light through silhouettes, so each of the
 * 2x2 source texels is weighted by its geometric similarity (relative view-space depth +
 * world-normal) to the block centre. Colour AND the confidence alpha are carried so the
 * downstream a-trous passes keep their confidence-weighted accumulation.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"

SAMPLER2D(s_ssil_input, 0); // higher-res SSIL colour + confidence
IMAGE2D_WO(i_ssil_output, rgba16f, 1);
SAMPLER2D(s_normal, 2); // full-res G-buffer normal
SAMPLER2D(s_depth, 3);  // full-res G-buffer depth

uniform vec4 u_downsample_params;
#define u_depth_sigma  u_downsample_params.x
#define u_normal_power u_downsample_params.y

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 lr_size = imageSize(i_ssil_output);
    if(any(greaterThanEqual(coord, lr_size)))
        return;

    ivec2 hr_size = textureSize(s_ssil_input, 0);
    vec2 depth_dim = vec2(textureSize(s_depth, 0));

    vec2 lr_uv = (vec2(coord) + 0.5) / vec2(lr_size);
    vec2 lr_scale = depth_dim / max(vec2(lr_size), vec2_splat(1.0));
    vec2 center_full_uv = HizScreenPassToFullResUV(lr_uv, lr_scale, depth_dim);
    float center_depth = DecodeGBufferDepthLod(center_full_uv, s_depth, 0.0).depth01;

    // Sky / background: emit nothing so the wide blur never drags geometry SSIL into it.
    BRANCH
#ifdef INVERTED_DEPTH_RANGE
    if(center_depth == 0.0)
#else
    if(center_depth == 1.0)
#endif
    {
        imageStore(i_ssil_output, coord, vec4_splat(0.0));
        return;
    }

    vec3 center_normal = DecodeGBufferNormalMetalRoughnessLod(center_full_uv, s_normal, 0.0).world_normal;
    float center_lin = max(abs(computeViewSpacePosition(center_full_uv, center_depth).z), 1e-3);

    vec2 hr_scale = depth_dim / max(vec2(hr_size), vec2_splat(1.0));
    ivec2 base = coord * 2;

    vec4 accum = vec4_splat(0.0);
    float total_w = 0.0;

    for(int dy = 0; dy <= 1; ++dy)
    {
        for(int dx = 0; dx <= 1; ++dx)
        {
            ivec2 tc = min(base + ivec2(dx, dy), hr_size - ivec2(1, 1));
            vec2 tap_uv = (vec2(tc) + 0.5) / vec2(hr_size);
            vec2 tap_full_uv = HizScreenPassToFullResUV(tap_uv, hr_scale, depth_dim);

            float gd = DecodeGBufferDepthLod(tap_full_uv, s_depth, 0.0).depth01;
            float g_lin = abs(computeViewSpacePosition(tap_full_uv, gd).z);
            float dw = exp(-abs(center_lin - g_lin) / max(u_depth_sigma * center_lin, 1e-4));

            vec3 gn = DecodeGBufferNormalMetalRoughnessLod(tap_full_uv, s_normal, 0.0).world_normal;
            float nw = pow(max(0.0, dot(center_normal, gn)), u_normal_power);

            float w = dw * nw;
            accum += texelFetch(s_ssil_input, tc, 0) * w;
            total_w += w;
        }
    }

    // Fall back to the matching block texel if every tap was rejected (thin feature) so the
    // half-res pixel is never left black.
    vec4 result = (total_w > 1e-4) ? (accum / total_w) : texelFetch(s_ssil_input, base, 0);
    imageStore(i_ssil_output, coord, result);
}
