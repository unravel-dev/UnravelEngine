$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"

// Current frame SSR result (rgb = color, a = confidence)
SAMPLER2D(s_ssr_curr, 0);
// Previous frame SSR history (rgb = filtered color, a = normalized weight)
SAMPLER2D(s_ssr_history, 1);
// Normal buffer for validity checks
SAMPLER2D(s_normal, 2);
// Depth buffer for validity checks
SAMPLER2D(s_depth, 3);
// Velocity buffer: RG = total uv-delta (uv_curr - uv_prev, unjittered prev), BA = the
// object-only component. Produced by the deferred velocity pass; authoritative when bound.
SAMPLER2D(s_velocity, 4);
// The trace's confidence-weighted mean hit distance THIS frame (view-space metres,
// 0 = no confident hit), and its accumulated history. Together they carry the per-pixel
// content validation that replaced the screen-wide mover cap.
SAMPLER2D(s_ssr_curr_hit_t, 5);
SAMPLER2D(s_ssr_hist_hit_t, 6);

uniform vec4 u_temporal_params;
#define u_enable_temporal         u_temporal_params.x
#define u_history_strength        u_temporal_params.y
#define u_depth_threshold         u_temporal_params.z
#define u_roughness_sensitivity   u_temporal_params.w

uniform vec4 u_motion_params;
#define u_motion_scale_pixels     u_motion_params.x
#define u_normal_dot_threshold    u_motion_params.y
#define u_max_accum_frames        u_motion_params.z
// 1 = reproject through the velocity buffer; 0 = legacy prev view-projection path.
#define u_velocity_available      u_motion_params.w

uniform vec4 u_fade_params;
// x = CONTENT-LAG release ceiling: 0.125 while the velocity pass drew any mover within
// one accumulation window, 1.0 otherwise. The trace samples PREV_SCENE_HDR, so a moving
// emitter's radiance sweeps every glossy surface ONE FRAME LATE while the geometry at
// the hit stays static and t-confirmed - a radiance-only change no per-pixel geometric
// guard can see (the hit-t signals below cover content entering/leaving the REFLECTION;
// they cannot cover stale COLOUR at a confirmed hit). A shallow window is the correct
// window for radiance that changes every frame; converged static content agrees with
// its neighbourhood box and loses only the release's tail. y unused.
#define u_content_lag_ceiling     u_fade_params.x
// Per-axis (full_dim / pass_dim) ratio. Scalar scale is wrong at odd full-res dims; see
// HizScreenPassToFullResUV docs in hiz_trace.sh.
#define u_ssr_resolution_scale    u_fade_params.zw

uniform mat4 u_prev_view_proj;

// Constants for temporal accumulation
#define DECAY_MIN 0.80
#define DECAY_MAX 0.99
#define MAX_ROUGHNESS 0.6
// Trace-target pixels of reprojection motion below which the receiver counts as still and
// the history clamp releases (the whole chain runs on TAA-unjittered matrices, so a parked
// camera reads ~0; the slack covers the full-uv <-> half-uv mapping).
#define SSR_CLAMP_MOTION_PIXELS 2.0
// Hit-distance history compare: relative ray-length change tolerated before the clamp
// engages, ramped by roughness - a wide lobe's per-frame mean-t genuinely wanders with the
// stochastic sample set, and a false mismatch would erode exactly the accumulation the
// release protects. The mismatch saturates at twice the tolerance. The distance floor
// keeps near-contact reflections (tiny t) from turning quantisation into rejects.
#define SSR_HIT_T_TOLERANCE_SHARP 0.15
#define SSR_HIT_T_TOLERANCE_ROUGH 0.75
#define SSR_HIT_T_MIN_DISTANCE    0.25
// Mismatch assigned when the history CLAIMS content (hist_t > 0) but this frame has no
// confident hit at all, on a ROUGH pixel. On a mirror the verdict is absolute (1.0): a
// deterministic surviving hit does not stochastically vanish, so "nothing found" means
// the content departed or was occluded - the exact pixels where departed-mover ghosts
// lived (grazing rays into the revealed background fail SSR validation, curr_t reads 0,
// and gates that abstained on missing data granted those ghosts the full release). On
// gloss a multi-ray dropout is genuinely possible, so the verdict softens to this.
#define SSR_NO_DATA_MISMATCH_ROUGH 0.4
// Fraction of the history WEIGHT a fully failed confirmation removes per frame. The
// neighbourhood clamp alone flushes only as well as the 3x3 box is tight, and a noisy
// stochastic surface (visible speckle) has a box wide enough to retain a ghost INSIDE
// it - the weight cut is box-independent: with the history weight gone, the merge takes
// the current frame and the ghost dies in ~2 frames regardless of local variance.
#define SSR_MISMATCH_WEIGHT_CUT 0.75

// Temporal reprojection functions
vec2 WorldToScreenPrevious(vec3 ws_pos)
{
    vec4 prev_clip4 = mul(u_prev_view_proj, vec4(ws_pos, 1.0));
    vec3 prev_clip = prev_clip4.xyz / prev_clip4.w;
    prev_clip = clipTransform(prev_clip);
    return prev_clip.xy * 0.5 + 0.5;
}

vec3 ComputeViewspacePosition(vec2 uv, float z)
{
    return computeViewSpacePosition(uv, z);
}

// Function to compute previous frame UV coordinates for temporal reprojection
vec2 ComputePreviousFrameUV(vec2 uv, float z)
{
    // Reconstruct world position from current UV and depth
    vec3 vs_pos = ComputeViewspacePosition(uv, z);
    vec4 ws_pos = mul(u_invView, vec4(vs_pos, 1.0));
    return WorldToScreenPrevious(ws_pos.xyz);
}

float GetRoughnessFade(float roughness)
{
    return MAX_ROUGHNESS - min(roughness, MAX_ROUGHNESS);
}

// ---------------------------------------------------------------------------
//  Temporal resolve: running-mean colour, running weight in alpha
//  * colour_out = filtered colour
//  * alpha_out  = W_new / max_accum_frames    (for next frame only)
// ---------------------------------------------------------------------------
// Struct return, deliberately NOT an out parameter: an out-param helper in a fragment
// .sc miscompiled silently on the HLSL path once before (the recorded TAA dilation
// lesson) - struct returns are the proven-safe shape.
struct SsrTemporalResult
{
    vec4 color;     // rgb = filtered colour, a = normalised weight
    float hit_t;    // resolved hit-distance history for next frame
};

SsrTemporalResult ApplyTemporalAccumulation(
        vec4  curr,          // rgb = colour THIS frame,  a = confidence THIS frame
        vec2  uv,            // pixel UV (full-res space, for reprojection / motion)
        vec2  curr_uv,       // pixel UV in the trace target (for the neighbourhood clamp)
        float surface_z,     // depth THIS frame
        float roughness,
        vec3  ws_normal)
{
    float curr_t = texture2DLod(s_ssr_curr_hit_t, curr_uv, 0.0).x;
    SsrTemporalResult result;
    result.color = curr;
    result.hit_t = curr_t;

    // == 0. feature toggle ==================================================

    BRANCH
    if (u_enable_temporal <= 0.5)
        return result;                         // temporal OFF → just pass through

    // == 1. reprojection ====================================================
    // Camera-consistent pixels ALWAYS use this pass's own matrix reprojection; the
    // velocity buffer's RG drives only OBJECT-motion pixels (BA = the object-only split).
    // Trusting RG for camera pixels drags the whole image: the buffer's camera component
    // is written with a previous view-projection that is NOT reliably this pass's own
    // (measured; open engine issue - see the velocity plan). Same gating as the TAA
    // resolve, which this pattern was proven on.
    vec2 prev_uv = ComputePreviousFrameUV(uv, surface_z);
    BRANCH
    if (u_velocity_available > 0.5)
    {
        vec4 vel4 = texture2DLod(s_velocity, uv, 0.0);
        vec2 vel_dim = vec2(textureSize(s_velocity, 0));
        float object_w = smoothstep(0.5, 1.5, length(vel4.zw * vel_dim));
        prev_uv = mix(prev_uv, uv - vel4.xy, object_w);
    }
    BRANCH
    if (any(lessThan(prev_uv, vec2(0.0, 0.0))) ||
        any(greaterThan(prev_uv, vec2(1.0, 1.0))))
        return result;                         // out of screen → no history

    // == 2. fetch history ===================================================
    vec4  hist     = texture2D(s_ssr_history, prev_uv);
    float W_hist   = hist.a * u_max_accum_frames;     // 0 … kMaxFrames
    vec3  C_hist   = hist.rgb;
    float hist_t   = texture2DLod(s_ssr_hist_hit_t, prev_uv, 0.0).x;

    float W_curr   = curr.a;                         // confidence this frame
    vec3  C_curr   = curr.rgb;

    // == 3. validity gate (depth / normal) =================================
    float depth_prev  = DecodeGBufferDepth(prev_uv, s_depth ).depth01;
    vec3  normal_prev = DecodeGBufferNormalMetalRoughness(prev_uv, s_normal).world_normal;

    bool depth_ok = abs(depth_prev - surface_z) <
                     u_depth_threshold;            // depth_threshold
    bool normal_ok = dot(normal_prev, ws_normal) >
                      u_normal_dot_threshold;             // normal_dot_threshold


    W_hist *= float(depth_ok);
	W_hist *= float(normal_ok);                                // hard reset

	// == 4. motion attenuation =============================================
	// OLD -----------------------------
	// float motion_px = length(prev_uv - uv) *
	//                   u_motion_scale_pixels * rcp(textureSize(s_ssr_curr,0).y);
	// float motion_f  = clamp(1.0 - motion_px, 0.0, 1.0);

	// NEW -----------------------------
	vec2  screen     = vec2(textureSize(s_ssr_curr,0));
	float motion_px  = length(prev_uv - uv) * screen.y;
	float motion_f   = clamp(1.0 - motion_px / u_motion_scale_pixels, 0.0, 1.0);

    // == 4.5 neighbourhood clamp (per-pixel content validated) ==============
    // The missing standard guard: the depth/normal gates above validate only the
    // RECEIVER, which is static under a moving REFLECTION - a mover's departed image
    // passed every gate and washed out only at the EMA rate (measured: red streaks
    // trailing emissive movers on a mirror floor, on a parked camera that read zero
    // motion). History is clamped to the 3x3 bounds of THIS frame's samples whenever
    // any of three signals distrusts it; a parked camera over unchanged content keeps
    // the full release, so stochastic convergence is untouched exactly where it is
    // possible. Bounds come from confident neighbours only: a zero-confidence
    // neighbour is not an image and must not drag the box toward black.
    vec3 clamp_lo = C_curr;
    vec3 clamp_hi = C_curr;
    vec2 curr_texel = vec2_splat(1.0) / screen;
    LOOP
    for(int y = -1; y <= 1; ++y)
    {
        LOOP
        for(int x = -1; x <= 1; ++x)
        {
            if(x == 0 && y == 0)
            {
                continue;
            }
            vec4 s = texture2DLod(s_ssr_curr, curr_uv + vec2(float(x), float(y)) * curr_texel, 0.0);
            if(s.a > 1e-3)
            {
                clamp_lo = min(clamp_lo, s.rgb);
                clamp_hi = max(clamp_hi, s.rgb);
            }
        }
    }
    // Signal 1 - RECEIVER motion: the pixel's viewing geometry moved.
    float still = 1.0 - saturate(motion_px / SSR_CLAMP_MOTION_PIXELS);
    // Signal 1b - CONTENT LAG (screen-wide, see the uniform): radiance-only change from
    // movers sweeping the prev-scene input, invisible to every per-pixel signal below.
    still = min(still, u_content_lag_ceiling);
    // Signal 2 - HIT-DISTANCE HISTORY CONFIRMATION, the local discriminator that
    // replaced the screen-wide mover cap. The history holds its release only while the
    // present RE-OBSERVES roughly the content it describes: a ray length within
    // tolerance of hist_t. Content that appeared, departed, or changed depth moves the
    // ray length no matter what the receiver gates read, and unlike a velocity probe
    // this cannot be fooled by a DEPARTED mover (the jump to the revealed background's
    // distance IS the signal). Crucially, "no confident hit at all" is NOT an
    // abstention when the history claims content - that exact gap is where the ghosts
    // lived: grazing rays into the revealed background fail SSR validation, and gates
    // that required current data granted precisely those pixels the full release.
    // hist_t = 0 (fresh/reset history) carries no claim - nothing to confirm.
    float t_mismatch = 0.0;
    BRANCH
    if(hist_t > 0.0)
    {
        BRANCH
        if(curr_t > 0.0)
        {
            float t_rel = abs(curr_t - hist_t) / max(min(curr_t, hist_t), SSR_HIT_T_MIN_DISTANCE);
            float t_tolerance = mix(SSR_HIT_T_TOLERANCE_SHARP, SSR_HIT_T_TOLERANCE_ROUGH,
                                    saturate(roughness / MAX_ROUGHNESS));
            t_mismatch = saturate(t_rel / t_tolerance - 1.0);
        }
        else
        {
            // Unconfirmed with no data: absolute on mirrors, softened on gloss where a
            // stochastic multi-ray dropout is genuinely possible.
            t_mismatch = mix(1.0, SSR_NO_DATA_MISMATCH_ROUGH, saturate(roughness / MAX_ROUGHNESS));
        }
        still = min(still, 1.0 - t_mismatch);
    }
    // Signal 3 - VELOCITY AT THE HIT: content confirmed moving RIGHT NOW forfeits the
    // release (tighten-only: a departed mover reads static at its ghost's pixels - the
    // current hit is the revealed background - which is exactly what signal 2 covers).
    // The mirror direction stands in for the stochastic lobe centre, as in the GI
    // reflection temporal.
    BRANCH
    if(u_velocity_available > 0.5 && curr_t > 0.0)
    {
        vec3 vs_pos = ComputeViewspacePosition(uv, surface_z);
        vec3 ws_pos = mul(u_invView, vec4(vs_pos, 1.0)).xyz;
        vec3 cam_pos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
        vec3 view_dir = normalize(cam_pos - ws_pos);
        vec3 mirror_dir = normalize(reflect(-view_dir, ws_normal));
        vec4 hit_clip = mul(u_viewProj, vec4(ws_pos + mirror_dir * curr_t, 1.0));
        BRANCH
        if(hit_clip.w > 1e-6)
        {
            vec3 hit_ndc = clipTransform(hit_clip.xyz / hit_clip.w);
            vec2 hit_uv = hit_ndc.xy * 0.5 + 0.5;
            if(all(greaterThanEqual(hit_uv, vec2_splat(0.0))) &&
               all(lessThanEqual(hit_uv, vec2_splat(1.0))))
            {
                vec4 hit_vel = texture2DLod(s_velocity, hit_uv, 0.0);
                vec2 vel_dim = vec2(textureSize(s_velocity, 0));
                float hit_motion = smoothstep(0.5, 1.5, length(hit_vel.zw * vel_dim));
                still = min(still, 1.0 - hit_motion);
            }
        }
    }
    C_hist = mix(clamp(C_hist, clamp_lo, clamp_hi), C_hist, still);
    // WEIGHT CUT on failed confirmation (box-independent, see the constant): the clamp
    // above flushes only as well as the box is tight, and a speckled stochastic surface
    // has a box wide enough to keep a ghost inside it. Tied to the CONFIRMATION signal
    // only - receiver motion already has its own decay below, and the hit-velocity
    // tighten covers content that is still present (its history stays valid).
    W_hist *= 1.0 - t_mismatch * SSR_MISMATCH_WEIGHT_CUT;

    // == 5. decay & roughness modulation ===================================
    float decay_user = mix(DECAY_MIN, DECAY_MAX, clamp(u_history_strength,0.0,1.0));
    float decay      = decay_user * motion_f *
                       mix(1.0, GetRoughnessFade(roughness),
                           clamp(u_roughness_sensitivity,0.0,1.0));

    W_hist *= decay;



    // == 6. running mean merge =============================================
    // IMPORTANT: divide by the TRUE sum of weights (W_total), not by the
    // clamp-capped W_new. Using the cap as the divisor breaks energy
    // conservation once history saturates (numerator keeps growing while
    // denominator is pinned), which manifests as brightness drift / reduced
    // current-frame contribution at small max_accum_frames.
    float W_total = W_hist + W_curr;
    float W_new   = min(W_total, u_max_accum_frames);   // stored weight only
    vec3  C_new   = (C_hist * W_hist + C_curr * W_curr) / max(W_total, 1e-3);

    // == 7. store: rgb = filtered colour, alpha = normalised weight; hit-distance
    // history merges under the SAME weights as the colour, so the stored t always
    // describes the content the accumulated image actually shows.
    result.color = vec4(C_new, W_new / max(u_max_accum_frames, 1.0));
    result.hit_t = (hist_t * W_hist + curr_t * W_curr) / max(W_total, 1e-3);
    return result;
}

void main()
{
    vec2 half_uv = v_texcoord0;
    vec2 depth_dim = vec2(textureSize(s_depth, 0));
    vec2 full_uv = HizScreenPassToFullResUV(half_uv, u_ssr_resolution_scale, depth_dim);

    // Sample current frame SSR result (half-res texture; same normalized UV as G-buffer extent)
    vec4 curr_ssr = texture2D(s_ssr_curr, half_uv);
    
    // Early out if no SSR contribution: colour AND hit-distance history both reset -
    // a zero t is "no confident data" to next frame's compare.
    BRANCH
    if (curr_ssr.a <= 0.0)
    {
        gl_FragData[0] = vec4(0.0, 0.0, 0.0, 0.0);
        gl_FragData[1] = vec4_splat(0.0);
        return;
    }

    // Sample G-buffer at full-res UV aligned with this half-res pixel
    GBufferDataNormalMetalRoughness normal_data = DecodeGBufferNormalMetalRoughness(full_uv, s_normal);
    float surface_z = DecodeGBufferDepth(full_uv, s_depth).depth01;

    // Apply temporal accumulation (uv = full-screen space for reprojection / motion;
    // half_uv = trace-target space for the neighbourhood clamp and hit-t fetches)
    SsrTemporalResult result = ApplyTemporalAccumulation(
        curr_ssr,
        full_uv,
        half_uv,
        surface_z,
        normal_data.roughness,
        normal_data.world_normal
    );

    gl_FragData[0] = result.color;
    gl_FragData[1] = vec4(result.hit_t, 0.0, 0.0, 0.0);
} 