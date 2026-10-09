$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"
#include "../pre_exposure.sh"
#include "../velocity/velocity_encoding.sh"

SAMPLER2D(s_curr, 0);
SAMPLER2D(s_history, 1);
SAMPLER2D(s_depth, 2);
SAMPLER2D(s_prev_depth, 3);
SAMPLER2D(s_velocity, 4);

uniform mat4 u_prev_view_proj;

uniform vec4 u_taa_params;
#define u_history_blend        u_taa_params.x
#define u_sharpen              u_taa_params.y
#define u_depth_reject_scale   u_taa_params.z
#define u_variance_clip_scale  u_taa_params.w

uniform vec4 u_taa_params2;
// 1 = reproject through the velocity buffer (uv - velocity); 0 = legacy depth reprojection.
#define u_use_velocity         u_taa_params2.x
// This frame's sub-pixel jitter as the NDC offset the projection carries (camera aa_data.zw): scene content moved by it.
#define u_jitter_ndc           u_taa_params2.yz

// Reprojection motion, in pixels, below which a pixel counts as STILL for the depth-edge damping. The output lives on
// the unjittered pixel grid, so a parked camera reprojects by exactly 0; this absorbs slow drift.
#define TAA_STILL_REPROJECTION_PX 0.5
// History weight a STILL pixel uses at least (the settings value keeps ruling in motion).
#define TAA_STILL_HISTORY_BLEND 0.950
// Variance clip box multiple for a STILL pixel: the jittered 3x3 box moves every frame at rest and a
// settings-width clip snapped the converging history back to it.
#define TAA_STILL_CLIP_SCALE 2.000
// Offset of the HDR weight 1 / (offset + luma) of the current-frame reconstruction taps (UE TemporalAA.usf).
#define TAA_FILTER_HDR_WEIGHT_OFFSET 4.0
// Reprojection motion, in pixels, over which a pixel moves from the rest weights to the motion weights below. Every
// frame of motion resamples the history once more and each resample softens it, so a long history blurs moving
// detail (UE TSR keeps a double-resolution history for this; UE TAA raises the current weight with velocity).
#define TAA_MOTION_START_PX 2.0
#define TAA_MOTION_FULL_PX 10.0
// History weight a moving pixel uses at most: fewer accumulated resamples keep its detail.
#define TAA_MOTION_HISTORY_BLEND 0.70
// Extra display sharpen at full motion, as a multiple of the settings value: it restores what the resamples soften.
#define TAA_MOTION_SHARPEN_BOOST 1.0
// Largest per-axis jitter change between two frames, in pixels: at jitter_amplitude 1 every sequence stays within
// [-0.5, 0.5].
#define TAA_JITTER_STEP_MAX_PX 1.0
// Object-only motion, in pixels, from which a velocity texel starts and fully counts as a moving object.
#define TAA_OBJECT_MOTION_START_PX 0.5
#define TAA_OBJECT_MOTION_FULL_PX 1.5
// The mover memory (history alpha): 1 where a moving object covers or borders the pixel, decaying by this factor per
// frame after it left. A pixel leaves the still path while its memory is above TAA_MOVER_MEMORY_STILL_END and is fully
// back below TAA_MOVER_MEMORY_STILL_START: about 6 and 13 frames after the mover left.
#define TAA_MOVER_MEMORY_DECAY 0.8
#define TAA_MOVER_MEMORY_STILL_START 0.05
#define TAA_MOVER_MEMORY_STILL_END 0.25


// YCoCg: chroma bounds are much tighter than RGB's, so a variance box built there
// rejects colored ghosting an RGB box lets through.
vec3 TAA_RGBToYCoCg(vec3 c)
{
    return vec3( 0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
                 0.5  * c.r             - 0.5  * c.b,
                -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 TAA_YCoCgToRGB(vec3 c)
{
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Ray-clip toward the box center instead of a per-axis clamp: a clamp projects
// history onto box corners and skews hue; the clip preserves the color direction.
vec3 TAA_ClipToAABB(vec3 value, vec3 center, vec3 extents)
{
    vec3 dir = value - center;
    vec3 t = abs(extents / max(abs(dir), vec3_splat(1e-6)));
    float t_min = min(1.0, min(t.x, min(t.y, t.z)));
    return center + dir * t_min;
}

// Reconstruction weight of a tap at squared distance dist2 (pixels) from the unjittered output centre: a
// Blackman-Harris-like polynomial of radius 1 px, 1 - 1.9 x^2 + 0.905 x^4 (UE TemporalAA.usf ComputeSampleWeigth).
float TAA_FilterWeight(float dist2)
{
    float x2 = saturate(dist2);
    return (0.905 * x2 - 1.9) * x2 + 1.0;
}

// 5-fetch Catmull-Rom (Jimenez): bilinear history resampling under sub-pixel jitter
// low-passes the accumulation every frame, so the history converges blurry no matter
// how good the rest of the filter is. Bicubic reconstruction keeps it sharp.
vec3 TAA_SampleHistoryCatmullRom(vec2 uv, vec2 texel_size)
{
    vec2 sample_pos = uv / texel_size;
    vec2 tex_pos1 = floor(sample_pos - 0.5) + 0.5;
    vec2 f = sample_pos - tex_pos1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;
    vec2 tex_pos0 = (tex_pos1 - vec2_splat(1.0)) * texel_size;
    vec2 tex_pos3 = (tex_pos1 + vec2_splat(2.0)) * texel_size;
    vec2 tex_pos12 = (tex_pos1 + offset12) * texel_size;
    vec3 result =
        texture2DLod(s_history, vec2(tex_pos12.x, tex_pos0.y), 0.0).rgb * (w12.x * w0.y) +
        texture2DLod(s_history, vec2(tex_pos0.x, tex_pos12.y), 0.0).rgb * (w0.x * w12.y) +
        texture2DLod(s_history, vec2(tex_pos12.x, tex_pos12.y), 0.0).rgb * (w12.x * w12.y) +
        texture2DLod(s_history, vec2(tex_pos3.x, tex_pos12.y), 0.0).rgb * (w3.x * w12.y) +
        texture2DLod(s_history, vec2(tex_pos12.x, tex_pos3.y), 0.0).rgb * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    // Renormalize for the dropped corner taps; negative lobes can undershoot, clamp to valid HDR.
    return max(result / weight, vec3_splat(0.0));
}

// Reprojects the current pixel into the previous frame. Returns the history UV in
// xy and the EXPECTED previous depth01 of this surface in z, so disocclusion can
// compare it against what the previous depth buffer actually stored there. The pass
// binds the UNJITTERED projection (u_invProj), so this is the unjittered pixel centre's
// reprojection: exactly uv for a parked camera, whatever the frame's jitter.
vec3 TAA_PreviousScreenPos(vec2 uv, float depth01)
{
    vec3 vs_pos = computeViewSpacePosition(uv, depth01);
    vec4 ws_pos = mul(u_invView, vec4(vs_pos, 1.0));
    vec4 prev_clip4 = mul(u_prev_view_proj, vec4(ws_pos.xyz, 1.0));
    vec3 prev_clip = prev_clip4.xyz / prev_clip4.w;
    prev_clip = clipTransform(prev_clip);
    return vec3(prev_clip.xy * 0.5 + 0.5, toDepthTextureZ(prev_clip.z));
}

float TAA_LinearViewDepthFrom01(float depth01)
{
    return screenSpaceToViewSpaceDepth(depth01);
}

// Absolute linear view depth of the current depth texel at t (clamped to the texture).
float TAA_LinearViewDepthAt(ivec2 t, ivec2 ddim)
{
    ivec2 clamped = clamp(t, ivec2(0, 0), ddim - ivec2(1, 1));
    return abs(TAA_LinearViewDepthFrom01(texelFetch(s_depth, clamped, 0).x));
}


void main()
{
    vec2 uv = v_texcoord0;
    ivec2 dim = textureSize(s_curr, 0);
    vec2 texel = vec2(1.0, 1.0) / vec2(float(dim.x), float(dim.y));
    // The jitter moved scene content by jitter_uv: the input texel at q holds the unjittered scene at q - jitter.
    // The output is defined on the unjittered grid, so neither the history fetch nor the current-frame
    // reconstruction moves with the jitter.
    vec2 jitter_uv = vec2(u_jitter_ndc.x, toClipSpaceY(u_jitter_ndc.y)) * 0.5;
    vec2 jitter_px = jitter_uv * vec2(float(dim.x), float(dim.y));

    ivec2 ddim = textureSize(s_depth, 0);
    vec2 ddimf = vec2(float(ddim.x), float(ddim.y));
    ivec2 tcent = clamp(ivec2(uv * ddimf - vec2_splat(0.499)), ivec2(0, 0), ddim - ivec2(1, 1));
    float depth01 = texelFetch(s_depth, tcent, 0).x;

    float depth_edge = max(abs(dFdx(depth01)), abs(dFdy(depth01)));

    // Camera-only reprojection: the fallback history UV, the expected previous depth for
    // the disocclusion test, and - in velocity mode - the reference that separates object
    // motion from camera motion.
    vec3 prev_pos = TAA_PreviousScreenPos(uv, depth01);
    vec2 prev_uv = prev_pos.xy;

    // How much of this pixel's motion the camera CANNOT explain (in pixels). Stays 0 on
    // the legacy path and for camera-consistent pixels in velocity mode.
    float object_motion_w = 0.0;
    // Whether a moving object covers this pixel or one of its 3x3 neighbours this frame.
    float mover_nearby = 0.0;
    if(u_use_velocity > 0.5)
    {
        // Closest-depth dilation source: the nearest (smallest depth01) texel in the 3x3
        // neighborhood, so a MOVER's anti-aliased silhouette reprojects with the object.
        // Deliberately inlined: an out-parameter helper for this loop miscompiled on the
        // HLSL path (garbage src_t/src_depth), which reprojected the whole frame by one
        // border texel's velocity - a full-screen history drag under camera motion.
        ivec2 src_t = tcent;
        float src_depth = depth01;
        float nearby_object_px = 0.0;
        for(int y = -1; y <= 1; ++y)
        {
            for(int x = -1; x <= 1; ++x)
            {
                ivec2 t = clamp(tcent + ivec2(x, y), ivec2(0, 0), ddim - ivec2(1, 1));
                float d = texelFetch(s_depth, t, 0).x;
                nearby_object_px = max(nearby_object_px, VelocityObjectMotionPixels(texelFetch(s_velocity, t, 0)));
                if(d < src_depth)
                {
                    src_depth = d;
                    src_t = t;
                }
            }
        }
        // Dilation affinity: adopt the closest neighbor's velocity only when this pixel
        // plausibly belongs to the same surface (linear depths within 5%). Without this,
        // every pixel within 1px of a MOVING surface - its whole background rim, and every
        // hole of a dithered (screen-door) surface - adopts the mover's velocity with the
        // depth test disabled below: a bright halo band around slow movers and wrong
        // reprojection across dithered interiors. Background-depth pixels keep their own
        // velocity texel (camera motion), full depth test and edge damping included; the
        // mover's own texels (interior + the AA texels whose depth the mover wrote) still
        // reproject with the mover.
        float z_center_aff = abs(TAA_LinearViewDepthFrom01(depth01));
        float z_src_aff = abs(TAA_LinearViewDepthFrom01(src_depth));
        if(abs(z_center_aff - z_src_aff) > 0.05 * max(z_center_aff, z_src_aff))
        {
            src_t = tcent;
            src_depth = depth01;
        }
        vec4 vel4 = texelFetch(s_velocity, src_t, 0);
        vec2 vel = vel4.xy;
        // B of the velocity buffer is the OBJECT-ONLY motion, split inside the
        // velocity pass itself with one consistent matrix set (fs_velocity.sc). It is
        // exactly zero for every camera-derived pixel, so the static world takes the
        // legacy path below unconditionally - no matrices are re-derived here, and no
        // cross-pass matrix consistency is assumed (measured in this engine: the same
        // camera getter did NOT return the same previous view-projection to the velocity
        // pass and to this pass within one frame; classification through a recomputed
        // camera velocity therefore misfired screen-wide).
        float object_px = VelocityObjectMotionPixels(vel4);
        object_motion_w = smoothstep(TAA_OBJECT_MOTION_START_PX, TAA_OBJECT_MOTION_FULL_PX, object_px);
        mover_nearby = smoothstep(TAA_OBJECT_MOTION_START_PX, TAA_OBJECT_MOTION_FULL_PX, nearby_object_px);
        // Genuine object motion reprojects through the dilated velocity (silhouette
        // band included). Camera-only pixels keep the center-depth camera reprojection:
        // exact for them (dither interiors and static silhouettes included), and it
        // keeps the expected-previous-depth disocclusion test meaningful.
        // The velocity is the jittered current position minus the unjittered previous one; removing this
        // frame's jitter makes it the unjittered centre's motion.
        prev_uv = mix(prev_pos.xy, uv - (vel - jitter_uv), object_motion_w);
    }

    vec4 curr = texture2D(s_curr, uv);

    vec2 overshoot = max(max(-prev_uv, prev_uv - vec2_splat(1.0)), vec2_splat(0.0));
    float edge_fade = 1.0 - smoothstep(0.0, 0.06, max(overshoot.x, overshoot.y));

    // Screen borders: 3x3 taps and reprojection are unreliable; fade history (fixes edge lines / bleed).
    float screen_inset = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    float screen_border_w = smoothstep(0.0, 4.0 * max(texel.x, texel.y), screen_inset);
    // Previous-frame UV: penalize near or outside [0,1] so bilinear does not drag border/clear colors.
    float prev_inset = min(min(prev_uv.x, prev_uv.y), min(1.0 - prev_uv.x, 1.0 - prev_uv.y));
    float history_border_w = smoothstep(0.0, 3.0 * max(texel.x, texel.y), prev_inset);

    // Disocclusion test: the reprojection also yields how deep THIS surface was in
    // the previous frame (prev_pos.z); if the previous depth buffer stored something
    // significantly different at that position, the history pixel belongs to another
    // surface (an occluder or a since-revealed background) and must not be blended.
    // Comparing against the PREVIOUS depth buffer is essential: the current depth at
    // the reprojected position cannot detect disocclusion and false-fires on grazing
    // surfaces under camera motion.
    ivec2 tprev = clamp(ivec2(prev_uv * ddimf - vec2_splat(0.499)), ivec2(0, 0), ddim - ivec2(1, 1));
    float z_expected = abs(TAA_LinearViewDepthFrom01(prev_pos.z));
    // Both depth buffers hold jittered samples: across the sub-pixel jitter step a silhouette texel flips between
    // the two surfaces and a sloped surface changes depth, so a single-texel comparison failed at rest and let the
    // jittered current frame through on every edge and grazing floor (the image shook with the jitter). The
    // surface counts as present when any texel of the previous 3x3 holds it; a true disocclusion has none.
    float z_stored_diff = 1e30;
    for(int py = -1; py <= 1; ++py)
    {
        for(int px = -1; px <= 1; ++px)
        {
            ivec2 tn = clamp(tprev + ivec2(px, py), ivec2(0, 0), ddim - ivec2(1, 1));
            float z_stored = abs(TAA_LinearViewDepthFrom01(texelFetch(s_prev_depth, tn, 0).x));
            z_stored_diff = min(z_stored_diff, abs(z_expected - z_stored));
        }
    }
    // The remaining sloped-surface difference is the jitter step's worst case (TAA_JITTER_STEP_MAX_PX) times the
    // surface slope, the smaller one-sided difference per axis so a silhouette does not widen it.
    float z_centre = TAA_LinearViewDepthAt(tcent, ddim);
    float slope_x = min(abs(TAA_LinearViewDepthAt(tcent - ivec2(1, 0), ddim) - z_centre),
                        abs(TAA_LinearViewDepthAt(tcent + ivec2(1, 0), ddim) - z_centre));
    float slope_y = min(abs(TAA_LinearViewDepthAt(tcent - ivec2(0, 1), ddim) - z_centre),
                        abs(TAA_LinearViewDepthAt(tcent + ivec2(0, 1), ddim) - z_centre));
    float jitter_depth_slack = (slope_x + slope_y) * TAA_JITTER_STEP_MAX_PX;
    float depth_diff = max(z_stored_diff - jitter_depth_slack, 0.0) / max(1.0, z_expected);
    float depth_ok = 1.0 - smoothstep(0.001, 0.035 * max(0.25, u_depth_reject_scale), depth_diff);

    // The expected previous depth comes from the camera-only reprojection, so the test is
    // only meaningful where the pixel's true motion agrees with it. For object-motion
    // pixels (velocity mode) the surface's previous depth is unknowable here - neutralize
    // the test and let the variance clip own history rejection for them.
    depth_ok = mix(depth_ok, 1.0, object_motion_w);

    // Depth-edge history damping. The floor masks TWO things: mover ghosting (which the
    // velocity path genuinely fixes) and bilinear history bleed across depth edges under
    // CAMERA motion (which velocity does nothing for - those pixels reproject exactly as
    // before). So it is retired only for object-motion pixels; camera-consistent edges -
    // static silhouettes and dithered-transparency interiors, which are depth edges at
    // every pixel - keep the legacy damping. Retiring it globally in velocity mode put a
    // bright halo on static edges and shimmer inside dither under camera motion.
    float silhouette = 1.0 - smoothstep(0.0, 0.02, depth_edge);
    // Still silhouettes keep the full history (TAA_STILL_REPROJECTION_PX): the damping is for bleed
    // under camera motion, and at rest it only let the jitter oscillate on every depth edge.
    float reprojection_px = length((uv - prev_uv) * ddimf);
    float still = 1.0 - smoothstep(TAA_STILL_REPROJECTION_PX, 2.0 * TAA_STILL_REPROJECTION_PX, reprojection_px);
    vec2 half_texel = texel * 0.5;
    vec2 hist_uv = clamp(prev_uv, half_texel, vec2(1.0, 1.0) - half_texel);
    // A pixel a moving object covered or bordered is not still while the object's trace fades from it: the still
    // weight and clip held its anti-aliased edge and the lighting it left behind as trails behind every mover.
    float mover_memory = max(mover_nearby, saturate(texture2DLod(s_history, hist_uv, 0.0).a) * TAA_MOVER_MEMORY_DECAY);
    still *= 1.0 - smoothstep(TAA_MOVER_MEMORY_STILL_START, TAA_MOVER_MEMORY_STILL_END, mover_memory);
    float edge_blend = mix(mix(0.6, 1.0, silhouette), 1.0, max(object_motion_w, still));

    float k = max(0.75, u_variance_clip_scale) * mix(1.0, TAA_STILL_CLIP_SCALE, still);
    // RGB mean/min/max feed the sharpen path below; the variance box for history
    // rejection is built in YCoCg where chroma bounds are tight.
    vec3 m1_rgb = vec3_splat(0.0);
    vec3 m1 = vec3_splat(0.0);
    vec3 m2 = vec3_splat(0.0);
    vec3 nb_min = vec3_splat(1e10);
    vec3 nb_max = vec3_splat(-1e10);
    // The current frame reconstructed at the unjittered output centre: each tap weighted by its distance
    // (offset - jitter) and by its HDR weight, so a firefly cannot dominate the reconstruction.
    vec3 filtered_sum = vec3_splat(0.0);
    float filtered_weight = 0.0;
    for(int y = -1; y <= 1; ++y)
    {
        for(int x = -1; x <= 1; ++x)
        {
            vec2 offset = vec2(float(x), float(y));
            vec2 suv = clamp(uv + offset * texel, vec2_splat(0.0), vec2_splat(1.0));
            vec3 c = texture2D(s_curr, suv).rgb;
            vec3 yc = TAA_RGBToYCoCg(c);
            m1_rgb += c;
            m1 += yc;
            m2 += yc * yc;
            nb_min = min(nb_min, c);
            nb_max = max(nb_max, c);
            vec2 to_centre = offset - jitter_px;
            float tap_weight = TAA_FilterWeight(dot(to_centre, to_centre)) / (TAA_FILTER_HDR_WEIGHT_OFFSET + max(yc.x, 0.0));
            filtered_sum += c * tap_weight;
            filtered_weight += tap_weight;
        }
    }
    vec3 filtered = filtered_sum / max(filtered_weight, 1e-6);
    // How well this frame's jittered sample covers the output centre: a sample far from it is trusted less
    // (UE FilteredTemporalWeight; 1 at the centre, ~0.3 at a pixel corner).
    float sample_alignment = TAA_FilterWeight(dot(jitter_px, jitter_px));
    const float inv9 = 1.0 / 9.0;
    vec3 mu = m1_rgb * inv9;
    vec3 mu_yc = m1 * inv9;
    vec3 sigma_yc = sqrt(max(m2 * inv9 - mu_yc * mu_yc, vec3_splat(1e-8)));

    // The history was written under last frame's pre-exposure (UE HistoryPreExposureCorrection).
    vec3 hist_rgb = TAA_SampleHistoryCatmullRom(hist_uv, texel) * u_history_pre_exposure_correction;
    vec3 hist_yc = TAA_RGBToYCoCg(hist_rgb);
    vec3 clipped_yc = TAA_ClipToAABB(hist_yc, mu_yc, sigma_yc * k);
    vec3 clamped_hist = max(TAA_YCoCgToRGB(clipped_yc), vec3_splat(0.0));

    float motion_w = smoothstep(TAA_MOTION_START_PX, TAA_MOTION_FULL_PX, reprojection_px);
    float history_weight = mix(u_history_blend, max(u_history_blend, TAA_STILL_HISTORY_BLEND), still);
    history_weight = mix(history_weight, min(history_weight, TAA_MOTION_HISTORY_BLEND), motion_w);
    history_weight = 1.0 - (1.0 - history_weight) * sample_alignment;
    float blend = history_weight * edge_fade * depth_ok * edge_blend * screen_border_w * history_border_w;
    // Karis-weighted resolve: weighting both terms by 1/(1+luma) evaluates the blend in
    // a tonemapped domain, so a single HDR firefly cannot dominate the average and
    // flicker as the jitter walks it on and off a sample position.
    const vec3 taa_luma_w = vec3(0.2126, 0.7152, 0.0722);
    float w_curr = (1.0 - blend) / (1.0 + dot(filtered, taa_luma_w));
    float w_hist = blend / (1.0 + dot(clamped_hist, taa_luma_w));
    vec3 resolved = (filtered * w_curr + clamped_hist * w_hist) / max(w_curr + w_hist, 1e-6);
    // The HISTORY target takes the resolve before any sharpening. Written back, the unsharp term
    // (computed from the resolved value, which already carries last frame's sharpening) compounded
    // frame after frame - a runaway that only its caps bounded, growing at rest (history 0.95, clip
    // x2) into speckled, blotchy micro-occlusion. Sharpening is a display-only step.
    vec3 history_resolved = resolved;
    if(u_sharpen > 0.001)
    {
        const vec3 luma_dir = vec3(0.212656, 0.715158, 0.072186);
        vec3 nb_rng = max(nb_max - nb_min, vec3_splat(1e-6));
        float contrast = max(nb_rng.x, max(nb_rng.y, nb_rng.z));
        // Strong attenuation on HDR edges (contrast^2 term); stops black/white ringing from unsharp.
        float sharpen_adapt = rcp(1.0 + contrast * 0.7 + contrast * contrast * 0.12);
        float sharpen_w = u_sharpen * mix(0.12, 1.0, silhouette) * sharpen_adapt * (1.0 + TAA_MOTION_SHARPEN_BOOST * motion_w);

        vec3 pre_sharp = resolved;
        float y_curr = dot(filtered, luma_dir);
        // Blur reference from last frame's resolved history around the reprojected position: it holds still
        // under the jitter, where this frame's raw 3x3 mean replays each frame's sub-pixel offset into the unsharp
        // term. Where the history is not trusted the current 3x3 mean stands in.
        vec3 history_blur = (texture2DLod(s_history, hist_uv + vec2(texel.x, 0.0), 0.0).rgb +
                             texture2DLod(s_history, hist_uv - vec2(texel.x, 0.0), 0.0).rgb +
                             texture2DLod(s_history, hist_uv + vec2(0.0, texel.y), 0.0).rgb +
                             texture2DLod(s_history, hist_uv - vec2(0.0, texel.y), 0.0).rgb) *
                            (0.25 * u_history_pre_exposure_correction);
        float history_validity = blend / max(history_weight, 1e-4);
        float y_mu = mix(dot(mu, luma_dir), dot(history_blur, luma_dir), history_validity);
        float y_res = dot(pre_sharp, luma_dir);
        // Unsharp term from the RESOLVED value, never the jittered centre sample: current minus its
        // 3x3 mean replays the sub-pixel jitter into the output every frame (the TAA study: the
        // sharpen was the largest rest-flicker source); the 9-tap mean barely moves with the jitter.
        float dy = (y_res - y_mu) * sharpen_w;
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

    gl_FragData[0] = vec4(resolved, curr.a);
    gl_FragData[1] = vec4(history_resolved, mover_memory);
}
