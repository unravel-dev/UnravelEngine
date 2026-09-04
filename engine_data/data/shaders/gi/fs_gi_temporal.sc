$input v_texcoord0

/*
 * SPLIT form of the temporal accumulation - the fallback when the fused
 * integrate+temporal program (fs_gi_probe_integrate_temporal.sc) is unavailable, and the
 * only form supporting the neighbourhood clamp (it needs this frame's gather as a
 * texture). Shared body: gi_temporal_kernel.sh.
 */

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_gi_current, 0);
SAMPLER2D(s_gi_history, 1);
SAMPLER2D(s_gi_depth, 2);
SAMPLER2D(s_gi_prev_depth, 3);
SAMPLER2D(s_gi_normal, 4);
SAMPLER2D(s_gi_history_moments, 5);
SAMPLER2D(s_gi_history_fast, 6);

#include "gi/gi_temporal_kernel.sh"

#define GI_COLOR_OUT   gl_FragData[0]
#define GI_MOMENTS_OUT gl_FragData[1]
#define GI_FAST_OUT    gl_FragData[2]

void main()
{
	vec2 uv = v_texcoord0;
	vec4 current = GiSanitize(texture2DLod(s_gi_current, uv, 0.0));
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	// The reconstruction is hoisted out of the kernel (the fused form owns it already); a
	// sky pixel's garbage value is never consumed past the kernel's sky test.
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	vec4 color;
	vec4 fast;
	vec4 moments;
	// The split form has no per-pixel screen share (the gather went through a texture):
	// the camera-motion collapse is off here, the fused form is the deliverable path.
	GiResolveTemporal(uv, current, depth, world_position, 0.0, color, fast, moments);
	GI_COLOR_OUT = color;
	GI_MOMENTS_OUT = moments;
	GI_FAST_OUT = fast;
}
