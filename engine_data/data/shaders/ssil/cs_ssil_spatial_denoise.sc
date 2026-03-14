/*
 * Variance-guided a-trous wavelet spatial denoiser for SSIL.
 *
 * Run 3 times with step_size=1,2,4 (ping-ponging buffers).
 * Uses the alpha channel (temporal sample count) as a variance proxy:
 * low alpha -> high variance boost -> more aggressive blurring.
 * Follows the FidelityFX denoiser approach (SVGF-style).
 *
 * Uniforms (u_denoise_params):
 *   x: step_size    - a-trous step (1, 2, 4)
 *   y: depth_sigma  - depth edge-stopping threshold (~0.01-0.05)
 *   z: normal_power - normal edge-stopping exponent (~32-128)
 *   w: luma_sigma   - luminance edge-stopping threshold (~0.5-2.0)
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_ssil_input, 0);
IMAGE2D_WO(i_ssil_output, rgba16f, 1);
SAMPLER2D(s_normal, 2);
SAMPLER2D(s_depth, 3);

uniform vec4 u_denoise_params;
#define u_step_size    u_denoise_params.x
#define u_depth_sigma  u_denoise_params.y
#define u_normal_power u_denoise_params.z
#define u_luma_sigma   u_denoise_params.w

#define KW0 0.375
#define KW1 0.25
#define KW2 0.0625

float ssil_kernel_weight(int dx, int dy)
{
    int ax = abs(dx);
    int ay = abs(dy);
    float wx = (ax == 0) ? KW0 : ((ax == 1) ? KW1 : KW2);
    float wy = (ay == 0) ? KW0 : ((ay == 1) ? KW1 : KW2);
    return wx * wy;
}

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(i_ssil_output);
    if(any(greaterThanEqual(coord, size)))
        return;

    vec2 texel_size = 1.0 / vec2(size);
    vec2 uv = (vec2(coord) + 0.5) * texel_size;

    vec4 center = texelFetch(s_ssil_input, coord, 0);

    BRANCH
    if(center.a <= 0.0)
    {
        imageStore(i_ssil_output, coord, center);
        return;
    }

    float center_depth = DecodeGBufferDepthLod(uv, s_depth, 0.0).depth01;
    vec3 center_normal = DecodeGBufferNormalMetalRoughnessLod(uv, s_normal, 0.0).world_normal;
    float center_luma = Luminance(center.rgb);
    int step_val = int(u_step_size);

    // FidelityFX-style variance boost: alpha encodes normalized temporal
    // sample count (0 = just arrived, 1 = fully accumulated). Low sample
    // count -> high variance_boost -> relaxed luminance edge-stopping so
    // the filter blurs more aggressively over disoccluded regions.
    float center_conf = clamp(center.a, 0.01, 1.0);
    float variance_boost = max(1.0 / center_conf, 1.0);
    float effective_luma_sigma = u_luma_sigma * variance_boost;

    float center_kw = ssil_kernel_weight(0, 0) * center_conf;
    vec4 result = center * center_kw;
    float total_w = center_kw;
    float alpha_sum = center.a * center_kw;

    for(int dy = -2; dy <= 2; dy++)
    {
        for(int dx = -2; dx <= 2; dx++)
        {
            if(dx == 0 && dy == 0)
                continue;

            ivec2 sc = coord + ivec2(dx, dy) * step_val;
            if(any(lessThan(sc, ivec2(0, 0))) || any(greaterThanEqual(sc, size)))
                continue;

            float kw = ssil_kernel_weight(dx, dy);
            vec4 s = texelFetch(s_ssil_input, sc, 0);
            vec2 suv = (vec2(sc) + 0.5) * texel_size;

            float sd = DecodeGBufferDepthLod(suv, s_depth, 0.0).depth01;
            float dw = exp(-abs(center_depth - sd) / max(u_depth_sigma, 1e-6));

            vec3 sn = DecodeGBufferNormalMetalRoughnessLod(suv, s_normal, 0.0).world_normal;
            float nw = pow(max(0.0, dot(center_normal, sn)), u_normal_power);

            float sl = Luminance(s.rgb);
            float lw = exp(-abs(center_luma - sl) / max(effective_luma_sigma, 1e-6));

            // When center is low-confidence, prefer high-confidence neighbors
            float cw = mix(s.a, 1.0, center_conf);

            float w = kw * dw * nw * lw * cw;
            result += s * w;
            total_w += w;
            alpha_sum += s.a * w;
        }
    }

    result /= max(total_w, 1e-6);
    // Propagate blended alpha so subsequent passes see improved confidence
    result.a = alpha_sum / max(total_w, 1e-6);
    imageStore(i_ssil_output, coord, result);
}
