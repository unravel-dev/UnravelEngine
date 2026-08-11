$input v_texcoord0

/*
 * Bloom upsample pass.
 *
 * 9-tap tent filter with per-mip tint, in one of two accumulation modes:
 *
 * SCATTER (u_upsampleParams.x > 0): emits premultiplied (rgb * s, s); the pass
 * blends with ONE / INV_SRC_ALPHA, so the destination becomes exactly
 * mix(dst, upsampled, s) -- the energy-conserving recursive lerp pyramid.
 *
 * LEGACY (u_upsampleParams.x == 0): tint.a-weighted color, rendered with
 * BGFX_STATE_BLEND_ADD, accumulating thresholded highlights as before.
 *
 * u_tint.rgb = per-mip tint color
 * u_tint.a   = per-mip weight (legacy additive weight; scatter folds it into s CPU-side)
 */

#include "../common.sh"

SAMPLER2D(s_tex, 0);

uniform vec4 u_pixelSize;
uniform vec4 u_tint;
uniform vec4 u_upsampleParams;
#define u_hop_scatter u_upsampleParams.x

void main()
{
    vec2 halfpixel = u_pixelSize.xy;
    vec2 uv = v_texcoord0.xy;

    vec4 sum = vec4_splat(0.0);

    sum += (2.0 / 16.0) * texture2D(s_tex, uv + vec2(-halfpixel.x, 0.0));
    sum += (2.0 / 16.0) * texture2D(s_tex, uv + vec2(0.0, halfpixel.y));
    sum += (2.0 / 16.0) * texture2D(s_tex, uv + vec2(halfpixel.x, 0.0));
    sum += (2.0 / 16.0) * texture2D(s_tex, uv + vec2(0.0, -halfpixel.y));
    sum += (1.0 / 16.0) * texture2D(s_tex, uv + vec2(-halfpixel.x, -halfpixel.y));
    sum += (1.0 / 16.0) * texture2D(s_tex, uv + vec2(-halfpixel.x, halfpixel.y));
    sum += (1.0 / 16.0) * texture2D(s_tex, uv + vec2(halfpixel.x, -halfpixel.y));
    sum += (1.0 / 16.0) * texture2D(s_tex, uv + vec2(halfpixel.x, halfpixel.y));
    sum += (4.0 / 16.0) * texture2D(s_tex, uv);

    vec3 filtered = min(max(u_tint.rgb * sum.rgb, vec3_splat(0.0)), vec3_splat(65504.0));
    if (u_hop_scatter > 0.0)
    {
        gl_FragColor = vec4(filtered * u_hop_scatter, u_hop_scatter);
    }
    else
    {
        gl_FragColor = vec4(filtered * u_tint.a, 1.0);
    }
}
