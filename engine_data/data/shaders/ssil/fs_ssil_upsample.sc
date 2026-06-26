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
/// zw: explicit target (output) dimensions in pixels. Required because this shader is
///     reused for both the trace->full output upsample AND the half-of-trace->trace
///     intra-denoise upsample; using textureSize(s_depth, 0) for the target dim would
///     compute the wrong scale (and over-wide kernel) for the latter, where the output
///     is at trace res but the G-buffer is at full res. Falls back to s_depth dim when
///     the uniform is unset (zero), preserving the old behaviour for the outer call.
#define u_depth_sigma  u_upsample_params.x
#define u_normal_power u_upsample_params.y
#define u_target_dim   u_upsample_params.zw

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

    vec2 gbuf_dim = vec2(textureSize(s_depth, 0));
    vec2 lr_dim = vec2(textureSize(s_ssil_input, 0));
    // Target (output) dim drives the reconstruction footprint. The shader is reused for
    // (a) trace->full output upsample (target = full G-buffer) and (b) the intra-denoise
    // half-of-trace->trace upsample (target = trace res). Inferring target from s_depth
    // would over-state the scale (and over-widen the kernel) in case (b).
    vec2 target_dim = (u_target_dim.x > 0.5 && u_target_dim.y > 0.5) ? u_target_dim : gbuf_dim;
    vec2 resolution_scale = target_dim / max(lr_dim, vec2_splat(1.0));

    // Low-res sample grid position for this full-res pixel (texel centres at integer + 0.5).
    vec2 lr_coordf = full_uv * lr_dim - vec2_splat(0.5);
    vec2 base = floor(lr_coordf);
    vec2 frac_uv = lr_coordf - base;

    // A 2x2 gather only spans one low-res texel; at quarter/eighth trace res a full-res
    // pixel falls between samples that are 4-8 texels apart, so 2x2 cannot cover the
    // reconstruction footprint (blocky edges / leaks). Widen the gather with the scale:
    // half -> radius 1 (the original 2x2), quarter -> 2 (4x4), eighth -> 3 (6x6).
    int up_radius = int(clamp(ceil(max(resolution_scale.x, resolution_scale.y) * 0.5), 1.0, 3.0));

    vec4 accum = vec4_splat(0.0);
    float total_w = 0.0;

    // Single-tap fallback for the degenerate case where every weighted tap is rejected.
    // Bilinear blends across the very depth/normal edges we are trying to preserve, so
    // for grazing/near surfaces and silhouette pixels it leaks brighter neighbours across
    // dark interiors (halos). Track the highest-weight tap as we go and use it as the
    // fallback instead: it is the single low-res sample most likely to belong to the
    // full-res surface, and it never leaks because it is a single texel value -- never a
    // blend across an edge.
    vec4 best_val = vec4_splat(0.0);
    float best_geom_w = 0.0;

    // Static bounds cover the max radius (3); taps outside the active radius are skipped.
    for(int dy = -2; dy <= 3; ++dy)
    {
        for(int dx = -2; dx <= 3; ++dx)
        {
            if(dx < 1 - up_radius || dx > up_radius || dy < 1 - up_radius || dy > up_radius)
                continue;

            vec2 off = vec2(float(dx), float(dy));
            ivec2 tc = ivec2(clamp(base + off, vec2_splat(0.0), lr_dim - vec2_splat(1.0)));

            // Separable tent footprint of half-width up_radius. At radius 1 it reduces
            // exactly to the bilinear weights; wider radii spread the footprint smoothly.
            vec2 fd = abs(off - frac_uv) / float(up_radius);
            float bw = max(0.0, 1.0 - fd.x) * max(0.0, 1.0 - fd.y);
            BRANCH
            if(bw <= 0.0)
                continue;

            // The low-res tap centre UV already maps to the centre of its full-res block,
            // so it is the correct geometry-guide UV. Snapping it through
            // HizScreenPassToFullResUV would bias the lookup by +0.5px relative to the tap.
            vec2 guide_uv = (vec2(tc) + vec2_splat(0.5)) / lr_dim;

            float gd = DecodeGBufferDepth(guide_uv, s_depth).depth01;
            float g_lin = abs(computeViewSpacePosition(guide_uv, gd).z);
            // Relative (scale-invariant) depth weight: tolerant near and far, so a
            // grazing wall is not falsely rejected the way an absolute depth01
            // threshold would.
            float dw = exp(-abs(center_lin - g_lin) / max(u_depth_sigma * center_lin, 1e-4));

            vec3 gn = DecodeGBufferNormalMetalRoughness(guide_uv, s_normal).world_normal;
            float nw = pow(max(0.0, dot(center_normal, gn)), u_normal_power);

            vec4 sample_val = texelFetch(s_ssil_input, tc, 0);

            // "Best geometry tap": rank by depth*normal alone (drop the bilinear footprint
            // factor) so a perfectly-matched tap at the kernel edge can still win over a
            // mediocre tap at the centre. This is the tap we pick if total_w collapses.
            float geom_only = dw * nw;
            if(geom_only > best_geom_w)
            {
                best_geom_w = geom_only;
                best_val = sample_val;
            }

            float w = bw * dw * nw;
            accum += sample_val * w;
            total_w += w;
        }
    }

    // total_w in [0,1] measures how well the edge-aware taps matched the full-res
    // surface. Blend the bilateral mean (when reliable) toward the single best-matched
    // tap as the matching degrades. We never fall back to hardware-bilinear: bilinear
    // explicitly blends across the depth/normal edges this pass exists to preserve,
    // producing visible bright leaks around silhouettes and dark halos around thin
    // features. The best-tap fallback never leaks because it is a single texel value.
    vec4 bilateral = (total_w > 1e-4) ? (accum / total_w) : best_val;
    gl_FragColor = mix(best_val, bilateral, clamp(total_w, 0.0, 1.0));
}
