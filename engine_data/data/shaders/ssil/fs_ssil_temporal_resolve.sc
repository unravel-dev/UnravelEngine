$input v_texcoord0

/*
 * Temporal accumulation pass for SSIL.
 *
 * Reprojects previous-frame SSIL using u_prev_view_proj,
 * validates via depth, and performs running-mean accumulation.
 */

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_ssil_curr, 0);
SAMPLER2D(s_ssil_history, 1);
SAMPLER2D(s_depth, 2);
SAMPLER2D(s_prev_depth, 3);

uniform vec4 u_temporal_params;
#define u_enable_temporal       u_temporal_params.x
#define u_history_strength      u_temporal_params.y
#define u_depth_threshold       u_temporal_params.z
#define u_max_accum_frames      u_temporal_params.w

uniform mat4 u_prev_view_proj;

#define DECAY_MIN 0.85
#define DECAY_MAX 0.99
#define EDGE_FADE_MARGIN 0.05

vec3 SSIL_ComputeViewspacePosition(vec2 uv, float z)
{
    return computeViewSpacePosition(uv, z);
}

vec2 SSIL_ComputePreviousFrameUV(vec2 uv, float z)
{
    vec3 vs_pos = SSIL_ComputeViewspacePosition(uv, z);
    vec4 ws_pos = mul(u_invView, vec4(vs_pos, 1.0));
    vec4 prev_clip4 = mul(u_prev_view_proj, vec4(ws_pos.xyz, 1.0));
    vec3 prev_clip = prev_clip4.xyz / prev_clip4.w;
    prev_clip = clipTransform(prev_clip);
    return prev_clip.xy * 0.5 + 0.5;
}

void main()
{
    vec2 uv = v_texcoord0;
    vec4 curr = texture2D(s_ssil_curr, uv);

    BRANCH
    if(u_enable_temporal <= 0.5)
    {
        gl_FragColor = curr;
        return;
    }

    float surface_z = DecodeGBufferDepth(uv, s_depth).depth01;

    vec2 prev_uv = SSIL_ComputePreviousFrameUV(uv, surface_z);

    vec2 overshoot = max(max(-prev_uv, prev_uv - vec2_splat(1.0)), vec2_splat(0.0));
    float edge_fade = 1.0 - smoothstep(0.0, EDGE_FADE_MARGIN, max(overshoot.x, overshoot.y));

    vec4 hist = texture2D(s_ssil_history, clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0)));
    float W_hist = hist.a * u_max_accum_frames;
    vec3 C_hist = hist.rgb;

    float W_curr = curr.a;
    vec3 C_curr = curr.rgb;

    // Neighborhood AABB clamp: prevent history from drifting beyond what
    // the current frame produces. This is the key mechanism that stops
    // multi-bounce energy from compounding over time.
    vec2 texel = 1.0 / vec2(textureSize(s_ssil_curr, 0));
    vec3 nb_min = C_curr;
    vec3 nb_max = C_curr;
    for(int dy = -1; dy <= 1; dy++)
    {
        for(int dx = -1; dx <= 1; dx++)
        {
            vec3 s = texture2DLod(s_ssil_curr, uv + vec2(float(dx), float(dy)) * texel, 0.0).rgb;
            nb_min = min(nb_min, s);
            nb_max = max(nb_max, s);
        }
    }
    C_hist = clamp(C_hist, nb_min, nb_max);

    float prev_surface_z = DecodeGBufferDepth(clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0)), s_prev_depth).depth01;
    bool depth_ok = abs(prev_surface_z - surface_z) < u_depth_threshold;

    W_hist *= float(depth_ok);
    W_hist *= edge_fade;

    float decay = mix(DECAY_MIN, DECAY_MAX, clamp(u_history_strength, 0.0, 1.0));
    W_hist *= decay;

    float W_new = clamp(W_hist + W_curr, 0.0, u_max_accum_frames);
    vec3 C_new = (C_hist * W_hist + C_curr * W_curr) / max(W_new, 1e-3);

    gl_FragColor = vec4(C_new, W_new / u_max_accum_frames);
}
