/*
 * GTAO temporal accumulation at the AO resolution. Camera-consistent pixels reproject
 * through the previous frame's view-projection (which also yields the expected previous
 * view depth for the disocclusion test against PREV_DEPTH); pixels the velocity buffer
 * marks as moving objects follow the velocity instead and skip the depth test, relying
 * on the 3x3 neighbourhood clamp that bounds every history sample. The clamp is what
 * keeps a moved occluder from leaving a ghost of its old occlusion behind.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "gtao_common.sh"

SAMPLER2D(s_gtao_current, 0);
SAMPLER2D(s_gtao_history, 1);
/// Full-resolution velocity (RG = motion in uv, BA = object-only motion, the mover gate).
SAMPLER2D(s_gtao_velocity, 2);
SAMPLER2D(s_gtao_depth_mips, 3);
/// Full-resolution device depth of the previous frame.
SAMPLER2D(s_gtao_prev_depth, 4);
IMAGE2D_WO(i_gtao_out, rgba8, 5);

/// Previous frame's view-projection (the same jitter state the previous depth was drawn with).
uniform mat4 u_gtao_prev_view_proj;
/// x = history strength (0 = off), y = velocity buffer bound (0/1), z = relative depth
/// tolerance for the disocclusion test, w = history valid (0 on the first frame / resize).
uniform vec4 u_gtao_temporal;

#define u_gtao_history_strength  u_gtao_temporal.x
#define u_gtao_velocity_bound    u_gtao_temporal.y
#define u_gtao_depth_tolerance   u_gtao_temporal.z
#define u_gtao_history_valid     u_gtao_temporal.w

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(u_gtao_size.xy);
	if(any(greaterThanEqual(texel, size)))
	{
		return;
	}
	vec2 uv = (vec2(texel) + vec2_splat(0.5)) * u_gtao_size.zw;
	vec4 current = texelFetch(s_gtao_current, texel, 0);
	float view_depth = texelFetch(s_gtao_depth_mips, texel, 0).x;
	if(u_gtao_history_valid < 0.5 || view_depth >= GTAO_SKY_DEPTH * 0.5)
	{
		imageStore(i_gtao_out, texel, current);
		return;
	}
	// Camera-consistent reprojection: this texel's pixel (the one its depth belongs to) through
	// last frame's matrix; prev_uv is that pixel's previous uv.
	vec2 pixel_uv = GtaoRayUv(uv);
	vec3 view_position = GtaoViewPosition(pixel_uv, view_depth);
	vec3 world_position = mul(u_invView, vec4(view_position, 1.0)).xyz;
	vec4 prev_clip = mul(u_gtao_prev_view_proj, vec4(world_position, 1.0));
	float history_weight = u_gtao_history_strength;
	if(prev_clip.w <= 1e-5)
	{
		imageStore(i_gtao_out, texel, current);
		return;
	}
	vec3 prev_ndc = prev_clip.xyz / prev_clip.w;
	vec2 prev_uv = clipToUv(prev_ndc.xy * 0.5 + vec2_splat(0.5));
	float expected_prev_depth = abs(prev_clip.w);
	// Movers: the velocity buffer's object-only lanes select the velocity path.
	float mover = 0.0;
	if(u_gtao_velocity_bound > 0.5)
	{
		vec4 velocity = texture2DLod(s_gtao_velocity, pixel_uv, 0.0);
		mover = smoothstep(0.5, 1.5, length(velocity.zw * u_gtao_full_size.xy));
		prev_uv = mix(prev_uv, pixel_uv - velocity.xy, mover);
	}
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		imageStore(i_gtao_out, texel, current);
		return;
	}
	// Disocclusion: the previous depth buffer at the reprojected uv must hold the depth this
	// surface had last frame. Movers skip it (their expected depth follows the camera, not
	// the object) and rely on the clamp below.
	if(mover < 0.5)
	{
		float prev_device = texture2DLod(s_gtao_prev_depth, prev_uv, 0.0).x;
		float prev_depth = GtaoViewDepthFromDevice(prev_device);
		float relative = abs(prev_depth - expected_prev_depth) / max(expected_prev_depth, 1e-4);
		history_weight *= 1.0 - smoothstep(u_gtao_depth_tolerance * 0.5, u_gtao_depth_tolerance, relative);
	}
	vec4 history = texture2DLod(s_gtao_history, GtaoTexelUvFromRay(prev_uv), 0.0);
	// Neighbourhood clamp of the history to the current 3x3 range (bent normal and
	// visibility alike): bounded ghosting on any motion the tests above let through.
	vec4 box_min = current;
	vec4 box_max = current;
	LOOP
	for(int dy = -1; dy <= 1; ++dy)
	{
		LOOP
		for(int dx = -1; dx <= 1; ++dx)
		{
			ivec2 tap = clamp(texel + ivec2(dx, dy), ivec2(0, 0), size - ivec2(1, 1));
			vec4 v = texelFetch(s_gtao_current, tap, 0);
			box_min = min(box_min, v);
			box_max = max(box_max, v);
		}
	}
	history = clamp(history, box_min, box_max);
	vec4 blended = mix(current, history, history_weight);
	vec3 bent = blended.xyz * 2.0 - vec3_splat(1.0);
	vec3 bent_out = dot(bent, bent) > 1e-8 ? normalize(bent) : GtaoDecodeNormal(current);
	imageStore(i_gtao_out, texel, GtaoEncode(bent_out, blended.a));
}
