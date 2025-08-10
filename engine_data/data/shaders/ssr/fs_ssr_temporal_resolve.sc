$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_ssr_curr, 0);    // Current frame SSR result (rgb = color, a = confidence)
SAMPLER2D(s_ssr_history, 1); // Previous frame SSR history (rgb = filtered color, a = normalized weight)
SAMPLER2D(s_normal, 2);      // Normal buffer for validity checks
SAMPLER2D(s_depth, 3);       // Depth buffer for validity checks

uniform vec4 u_temporal_params; // x: enable_temporal, y: history_strength, z: depth_threshold, w: roughness_sensitivity
#define u_enable_temporal         u_temporal_params.x
#define u_history_strength        u_temporal_params.y
#define u_depth_threshold         u_temporal_params.z
#define u_roughness_sensitivity   u_temporal_params.w

uniform vec4 u_motion_params;    // x: motion_scale_pixels, y: normal_dot_threshold, z: max_accum_frames, w: unused
#define u_motion_scale_pixels     u_motion_params.x
#define u_normal_dot_threshold    u_motion_params.y
#define u_max_accum_frames        u_motion_params.z
#define u_motion_unused_w         u_motion_params.w

uniform vec4 u_fade_params;      // x: fade_in_start, y: fade_in_end, z: ssr_resolution_scale, w: unused
#define u_fade_in_start           u_fade_params.x
#define u_fade_in_end             u_fade_params.y
#define u_ssr_resolution_scale    u_fade_params.z
#define u_fade_unused_w           u_fade_params.w

uniform mat4 u_prev_view_proj;   // Previous frame view-projection matrix

// Constants for temporal accumulation
#define DECAY_MIN 0.80
#define DECAY_MAX 0.99
#define MAX_ROUGHNESS 0.6

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
vec4 ApplyTemporalAccumulation(
        vec4  curr,          // rgb = colour THIS frame,  a = confidence THIS frame
        vec2  uv,            // pixel UV
        float surface_z,     // depth THIS frame
        float roughness,
        vec3  ws_normal)
{
    // == 0. feature toggle ==================================================

    BRANCH
    if (u_enable_temporal <= 0.5)
        return curr;                           // temporal OFF → just pass through

    // == 1. reprojection ====================================================
    vec2 prev_uv = ComputePreviousFrameUV(uv, surface_z);
    BRANCH
    if (any(lessThan(prev_uv, vec2(0.0, 0.0))) ||
        any(greaterThan(prev_uv, vec2(1.0, 1.0))))
        return curr;                           // out of screen → no history

    // == 2. fetch history ===================================================
    vec4  hist     = texture2D(s_ssr_history, prev_uv);
    float W_hist   = hist.a * u_max_accum_frames;     // 0 … kMaxFrames
    vec3  C_hist   = hist.rgb;

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

    // == 5. decay & roughness modulation ===================================
    float decay_user = mix(DECAY_MIN, DECAY_MAX, clamp(u_history_strength,0.0,1.0));
    float decay      = decay_user * motion_f *
                       mix(1.0, GetRoughnessFade(roughness),
                           clamp(u_roughness_sensitivity,0.0,1.0));

    W_hist *= decay;



    // == 6. running mean merge =============================================
    float W_new = clamp(W_hist + W_curr, 0.0, u_max_accum_frames);   // ≤ kMaxFrames
    vec3  C_new = (C_hist * W_hist + C_curr * W_curr) / max(W_new, 1e-3);

    // == 7. store: rgb = filtered colour, alpha = normalised weight ========
    return vec4(C_new, W_new / u_max_accum_frames);
}

void main()
{
    vec2 uv = v_texcoord0;
    
    // Sample current frame SSR result
    vec4 curr_ssr = texture2D(s_ssr_curr, uv);
    
    // Early out if no SSR contribution
    BRANCH
    if (curr_ssr.a <= 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    
    // Sample G-buffer data for temporal validation
    GBufferDataNormalMetalRoughness normal_data = DecodeGBufferNormalMetalRoughness(uv, s_normal);
    float surface_z = DecodeGBufferDepth(uv, s_depth).depth01;
    
    // Apply temporal accumulation
    vec4 result = ApplyTemporalAccumulation(
        curr_ssr,
        uv,
        surface_z,
        normal_data.roughness,
        normal_data.world_normal
    );
    
    gl_FragColor = result;
} 