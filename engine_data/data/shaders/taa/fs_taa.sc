$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_curr, 0);
SAMPLER2D(s_history, 1);
SAMPLER2D(s_depth, 2);

uniform mat4 u_prev_view_proj;

uniform vec4 u_taa_params;
#define u_history_blend        u_taa_params.x
#define u_sharpen              u_taa_params.y
#define u_depth_reject_scale   u_taa_params.z
#define u_variance_clip_scale  u_taa_params.w

vec2 TAA_PreviousUV(vec2 uv, float depth01)
{
    vec3 vs_pos = computeViewSpacePosition(uv, depth01);
    vec4 ws_pos = mul(u_invView, vec4(vs_pos, 1.0));
    vec4 prev_clip4 = mul(u_prev_view_proj, vec4(ws_pos.xyz, 1.0));
    vec3 prev_clip = prev_clip4.xyz / prev_clip4.w;
    prev_clip = clipTransform(prev_clip);
    return prev_clip.xy * 0.5 + 0.5;
}

float TAA_LinearViewDepthFrom01(float depth01)
{
    return screenSpaceToViewSpaceDepth(depth01);
}

void main()
{
    vec2 uv = v_texcoord0;
    ivec2 dim = textureSize(s_curr, 0);
    vec2 texel = vec2(1.0, 1.0) / vec2(float(dim.x), float(dim.y));

    ivec2 ddim = textureSize(s_depth, 0);
    vec2 ddimf = vec2(float(ddim.x), float(ddim.y));
    ivec2 tcent = clamp(ivec2(uv * ddimf - vec2_splat(0.499)), ivec2(0, 0), ddim - ivec2(1, 1));
    float depth01 = texelFetch(s_depth, tcent, 0).x;

    float depth_edge = max(abs(dFdx(depth01)), abs(dFdy(depth01)));

    vec2 prev_uv = TAA_PreviousUV(uv, depth01);

    vec4 curr = texture2D(s_curr, uv);

    vec2 overshoot = max(max(-prev_uv, prev_uv - vec2_splat(1.0)), vec2_splat(0.0));
    float edge_fade = 1.0 - smoothstep(0.0, 0.06, max(overshoot.x, overshoot.y));

    // Screen borders: 3x3 taps and reprojection are unreliable; fade history (fixes edge lines / bleed).
    float screen_inset = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    float screen_border_w = smoothstep(0.0, 4.0 * max(texel.x, texel.y), screen_inset);
    // Previous-frame UV: penalize near or outside [0,1] so bilinear does not drag border/clear colors.
    float prev_inset = min(min(prev_uv.x, prev_uv.y), min(1.0 - prev_uv.x, 1.0 - prev_uv.y));
    float history_border_w = smoothstep(0.0, 3.0 * max(texel.x, texel.y), prev_inset);

    ivec2 tprev = clamp(ivec2(prev_uv * ddimf - vec2_splat(0.499)), ivec2(0, 0), ddim - ivec2(1, 1));
    float depth01_prev = texelFetch(s_depth, tprev, 0).x;

    float z_curr = TAA_LinearViewDepthFrom01(depth01);
    float z_prev = TAA_LinearViewDepthFrom01(depth01_prev);
    float depth_diff = abs(z_curr - z_prev) / max(1.0, abs(z_curr));
    float depth_ok = 1.0 - smoothstep(0.001, 0.035 * max(0.25, u_depth_reject_scale), depth_diff);

    float silhouette = 1.0 - smoothstep(0.0, 0.02, depth_edge);
    float edge_blend = mix(0.35, 1.0, silhouette);

    float k = max(0.75, u_variance_clip_scale);
    vec3 m1 = vec3_splat(0.0);
    vec3 m2 = vec3_splat(0.0);
    vec3 nb_min = vec3_splat(1e10);
    vec3 nb_max = vec3_splat(-1e10);
    for(int y = -1; y <= 1; ++y)
    {
        for(int x = -1; x <= 1; ++x)
        {
            vec2 suv = clamp(uv + vec2(float(x), float(y)) * texel, vec2_splat(0.0), vec2_splat(1.0));
            vec3 c = texture2D(s_curr, suv).rgb;
            m1 += c;
            m2 += c * c;
            nb_min = min(nb_min, c);
            nb_max = max(nb_max, c);
        }
    }
    const float inv9 = 1.0 / 9.0;
    vec3 mu = m1 * inv9;
    vec3 sigma = sqrt(max(m2 * inv9 - mu * mu, vec3_splat(1e-8)));
    vec3 cmin = mu - sigma * k;
    vec3 cmax = mu + sigma * k;

    vec2 half_texel = texel * 0.5;
    vec2 hist_uv = clamp(prev_uv, half_texel, vec2(1.0, 1.0) - half_texel);
    vec3 hist_rgb = texture2D(s_history, hist_uv).rgb;
    vec3 clamped_hist = clamp(hist_rgb, cmin, cmax);

    float blend = u_history_blend * edge_fade * depth_ok * edge_blend * screen_border_w * history_border_w;
    vec3 resolved = mix(curr.rgb, clamped_hist, blend);

    if(u_sharpen > 0.001)
    {
        const vec3 luma_dir = vec3(0.212656, 0.715158, 0.072186);
        vec3 nb_rng = max(nb_max - nb_min, vec3_splat(1e-6));
        float contrast = max(nb_rng.x, max(nb_rng.y, nb_rng.z));
        // Strong attenuation on HDR edges (contrast^2 term); stops black/white ringing from unsharp.
        float sharpen_adapt = rcp(1.0 + contrast * 0.7 + contrast * contrast * 0.12);
        float sharpen_w = u_sharpen * mix(0.12, 1.0, silhouette) * sharpen_adapt;

        vec3 pre_sharp = resolved;
        float y_curr = dot(curr.rgb, luma_dir);
        float y_mu = dot(mu, luma_dir);
        float y_res = dot(pre_sharp, luma_dir);
        float dy = (y_curr - y_mu) * sharpen_w;
        float y_lo = min(min(y_res, y_curr), y_mu);
        float y_hi = max(max(y_res, y_curr), y_mu);
        float neg_cap = max(y_lo, 1e-5) * 0.16;
        float pos_cap = max(y_hi, 1e-5) * 0.16;
        dy = clamp(dy, -neg_cap, pos_cap);
        float y_new = max(y_res + dy, 0.0);
        float y_scale = (y_res > 1e-5) ? (y_new / y_res) : 1.0;
        resolved = max(pre_sharp * y_scale, vec3_splat(0.0));

        vec3 rgb_delta = resolved - pre_sharp;
        rgb_delta = clamp(rgb_delta, -nb_rng * 0.09, nb_rng * 0.09);
        resolved = pre_sharp + rgb_delta;

        vec3 lo = min(nb_min, pre_sharp);
        vec3 hi = max(nb_max, pre_sharp);
        vec3 slack = max(nb_rng * 0.022, vec3_splat(1e-5));
        resolved = clamp(resolved, lo - slack, hi + slack);
        resolved = max(resolved, vec3_splat(0.0));
    }

    gl_FragColor = vec4(resolved, curr.a);
}
