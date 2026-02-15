$input v_texcoord0

/*
 * Bloom downsample pass.
 * Reference: BGFX 38-bloom, Unity HDRP High Quality Prefiltering.
 * 13-tap tent filter for smooth downsampling.
 * Prefilter pass (mip_level==0): full-res 13-tap + optional threshold/soft knee.
 * Pyramid passes (mip_level==1): downsample only, no threshold.
 */

#include "../common.sh"

SAMPLER2D(s_tex, 0);

uniform vec4 u_pixelSize;
uniform vec4 u_params;

#define u_threshold  u_params.x
#define u_soft_knee  u_params.z
#define u_clamp      u_params.w
#define u_mip_level  int(u_params.y)

void main()
{
    vec2 halfpixel = 0.5 * vec2(u_pixelSize.x, u_pixelSize.y);
    vec2 onepixel = 1.0 * vec2(u_pixelSize.x, u_pixelSize.y);

    vec2 uv = v_texcoord0.xy;

    vec4 sum = vec4_splat(0.0);

    sum += (4.0 / 32.0) * texture2D(s_tex, uv);
    sum += (4.0 / 32.0) * texture2D(s_tex, uv + vec2(-halfpixel.x, -halfpixel.y));
    sum += (4.0 / 32.0) * texture2D(s_tex, uv + vec2(+halfpixel.x, +halfpixel.y));
    sum += (4.0 / 32.0) * texture2D(s_tex, uv + vec2(+halfpixel.x, -halfpixel.y));
    sum += (4.0 / 32.0) * texture2D(s_tex, uv + vec2(-halfpixel.x, +halfpixel.y));
    sum += (2.0 / 32.0) * texture2D(s_tex, uv + vec2(+onepixel.x, 0.0));
    sum += (2.0 / 32.0) * texture2D(s_tex, uv + vec2(-onepixel.x, 0.0));
    sum += (2.0 / 32.0) * texture2D(s_tex, uv + vec2(0.0, +onepixel.y));
    sum += (2.0 / 32.0) * texture2D(s_tex, uv + vec2(0.0, -onepixel.y));
    sum += (1.0 / 32.0) * texture2D(s_tex, uv + vec2(+onepixel.x, +onepixel.y));
    sum += (1.0 / 32.0) * texture2D(s_tex, uv + vec2(-onepixel.x, +onepixel.y));
    sum += (1.0 / 32.0) * texture2D(s_tex, uv + vec2(+onepixel.x, -onepixel.y));
    sum += (1.0 / 32.0) * texture2D(s_tex, uv + vec2(-onepixel.x, -onepixel.y));

    vec3 color = sum.rgb;

    if (u_mip_level == 0 && u_clamp > 0.0)
    {
        color = min(color, vec3_splat(u_clamp));
    }
    if (u_mip_level == 0 && u_threshold > 0.0)
    {
        float br = max(max(color.r, color.g), color.b);
        float knee = u_threshold * u_soft_knee + 1e-5;
        float curve_x = u_threshold - knee;
        float curve_y = knee * 2.0;
        float curve_z = 0.25 / knee;

        float rq = clamp(br - curve_x, 0.0, curve_y);
        rq = curve_z * rq * rq;

        float factor = max(rq, br - u_threshold) / max(br, 1e-5);
        color *= factor;
    }

    gl_FragColor = vec4(color, sum.a);
}
