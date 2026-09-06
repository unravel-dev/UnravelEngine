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
// Full-res G-buffer normal. Used for the normal-validity disocclusion gate (compares the
// current pixel's normal against the normal at the reprojected UV in the CURRENT frame).
// We don't have a prev-frame normal buffer, so this catches the common case of the
// reprojection landing on a different surface (rotated geometry, animated foliage) but
// not the rarer case of the same surface being re-shaded with a different normal.
SAMPLER2D(s_normal, 5);
// Velocity buffer: RG = total uv-delta (uv_curr - uv_prev, unjittered prev), BA = the
// object-only component. Produced by the deferred velocity pass; authoritative when bound.
SAMPLER2D(s_velocity, 6);

uniform vec4 u_temporal_params;
#define u_enable_temporal       u_temporal_params.x
#define u_history_strength      u_temporal_params.y
#define u_depth_threshold       u_temporal_params.z
#define u_max_accum_frames      u_temporal_params.w

uniform vec4 u_temporal_params2;
/// Minimum dot(n_curr, n_at_prev_uv) to accept history. Typical 0.85 (~32 deg).
#define u_normal_dot_threshold  u_temporal_params2.x
/// 1 = reproject through the velocity buffer; 0 = legacy prev view-projection path.
#define u_velocity_available    u_temporal_params2.y

/// xy = full G-buffer size; zw = per-axis full / temporal-target scale.
uniform vec4 u_temporal_resolution;

uniform mat4 u_prev_view_proj;

// Replace NaN/Inf (which RGBA16F can carry across frames if anything ever wrote a denorm
// or overflowed) with zero, so the running mean does not stick permanently dark/bright.
// Do not call isnan()/isinf(): shaderc lowers those to equal/notEqual(float, float),
// which OpenGL GLSL rejects (those builtins are vector-only). Use x!=x for NaN and a
// finite threshold above RGBA16F range for Inf.
vec4 SSIL_SanitizeRgba(vec4 v)
{
    const float k_inf_threshold = 1e30;
    bvec4 bad = bvec4(v.x != v.x || abs(v.x) > k_inf_threshold,
                      v.y != v.y || abs(v.y) > k_inf_threshold,
                      v.z != v.z || abs(v.z) > k_inf_threshold,
                      v.w != v.w || abs(v.w) > k_inf_threshold);
    return mix(v, vec4_splat(0.0), vec4(float(bad.x), float(bad.y), float(bad.z), float(bad.w)));
}

// 5-tap Catmull-Rom resampler (Karis SIGGRAPH 2014 "High Quality Temporal Supersampling").
// Each tap is a hardware-bilinear sample whose UV is offset so the 5 taps reproduce the
// energy of a 4x4 bicubic-B-spline footprint (corner weights dropped; the remaining centre
// + cross taps account for >97% of bicubic weight). Sharper than hardware bilinear without
// the ringing of a full 9-tap bicubic, and crucially does NOT cross-blur during disocclusion
// because the taps still resolve via the same plane-distance and edge-fade gates downstream
// (the colour-blur it removes vs. plain bilinear is sub-pixel motion blur).
vec4 SSIL_SampleHistoryCatmullRom(sampler2D tex, vec2 uv, vec2 tex_size)
{
    vec2 sample_pos = uv * tex_size;
    vec2 tex_pos1 = floor(sample_pos - vec2_splat(0.5)) + vec2_splat(0.5);
    vec2 f = sample_pos - tex_pos1;

    // Catmull-Rom (a = -0.5) weights at fractional offsets {-1+f, f, 1-f, 2-f}.
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // Combine w1+w2 so the centre cross is a single bilinear tap at offset12.
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / max(w12, vec2_splat(1e-6));

    vec2 inv_tex_size = vec2_splat(1.0) / max(tex_size, vec2_splat(1.0));
    vec2 tex_pos0 = (tex_pos1 - vec2_splat(1.0)) * inv_tex_size;
    vec2 tex_pos3 = (tex_pos1 + vec2_splat(2.0)) * inv_tex_size;
    vec2 tex_pos12 = (tex_pos1 + offset12) * inv_tex_size;

    vec4 result  = SSIL_SanitizeRgba(texture2DLod(tex, vec2(tex_pos12.x, tex_pos0.y),  0.0)) * (w12.x * w0.y);
    result      += SSIL_SanitizeRgba(texture2DLod(tex, vec2(tex_pos0.x,  tex_pos12.y), 0.0)) * (w0.x  * w12.y);
    result      += SSIL_SanitizeRgba(texture2DLod(tex, vec2(tex_pos12.x, tex_pos12.y), 0.0)) * (w12.x * w12.y);
    result      += SSIL_SanitizeRgba(texture2DLod(tex, vec2(tex_pos3.x,  tex_pos12.y), 0.0)) * (w3.x  * w12.y);
    result      += SSIL_SanitizeRgba(texture2DLod(tex, vec2(tex_pos12.x, tex_pos3.y),  0.0)) * (w12.x * w3.y);

    // The 5-tap subset misses the four corner taps (~3% of the bicubic energy in the worst
    // case); normalise so brightness is preserved exactly regardless of where on the texel
    // grid the resampled point lands. Cheap (one rcp), and the alternative -- an unbalanced
    // sum -- would manifest as a checkerboard luminance drift across the screen.
    float weight_sum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return result / max(weight_sum, 1e-6);
}

//#if BGFX_SHADER_LANGUAGE_GLSL
//layout(location = 0) out vec4 ssil_color_out;
//layout(location = 1) out vec4 ssil_moments_out;
//#define SSIL_COLOR_OUT   ssil_color_out
//#define SSIL_MOMENTS_OUT ssil_moments_out
//#else
#define SSIL_COLOR_OUT   gl_FragData[0]
#define SSIL_MOMENTS_OUT gl_FragData[1]
//#endif

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

    // Camera-consistent pixels ALWAYS use this pass's own matrix reprojection (which also
    // supplies the expected previous depth for the disocclusion gate); the velocity
    // buffer's RG drives only OBJECT-motion pixels, gated by BA (the object-only split).
    // Trusting RG for camera pixels drags the whole image - the buffer's camera component
    // is not reliably this pass's own previous view-projection (measured; open engine
    // issue, see the velocity plan). Same gating as the TAA resolve. For movers the
    // velocity prev_uv follows the object while expected_prev_z does not; the depth and
    // normal gates then reject stale content there, the same net behavior as legacy.
    vec3 prev_sample = SSIL_ComputePreviousFrameSample(full_uv, surface_z);
    vec2 prev_uv = prev_sample.xy;
    float expected_prev_z = prev_sample.z;
    BRANCH
    if(u_velocity_available > 0.5)
    {
        vec4 vel4 = texture2DLod(s_velocity, full_uv, 0.0);
        vec2 vel_dim = vec2(textureSize(s_velocity, 0));
        float object_w = smoothstep(0.5, 1.5, length(vel4.zw * vel_dim));
        prev_uv = mix(prev_uv, full_uv - vel4.xy, object_w);
    }

    vec2 overshoot = max(max(-prev_uv, prev_uv - vec2_splat(1.0)), vec2_splat(0.0));
    float edge_fade = 1.0 - smoothstep(0.0, EDGE_FADE_MARGIN, max(overshoot.x, overshoot.y));

    vec2 prev_uv_c = clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0));
    vec2 history_dim = vec2(textureSize(s_ssil_history, 0));
    // Catmull-Rom for the colour history: sub-pixel motion under bilinear permanently
    // blurs converged SSIL because every reprojection averages 2x2 history texels even
    // when the surface didn't move at all (in the "no actual surface motion" case the
    // running mean keeps re-blurring its own already-blurred output). Catmull-Rom keeps
    // the same disocclusion behaviour (the depth/normal/edge gates below still apply)
    // but preserves sub-pixel detail across stable frames -- the converged image stays
    // crisp instead of slowly softening.
    vec4 hist = SSIL_SampleHistoryCatmullRom(s_ssil_history, prev_uv_c, history_dim);
    // Moments stay bilinear: Catmull-Rom samples a 4x4 footprint, which would WIDEN the
    // cross-surface bleed during disocclusion (variance from a neighbouring surface
    // collapsing the luma stop on this one). Bilinear is the right footprint for a
    // statistic that needs to stay local.
    vec4 m_hist_full = SSIL_SanitizeRgba(texture2D(s_ssil_moments_history, prev_uv_c));
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

    // Normal validity: we lack a previous-frame normal buffer, so compare the current
    // pixel's normal against the normal at the reprojected UV in the CURRENT frame. This
    // detects the common disocclusion case where reprojection lands on a different surface
    // (rotated mesh, animated foliage) but is silent on a same-pixel shading change.
    //
    // SOFT weight, not a hard reject. A hard reject (the v1 of this gate) drops history
    // entirely whenever the reprojection lands on a slightly different normal -- but on
    // curved surfaces during ANY camera motion the reprojected UV is, by construction,
    // some texels away from the current UV, and that "few texel offset along a curve"
    // gives a different normal value even though both pixels belong to the same surface.
    // A binary cutoff turns every curved silhouette and every sub-pixel reprojection
    // error into a full history drop, exposing the raw one-frame trace noise as crawling
    // edge speckle whenever the camera moves. The smoothstep keeps full history weight
    // when the surfaces match well, fades it smoothly through the moderate-mismatch band
    // (where the surface is plausibly the same but not quite), and only drops to zero
    // when the surfaces are clearly different (a real disocclusion).
    vec3 curr_normal = DecodeGBufferNormalMetalRoughness(full_uv, s_normal).world_normal;
    vec3 prev_uv_normal = DecodeGBufferNormalMetalRoughness(prev_uv_c, s_normal).world_normal;
    float n_dot = dot(curr_normal, prev_uv_normal);
    // Symmetric 0.2-wide soft band centred on the threshold:
    //   n_dot >= threshold + 0.1  -> full history weight 1.0
    //   n_dot <= threshold - 0.1  -> reject (weight 0.0)
    //   in between                 -> smooth Hermite fade
    // A 0.2 band is ~12-15 degrees of additional normal slack on either side of the
    // configured threshold, which absorbs sub-pixel reprojection error on curved
    // geometry without admitting clearly-different surfaces.
    float n_weight = smoothstep(u_normal_dot_threshold - 0.1, u_normal_dot_threshold + 0.1, n_dot);

    float validity = float(depth_ok) * edge_fade * n_weight;
    W_hist *= validity;
    hit_hist *= validity;

    float decay = mix(DECAY_MIN, DECAY_MAX, clamp(u_history_strength, 0.0, 1.0));
    W_hist *= decay;
    hit_hist *= decay;

    // Moments-driven firefly clamp on the CURRENT sample (SVGF recipe).
    //
    // The trace's importance-sampled hemisphere occasionally picks a direction that lands
    // on a tiny intensely-bright source (a specular highlight viewed from grazing, an
    // emissive sub-pixel, a sun-disk hit), giving a single-frame luminance spike that the
    // running mean then drags into the converged history for many frames. We use the
    // PRIOR-FRAME moments (which represent the surface's stable luminance distribution) to
    // bound the current sample at the UPPER tail only.
    //
    // ASYMMETRIC clamp -- upper tail only. The symmetric two-sided clamp(luma, mu-Ksig,
    // mu+Ksig) also lifts low outliers UP to mu-Ksig, which for a legitimately darker
    // current sample (e.g. luma = 0.1) against a bright mean (mu = 0.5, sig = 0.05)
    // multiplies RGB by 3x. That produces bright BLOTCHES in regions that ought to be
    // turning darker (a moving shadow, a surface rotating out of light), which is exactly
    // the wrong direction. Dark outliers converge naturally via the running mean and need
    // no clamp; only the bright spikes are firefly behaviour worth suppressing.
    //
    // Only applied once the history is reliable enough (W_hist > 1.0) -- on freshly
    // disoccluded pixels the moments are zero and clamping would just write the noisy
    // current value to itself. RGB is scaled by the clamped/unclamped luminance ratio so
    // colour balance is preserved (clamping the energy magnitude, not its hue).
    #define SSIL_FIREFLY_SIGMA 4.0
    BRANCH
    if(W_hist > 1.0 && luma_curr > 1e-4)
    {
        float mu = m_hist.x;
        float var_hist = max(0.0, m_hist.y - mu * mu);
        float sig = sqrt(var_hist);
        float hi = mu + SSIL_FIREFLY_SIGMA * sig;
        BRANCH
        if(luma_curr > hi)
        {
            float scale = hi / luma_curr;
            C_curr = C_curr * scale;
            luma_curr = hi;
        }
    }

    // Weighted-mean accumulation. The divisor must be the actual sum of
    // weights - NOT the saturation-clamped W_new - otherwise increasing
    // u_max_accum_frames artificially shrinks C_curr's contribution to the
    // numerator while leaving the denominator capped, dimming the SSIL signal
    // as the accumulation window grows. W_new (capped at u_max_accum_frames)
    // is round-tripped as temporal sample weight in moments alpha.
    float W_total = W_hist + W_curr;
    float W_new   = min(W_total, u_max_accum_frames);
    float inv_W   = 1.0 / max(W_total, 1e-3);
    vec3  C_new   = (C_hist * W_hist + C_curr * W_curr) * inv_W;
    float hit_new = min(hit_hist + curr.a, u_max_accum_frames);

    // Accumulate luminance moments with the SAME running-mean weights as colour, using
    // the (possibly firefly-clamped) current luminance so the moments themselves remain
    // an accurate estimator of the post-clamp signal -- otherwise the variance would
    // grow proportional to the spike and the next frame would WIDEN the luma stop in
    // exactly the regions we just suppressed (re-admitting the firefly).
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
