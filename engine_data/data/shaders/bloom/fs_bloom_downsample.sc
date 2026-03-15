$input v_texcoord0

/*
 * Bloom downsample pass with Karis anti-flicker.
 *
 * Reference: "Next Generation Post Processing in Call of Duty: Advanced Warfare"
 *            (Jimenez, SIGGRAPH 2014), Unity HDRP, Unreal Engine 4/5.
 *
 * First pass (u_mip_level == 0):
 *   13-tap downsample with partial Karis average to suppress sub-pixel
 *   specular flicker, plus threshold/soft-knee prefilter.
 *   Reads full-res, writes half-res.
 *
 * Subsequent passes (u_mip_level != 0):
 *   Standard 13-tap tent downsample, no threshold.
 *
 * u_pixelSize must be the SOURCE texture's texel size.
 */

#include "../common.sh"

SAMPLER2D(s_tex, 0);

uniform vec4 u_pixelSize;
uniform vec4 u_params;

#define u_threshold  u_params.x
#define u_soft_knee  u_params.z
#define u_clamp      u_params.w
#define u_mip_level  int(u_params.y)

float luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 apply_threshold(vec3 color)
{
    float br = max(max(color.r, color.g), color.b);
    float knee = u_threshold * u_soft_knee + 1e-5;
    float curve_x = u_threshold - knee;
    float curve_y = knee * 2.0;
    float curve_z = 0.25 / knee;

    float rq = clamp(br - curve_x, 0.0, curve_y);
    rq = curve_z * rq * rq;

    float factor = max(rq, br - u_threshold) / max(br, 1e-5);
    return color * factor;
}

void main()
{
    vec2 ps = vec2(u_pixelSize.x, u_pixelSize.y);
    vec2 uv = v_texcoord0.xy;

    if (u_mip_level == 0)
    {
        /*
         * First downsample with partial Karis average.
         *
         * 13 taps in source-texel offsets:
         *
         *   a . b . c       Groups (overlapping 2x2 quads):
         *   . j . k .         g0 = {a,b,d,e}  g1 = {b,c,e,f}
         *   d . e . f         g2 = {d,e,g,h}  g3 = {e,f,h,i}
         *   . l . m .         g4 = {j,k,l,m}  (center diamond)
         *   g . h . i
         *
         * Corner groups get 0.125 weight each (0.5 total).
         * Center diamond gets 0.5 weight.
         * Each group is weighted by 1/(1+luma) to suppress bright outliers.
         */

        vec3 a  = texture2D(s_tex, uv + vec2(-1.0, -1.0) * ps).rgb;
        vec3 b  = texture2D(s_tex, uv + vec2( 0.0, -1.0) * ps).rgb;
        vec3 c  = texture2D(s_tex, uv + vec2( 1.0, -1.0) * ps).rgb;
        vec3 d  = texture2D(s_tex, uv + vec2(-1.0,  0.0) * ps).rgb;
        vec3 e  = texture2D(s_tex, uv).rgb;
        vec3 f  = texture2D(s_tex, uv + vec2( 1.0,  0.0) * ps).rgb;
        vec3 g  = texture2D(s_tex, uv + vec2(-1.0,  1.0) * ps).rgb;
        vec3 h  = texture2D(s_tex, uv + vec2( 0.0,  1.0) * ps).rgb;
        vec3 i  = texture2D(s_tex, uv + vec2( 1.0,  1.0) * ps).rgb;
        vec3 j  = texture2D(s_tex, uv + vec2(-0.5, -0.5) * ps).rgb;
        vec3 k  = texture2D(s_tex, uv + vec2( 0.5, -0.5) * ps).rgb;
        vec3 l  = texture2D(s_tex, uv + vec2(-0.5,  0.5) * ps).rgb;
        vec3 m  = texture2D(s_tex, uv + vec2( 0.5,  0.5) * ps).rgb;

        if (u_clamp > 0.0)
        {
            vec3 cv = vec3_splat(u_clamp);
            a = min(a, cv); b = min(b, cv); c = min(c, cv);
            d = min(d, cv); e = min(e, cv); f = min(f, cv);
            g = min(g, cv); h = min(h, cv); i = min(i, cv);
            j = min(j, cv); k = min(k, cv); l = min(l, cv); m = min(m, cv);
        }

        vec3 g0 = (a + b + d + e) * 0.25;
        vec3 g1 = (b + c + e + f) * 0.25;
        vec3 g2 = (d + e + g + h) * 0.25;
        vec3 g3 = (e + f + h + i) * 0.25;
        vec3 g4 = (j + k + l + m) * 0.25;

        float kw0 = 1.0 / (1.0 + luminance(g0));
        float kw1 = 1.0 / (1.0 + luminance(g1));
        float kw2 = 1.0 / (1.0 + luminance(g2));
        float kw3 = 1.0 / (1.0 + luminance(g3));
        float kw4 = 1.0 / (1.0 + luminance(g4));

        float corner_weight = 0.125;
        float center_weight = 0.5;

        vec3 color = corner_weight * (g0 * kw0 + g1 * kw1 + g2 * kw2 + g3 * kw3)
                   + center_weight * g4 * kw4;
        float w_sum = corner_weight * (kw0 + kw1 + kw2 + kw3) + center_weight * kw4;
        color /= w_sum;

        if (u_threshold > 0.0)
        {
            color = apply_threshold(color);
        }

        gl_FragColor = vec4(color, 1.0);
    }
    else
    {
        vec2 hp = 0.5 * ps;

        vec4 sum = vec4_splat(0.0);

        sum += (4.0 / 32.0) * texture2D(s_tex, uv);
        sum += (4.0 / 32.0) * texture2D(s_tex, uv + vec2(-hp.x, -hp.y));
        sum += (4.0 / 32.0) * texture2D(s_tex, uv + vec2( hp.x,  hp.y));
        sum += (4.0 / 32.0) * texture2D(s_tex, uv + vec2( hp.x, -hp.y));
        sum += (4.0 / 32.0) * texture2D(s_tex, uv + vec2(-hp.x,  hp.y));
        sum += (2.0 / 32.0) * texture2D(s_tex, uv + vec2( ps.x,  0.0));
        sum += (2.0 / 32.0) * texture2D(s_tex, uv + vec2(-ps.x,  0.0));
        sum += (2.0 / 32.0) * texture2D(s_tex, uv + vec2( 0.0,   ps.y));
        sum += (2.0 / 32.0) * texture2D(s_tex, uv + vec2( 0.0,  -ps.y));
        sum += (1.0 / 32.0) * texture2D(s_tex, uv + vec2( ps.x,  ps.y));
        sum += (1.0 / 32.0) * texture2D(s_tex, uv + vec2(-ps.x,  ps.y));
        sum += (1.0 / 32.0) * texture2D(s_tex, uv + vec2( ps.x, -ps.y));
        sum += (1.0 / 32.0) * texture2D(s_tex, uv + vec2(-ps.x, -ps.y));

        gl_FragColor = vec4(sum.rgb, sum.a);
    }
}
