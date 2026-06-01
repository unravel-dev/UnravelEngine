$input v_texcoord0

/*
 * Temporal accumulation pass for SSIL.
 *
 * Reprojects previous-frame SSIL using u_prev_view_proj,
 * validates via depth, and performs running-mean accumulation.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"

SAMPLER2D(s_ssil_curr, 0);
SAMPLER2D(s_ssil_history, 1);
SAMPLER2D(s_depth, 2);
SAMPLER2D(s_prev_depth, 3);
// Luminance-moment history: r = E[L], g = E[L^2], b = accumulated screen-hit evidence,
// a = temporal sample weight.
SAMPLER2D(s_ssil_moments_history, 4);

uniform vec4 u_temporal_params;
#define u_enable_temporal       u_temporal_params.x
#define u_history_strength      u_temporal_params.y
#define u_depth_threshold       u_temporal_params.z
#define u_max_accum_frames      u_temporal_params.w

/// xy = full G-buffer size; zw = per-axis full / temporal-target scale.
uniform vec4 u_temporal_resolution;

uniform mat4 u_prev_view_proj;

#if BGFX_SHADER_LANGUAGE_GLSL >= 330
layout(location = 0) out vec4 ssil_color_out;
layout(location = 1) out vec4 ssil_moments_out;
#define SSIL_COLOR_OUT   ssil_color_out
#define SSIL_MOMENTS_OUT ssil_moments_out
#else
#define SSIL_COLOR_OUT   gl_FragData[0]
#define SSIL_MOMENTS_OUT gl_FragData[1]
#endif

#define DECAY_MIN 0.85
#define DECAY_MAX 0.99
#define EDGE_FADE_MARGIN 0.05
vec3 SSIL_ComputeViewspacePosition(vec2 uv, float z)
{
    return computeViewSpacePosition(uv, z);
}

/// Inverse of toClipSpaceDepth: map a clip-space depth back to the [0,1] device
/// depth stored in the depth buffer so the reprojected and sampled depths can be
/// compared in the same domain.
float SSIL_ClipDepthToDevice(float clip_z)
{
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV
    return clip_z;
#else
    return clip_z * 0.5 + 0.5;
#endif
}

/// Reproject the current world point into the previous frame. Returns the previous
/// UV in xy and the EXPECTED previous-frame device depth of that world point in z.
vec3 SSIL_ComputePreviousFrameSample(vec2 uv, float z)
{
    vec3 vs_pos = SSIL_ComputeViewspacePosition(uv, z);
    vec4 ws_pos = mul(u_invView, vec4(vs_pos, 1.0));
    vec4 prev_clip4 = mul(u_prev_view_proj, vec4(ws_pos.xyz, 1.0));
    vec3 prev_clip = prev_clip4.xyz / prev_clip4.w;
    prev_clip = clipTransform(prev_clip);
    vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;
    return vec3(prev_uv, SSIL_ClipDepthToDevice(prev_clip.z));
}

void main()
{
    vec2 uv = v_texcoord0;
    vec2 full_uv = HizScreenPassToFullResUV(uv,
                                            max(u_temporal_resolution.zw, vec2_splat(1.0)),
                                            u_temporal_resolution.xy);
    vec4 curr = texture2D(s_ssil_curr, uv);
    float surface_z = DecodeGBufferDepth(full_uv, s_depth).depth01;
    bool valid_surface =
#ifdef INVERTED_DEPTH_RANGE
        surface_z != 0.0;
#else
        surface_z != 1.0;
#endif

    float luma_curr = Luminance(curr.rgb);

    BRANCH
    if(!valid_surface)
    {
        SSIL_COLOR_OUT = curr;
        SSIL_MOMENTS_OUT = vec4(luma_curr, luma_curr * luma_curr, curr.a, 0.0);
        return;
    }

    BRANCH
    if(u_enable_temporal <= 0.5)
    {
        SSIL_COLOR_OUT = curr;
        SSIL_MOMENTS_OUT = vec4(luma_curr, luma_curr * luma_curr, curr.a,
                                1.0 / max(u_max_accum_frames, 1.0));
        return;
    }

    vec3 prev_sample = SSIL_ComputePreviousFrameSample(full_uv, surface_z);
    vec2 prev_uv = prev_sample.xy;
    float expected_prev_z = prev_sample.z;

    vec2 overshoot = max(max(-prev_uv, prev_uv - vec2_splat(1.0)), vec2_splat(0.0));
    float edge_fade = 1.0 - smoothstep(0.0, EDGE_FADE_MARGIN, max(overshoot.x, overshoot.y));

    vec2 prev_uv_c = clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0));
    vec4 hist = texture2D(s_ssil_history, prev_uv_c);
    vec4 m_hist_full = texture2D(s_ssil_moments_history, prev_uv_c);
    vec2 m_hist = m_hist_full.rg;
    float hit_hist = m_hist_full.b * u_max_accum_frames;
    float W_hist = m_hist_full.a * u_max_accum_frames;
    vec3 C_hist = hist.rgb;

    // Each valid surface frame contributes one radiance sample. Trace alpha is not sample
    // validity here: env-only rays still carry valid SH radiance.
    float W_curr = 1.0;
    vec3 C_curr = curr.rgb;

    // No neighborhood colour clip on the history. The trace is a noisy few-ray Monte Carlo
    // signal, so a clamp box built from the current 3x3 is itself noisy: clamping the
    // converged history into that per-frame box re-injects the very noise temporal
    // accumulation exists to remove, producing crawling/boiling speckle. (TAA can clamp
    // because its current frame is the clean rendered image; ours is not.) Disocclusion /
    // stale-history rejection is handled by the relative-depth test + edge_fade below.

    // Disocclusion test: compare the depth the current world point WOULD have had
    // last frame (expected_prev_z) against the depth actually stored at prev_uv.
    // The previous code compared the current device depth to the stored previous
    // device depth directly -- invalid when the camera moves (device depth is
    // camera-relative and non-linear), so it was both too strict near the camera
    // and too loose far away, and it let foreground/background ghost across
    // silhouettes. We linearise both depths to view space (same projection frame
    // to frame) and reject on a RELATIVE difference, which is scale-invariant and
    // catches occlusion changes the absolute device-depth test missed.
    float stored_prev_z = DecodeGBufferDepth(prev_uv_c, s_prev_depth).depth01;
    float lin_expected = abs(SSIL_ComputeViewspacePosition(prev_uv_c, expected_prev_z).z);
    float lin_stored = abs(SSIL_ComputeViewspacePosition(prev_uv_c, stored_prev_z).z);
    float rel_depth_diff = abs(lin_expected - lin_stored) / max(lin_stored, 1e-3);
    bool depth_ok = rel_depth_diff < u_depth_threshold;

    W_hist *= float(depth_ok);
    hit_hist *= float(depth_ok);
    W_hist *= edge_fade;
    hit_hist *= edge_fade;

    float decay = mix(DECAY_MIN, DECAY_MAX, clamp(u_history_strength, 0.0, 1.0));
    W_hist *= decay;
    hit_hist *= decay;

    // Weighted-mean accumulation. The divisor must be the actual sum of
    // weights — NOT the saturation-clamped W_new — otherwise increasing
    // u_max_accum_frames artificially shrinks C_curr's contribution to the
    // numerator while leaving the denominator capped, dimming the SSIL signal
    // as the accumulation window grows. W_new (capped at u_max_accum_frames)
    // is round-tripped as temporal sample weight in moments alpha.
    float W_total = W_hist + W_curr;
    float W_new   = min(W_total, u_max_accum_frames);
    float inv_W   = 1.0 / max(W_total, 1e-3);
    vec3  C_new   = (C_hist * W_hist + C_curr * W_curr) * inv_W;
    float hit_new = min(hit_hist + curr.a, u_max_accum_frames);

    // Accumulate luminance moments with the SAME running-mean weights as colour.
    // mu1 = E[L], mu2 = E[L^2]; the denoiser derives temporal variance = mu2 - mu1^2.
    // Reprojection-invalid history (W_hist == 0 from disocclusion/edge) collapses these
    // to the current sample, giving variance 0 -> the denoiser falls back to its spatial
    // variance estimate for freshly disoccluded pixels (standard SVGF behaviour).
    float mu1_curr = luma_curr;
    float mu2_curr = luma_curr * luma_curr;
    float mu1_new = (m_hist.x * W_hist + mu1_curr * W_curr) * inv_W;
    float mu2_new = (m_hist.y * W_hist + mu2_curr * W_curr) * inv_W;
    float hit_history = hit_new / max(u_max_accum_frames, 1.0);
    float blend_weight = saturate(hit_new);

    SSIL_COLOR_OUT = vec4(C_new, blend_weight);
    SSIL_MOMENTS_OUT = vec4(mu1_new, mu2_new, hit_history, W_new / max(u_max_accum_frames, 1.0));
}
