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
// Luminance-moment history: r = E[L] (mu1), g = E[L^2] (mu2). Accumulated with the
// same running-mean weights as colour so the denoiser can derive per-pixel temporal
// variance = mu2 - mu1^2 (true SVGF).
SAMPLER2D(s_ssil_moments_history, 4);

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
    vec4 curr = texture2D(s_ssil_curr, uv);
    float luma_curr = Luminance(curr.rgb);

    BRANCH
    if(u_enable_temporal <= 0.5)
    {
        gl_FragData[0] = curr;
        gl_FragData[1] = vec4(luma_curr, luma_curr * luma_curr, 0.0, 0.0);
        return;
    }

    float surface_z = DecodeGBufferDepth(uv, s_depth).depth01;

    vec3 prev_sample = SSIL_ComputePreviousFrameSample(uv, surface_z);
    vec2 prev_uv = prev_sample.xy;
    float expected_prev_z = prev_sample.z;

    vec2 overshoot = max(max(-prev_uv, prev_uv - vec2_splat(1.0)), vec2_splat(0.0));
    float edge_fade = 1.0 - smoothstep(0.0, EDGE_FADE_MARGIN, max(overshoot.x, overshoot.y));

    vec4 hist = texture2D(s_ssil_history, clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0)));
    float W_hist = hist.a * u_max_accum_frames;
    vec3 C_hist = hist.rgb;

    // Each valid current frame contributes a unit sample to the running mean (standard
    // exponential accumulation); temporal averaging is what reduces the trace noise. We only
    // gate out sky (curr.a == 0). curr.a is otherwise the trace's single-frame confidence,
    // consumed as the SH-fallback weight on the temporal-disabled path -- it is NOT used as
    // an accumulation weight here (down-weighting noisy frames would just slow convergence).
    float W_curr = (curr.a > 0.0) ? 1.0 : 0.0;
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
    vec2 prev_uv_c = clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0));
    float stored_prev_z = DecodeGBufferDepth(prev_uv_c, s_prev_depth).depth01;
    float lin_expected = abs(SSIL_ComputeViewspacePosition(prev_uv_c, expected_prev_z).z);
    float lin_stored = abs(SSIL_ComputeViewspacePosition(prev_uv_c, stored_prev_z).z);
    float rel_depth_diff = abs(lin_expected - lin_stored) / max(lin_stored, 1e-3);
    bool depth_ok = rel_depth_diff < u_depth_threshold;

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
    float inv_W   = 1.0 / max(W_total, 1e-3);
    vec3  C_new   = (C_hist * W_hist + C_curr * W_curr) * inv_W;

    // Accumulate luminance moments with the SAME running-mean weights as colour.
    // mu1 = E[L], mu2 = E[L^2]; the denoiser derives temporal variance = mu2 - mu1^2.
    // Reprojection-invalid history (W_hist == 0 from disocclusion/edge) collapses these
    // to the current sample, giving variance 0 -> the denoiser falls back to its spatial
    // variance estimate for freshly disoccluded pixels (standard SVGF behaviour).
    vec2 m_hist = texture2D(s_ssil_moments_history, prev_uv_c).rg;
    float mu1_curr = luma_curr;
    float mu2_curr = luma_curr * luma_curr;
    float mu1_new = (m_hist.x * W_hist + mu1_curr * W_curr) * inv_W;
    float mu2_new = (m_hist.y * W_hist + mu2_curr * W_curr) * inv_W;

    gl_FragData[0] = vec4(C_new, W_new / max(u_max_accum_frames, 1.0));
    gl_FragData[1] = vec4(mu1_new, mu2_new, 0.0, 0.0);
}
