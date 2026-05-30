$input v_texcoord0

/*
 * Joint-bilateral upsample for reduced-resolution SSIL.
 *
 * When the trace runs at half/quarter/eighth resolution the indirect-lighting
 * pass would otherwise upsample the SSIL buffer with a plain hardware-bilinear
 * tap, which bleeds bright indirect light across depth and normal
 * discontinuities (halos around objects, light leaking through silhouettes).
 *
 * This pass runs at full resolution and reconstructs each full-res pixel from
 * the 2x2 surrounding low-res texels, weighting each tap by:
 *   - bilinear footprint weight,
 *   - depth similarity (full-res surface depth vs the low-res tap's block depth),
 *   - normal similarity,
 * so taps that belong to a different surface are rejected. If every tap is
 * rejected (e.g. a thin feature with no matching low-res sample) we fall back to
 * the plain bilinear fetch so the pixel is never left black.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"

SAMPLER2D(s_ssil_input, 0); // low-res (trace-res) denoised SSIL
SAMPLER2D(s_normal, 1);     // full-res G-buffer normal
SAMPLER2D(s_depth, 2);      // full-res G-buffer depth

uniform vec4 u_upsample_params;
/// x: relative view-space depth tolerance (fraction of linear depth). Absolute
///    device-depth (depth01) tolerances over-reject on grazing/near surfaces where
///    depth01 changes fast across a low-res block, collapsing the wall to black.
#define u_depth_sigma  u_upsample_params.x
#define u_normal_power u_upsample_params.y

void main()
{
    vec2 full_uv = v_texcoord0;

    float center_depth = DecodeGBufferDepth(full_uv, s_depth).depth01;

    // Sky / background carries no indirect signal; emit zero so the bilinear
    // footprint never drags geometry SSIL into the background.
    BRANCH
#ifdef INVERTED_DEPTH_RANGE
    if(center_depth == 0.0)
#else
    if(center_depth == 1.0)
#endif
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 center_normal = DecodeGBufferNormalMetalRoughness(full_uv, s_normal).world_normal;
    float center_lin = abs(computeViewSpacePosition(full_uv, center_depth).z);

    vec2 full_dim = vec2(textureSize(s_depth, 0));
    vec2 lr_dim = vec2(textureSize(s_ssil_input, 0));
    vec2 resolution_scale = full_dim / max(lr_dim, vec2_splat(1.0));

    // Low-res sample grid position for this full-res pixel (texel centres at integer + 0.5).
    vec2 lr_coordf = full_uv * lr_dim - vec2_splat(0.5);
    vec2 base = floor(lr_coordf);
    vec2 frac_uv = lr_coordf - base;

    vec4 accum = vec4_splat(0.0);
    float total_w = 0.0;

    for(int dy = 0; dy <= 1; ++dy)
    {
        for(int dx = 0; dx <= 1; ++dx)
        {
            vec2 off = vec2(float(dx), float(dy));
            ivec2 tc = ivec2(clamp(base + off, vec2_splat(0.0), lr_dim - vec2_splat(1.0)));

            float bw = (dx == 0 ? (1.0 - frac_uv.x) : frac_uv.x) *
                       (dy == 0 ? (1.0 - frac_uv.y) : frac_uv.y);

            vec2 tap_uv = (vec2(tc) + vec2_splat(0.5)) / lr_dim;
            // Full-res block centre the low-res tap was traced from -- gives a
            // representative full-res depth/normal for that tap to compare against.
            vec2 guide_uv = HizScreenPassToFullResUV(tap_uv, resolution_scale, full_dim);

            float gd = DecodeGBufferDepth(guide_uv, s_depth).depth01;
            float g_lin = abs(computeViewSpacePosition(guide_uv, gd).z);
            // Relative (scale-invariant) depth weight: tolerant near and far, so a
            // grazing wall is not falsely rejected the way an absolute depth01
            // threshold would.
            float dw = exp(-abs(center_lin - g_lin) / max(u_depth_sigma * center_lin, 1e-4));

            vec3 gn = DecodeGBufferNormalMetalRoughness(guide_uv, s_normal).world_normal;
            float nw = pow(max(0.0, dot(center_normal, gn)), u_normal_power);

            float w = bw * dw * nw;
            accum += texelFetch(s_ssil_input, tc, 0) * w;
            total_w += w;
        }
    }

    // Plain bilinear is the safe baseline: it is always lit where the low-res buffer
    // is lit. total_w in [0,1] measures how well the edge-aware taps matched the
    // full-res surface; blend toward the bilateral result by that confidence so
    // strongly-matched pixels stay edge-preserving while weakly-matched ones
    // (grazing/near surfaces, screen edges) gracefully fall back to bilinear and
    // never collapse to black.
    vec4 bilinear = texture2D(s_ssil_input, full_uv);
    vec4 bilateral = (total_w > 1e-4) ? (accum / total_w) : bilinear;
    gl_FragColor = mix(bilinear, bilateral, clamp(total_w, 0.0, 1.0));
}
