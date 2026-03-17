$input v_texcoord0

/*
 * Bloom upsample pass.
 *
 * 9-tap tent filter with per-mip tint and weight.
 * Rendered with BGFX_STATE_BLEND_ADD when accumulating into previous level.
 *
 * u_tint.rgb = per-mip tint color
 * u_tint.a   = effective weight (global intensity * per-mip alpha)
 */

#include "../common.sh"

SAMPLER2D(s_tex, 0);

uniform vec4 u_pixelSize;
uniform vec4 u_tint;

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

    vec3 result = min(max(u_tint.rgb * u_tint.a * sum.rgb, vec3_splat(0.0)), vec3_splat(65504.0));
    gl_FragColor = vec4(result, 1.0);
}
