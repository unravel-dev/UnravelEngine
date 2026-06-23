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
 * world-normal) to the block centre. RGB radiance and alpha coverage are carried together.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"

SAMPLER2D(s_ssil_input, 0); // higher-res SSIL radiance + coverage
IMAGE2D_WO(i_ssil_output, rgba16f, 1);
SAMPLER2D(s_normal, 2); // full-res G-buffer normal
SAMPLER2D(s_depth, 3);  // full-res G-buffer depth
SAMPLER2D(s_ssil_variance, 4);
IMAGE2D_WO(i_ssil_variance_output, r16f, 5);

uniform vec4 u_downsample_params;
#define u_depth_sigma  u_downsample_params.x
#define u_normal_power u_downsample_params.y
#define u_has_variance u_downsample_params.z

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 lr_size = imageSize(i_ssil_output);
    if(any(greaterThanEqual(coord, lr_size)))
        return;

    ivec2 hr_size = textureSize(s_ssil_input, 0);

    // The half-res block-centre UV already maps to the centre of the full-res region this
    // block covers, so it is the correct geometry-guide UV. Snapping it through
    // HizScreenPassToFullResUV would bias the lookup by +0.5px relative to the SSIL block.
    vec2 lr_uv = (vec2(coord) + 0.5) / vec2(lr_size);
    vec2 center_full_uv = lr_uv;
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
        imageStore(i_ssil_variance_output, coord, vec4_splat(0.0));
        return;
    }

    vec3 center_normal = DecodeGBufferNormalMetalRoughnessLod(center_full_uv, s_normal, 0.0).world_normal;
    float center_lin = max(abs(computeViewSpacePosition(center_full_uv, center_depth).z), 1e-3);

    ivec2 base = coord * 2;

    vec4 accum = vec4_splat(0.0);
    float variance_accum = 0.0;
    float total_w = 0.0;

    for(int dy = 0; dy <= 1; ++dy)
    {
        for(int dx = 0; dx <= 1; ++dx)
        {
            ivec2 tc = min(base + ivec2(dx, dy), hr_size - ivec2(1, 1));
            vec2 tap_full_uv = (vec2(tc) + 0.5) / vec2(hr_size);

            float gd = DecodeGBufferDepthLod(tap_full_uv, s_depth, 0.0).depth01;
            float g_lin = abs(computeViewSpacePosition(tap_full_uv, gd).z);
            float dw = exp(-abs(center_lin - g_lin) / max(u_depth_sigma * center_lin, 1e-4));

            vec3 gn = DecodeGBufferNormalMetalRoughnessLod(tap_full_uv, s_normal, 0.0).world_normal;
            float nw = pow(max(0.0, dot(center_normal, gn)), u_normal_power);

            float w = dw * nw;
            accum += texelFetch(s_ssil_input, tc, 0) * w;
            BRANCH
            if(u_has_variance > 0.5)
            {
                variance_accum += texelFetch(s_ssil_variance, tc, 0).r * w * w;
            }
            total_w += w;
        }
    }

    // Fall back to the matching block texel if every tap was rejected (thin feature) so the
    // half-res pixel is never left black.
    vec4 result = (total_w > 1e-4) ? (accum / total_w) : texelFetch(s_ssil_input, base, 0);
    float variance = (u_has_variance > 0.5)
                         ? ((total_w > 1e-4)
                                ? (variance_accum / max(total_w * total_w, 1e-6))
                                : texelFetch(s_ssil_variance, base, 0).r)
                         : 0.0;
    imageStore(i_ssil_output, coord, result);
    imageStore(i_ssil_variance_output, coord, vec4_splat(variance));
}
