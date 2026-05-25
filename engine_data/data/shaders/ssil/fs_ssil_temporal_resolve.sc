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

    // No neighborhood color clip on the SSIL history: the trace is a sparse
    // Monte Carlo signal (a handful of cosine rays per pixel), so a 3x3 of
    // the raw current frame is dominated by zero-hit samples. Any
    // mu/sigma- or min/max-based clip built from that neighborhood
    // collapses toward the noise floor and persistently drags converged
    // history down -- and the dimming compounds through multi-bounce via
    // s_prev_ssil. The trace already clamps prev_indirect and per-ray
    // hit_color to 10 so the multi-bounce series cannot run away; the
    // depth check below + edge_fade handle the disocclusion / stale-history
    // cases the clip was nominally there for.

    float prev_surface_z = DecodeGBufferDepth(clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0)), s_prev_depth).depth01;
    bool depth_ok = abs(prev_surface_z - surface_z) < u_depth_threshold;

    W_hist *= float(depth_ok);
    W_hist *= edge_fade;

    float decay = mix(DECAY_MIN, DECAY_MAX, clamp(u_history_strength, 0.0, 1.0));
    W_hist *= decay;

    // Weighted-mean accumulation. The divisor must be the actual sum of
    // weights — NOT the saturation-clamped W_new — otherwise increasing
    // u_max_accum_frames artificially shrinks C_curr's contribution to the
    // numerator while leaving the denominator capped, dimming the SSIL signal
    // as the accumulation window grows. W_new (capped at u_max_accum_frames)
    // is only used as the round-tripped normalized weight in the alpha
    // channel, so saturation still bounds the effective history half-life.
    float W_total = W_hist + W_curr;
    float W_new   = min(W_total, u_max_accum_frames);
    vec3  C_new   = (C_hist * W_hist + C_curr * W_curr) / max(W_total, 1e-3);

    gl_FragColor = vec4(C_new, W_new / max(u_max_accum_frames, 1.0));
}
