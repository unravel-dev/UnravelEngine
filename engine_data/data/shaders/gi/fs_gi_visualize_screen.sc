$input v_texcoord0

/*
 * The GI screen-space debug views:
 *  mode 0, Dedicated Reflection Rays: the diffuse albedo's luminance lit from one direction in grey, and in red,
 *          brighter the smoother, every pixel that traces reflection rays (the traced reflections' fade: below the
 *          max roughness to trace, fading over 0.1); behind the scene, the sky along the camera ray (the
 *          environment's radiance SH the rays read), tone mapped.
 *  mode 1, Screen Probe Frames Accumulated: the gather history's 4-bit frame count / 15 in red over the grey of the
 *          finished image.
 * In a tile (u_gi_visualize_tile.xy > 0: the overview views) the view fills the tile, whose corners are rounded.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "../tonemapping/tonemapping.sh"
#include "gi/gi_common.sh"
#include "gi/gi_visualize.sh"
#include "../pre_exposure.sh"

/// The G-buffer's colour and normal targets and its depth.
SAMPLER2D(s_gbuffer0, 0);
SAMPLER2D(s_gbuffer1, 1);
SAMPLER2D(s_gbuffer_depth, 2);
/// The environment's radiance SH (9 texels, absolute radiance).
SAMPLER2D(s_gi_env_sh, 3);
/// This frame's gather history: a = GiEncodeHistoryAlpha (the frames accumulated, quantized to multiples of the
/// maximum / 15, and the fast update amount).
SAMPLER2D(s_gi_history, 4);
/// A copy of the finished image.
SAMPLER2D(s_scene_color, 5);

/// x = mode, y = the roughness below which pixels trace reflection rays, z = the lit image's tone mapping operator,
/// w = the view's exposure.
uniform vec4 u_gi_visualize;
/// xy = the tile's size in pixels (0 = the whole view), z = the gather's maximum frame count.
uniform vec4 u_gi_visualize_tile;

#define GI_VISUALIZE_DEDICATED_REFLECTION_RAYS 0
#define GI_VISUALIZE_SCREEN_PROBE_FRAMES 1
/// The roughness range over which traced reflections fade out, ending at the max roughness to trace.
#define GI_REFLECTION_ROUGHNESS_FADE_LENGTH 0.1
/// The history's frame count has 4 bits.
#define GI_HISTORY_FRAME_LEVELS 15.0
/// The opacity of the frame count drawn over the image.
#define GI_VISUALIZE_OVERLAY_OPACITY 0.8

vec3 GiCameraDirection(vec2 uv)
{
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(0.5)));
	vec3 world_point = clipToWorld(u_invViewProj, clip);
	vec3 origin = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	return normalize(world_point - origin);
}

vec3 GiDedicatedReflectionRays(vec2 uv)
{
	GBufferData data = DecodeGBufferSimple(uv, s_gbuffer0, s_gbuffer1, s_gbuffer_depth);
	if(data.depth01 >= 1.0)
	{
		// u_gi_visualize.w is the CPU's share of the pre-exposure; the GPU's share joins it here.
		vec3 sky = eval_radiance_sh(s_gi_env_sh, GiCameraDirection(uv)) * (u_gi_visualize.w * u_pre_exposure_gpu_scale);
		return apply_tonemapping(sky, int(u_gi_visualize.z), 1.0);
	}
	vec3 light_direction = vec3(-0.707, 0.707, 0.0);
	float lighting = 0.5 * dot(light_direction, normalize(data.world_normal)) + 0.5;
	vec3 color = vec3_splat(sqrt(dot(data.diffuse_color, vec3(0.3, 0.59, 0.11)) * lighting * 1.5));
	if(saturate((u_gi_visualize.y - data.roughness) / GI_REFLECTION_ROUGHNESS_FADE_LENGTH) > 0.0)
	{
		color = vec3(1.0, 0.0, 0.0) * ((1.0 - data.roughness) * 0.8 + 0.2);
	}
	return color;
}

vec3 GiScreenProbeFramesAccumulated(vec2 uv)
{
	vec3 scene = texture2D(s_scene_color, uv).xyz;
	float grey = dot(scene, vec3_splat(1.0)) / 3.0;
	ivec2 size = textureSize(s_gi_history, 0);
	ivec2 texel = min(ivec2(uv * vec2(size)), size - ivec2(1, 1));
	float frames = GiHistoryAlphaFrames(texelFetch(s_gi_history, texel, 0).w, max(u_gi_visualize_tile.z, 1.0));
	float bits = floor(saturate(frames / max(u_gi_visualize_tile.z, 1.0)) * GI_HISTORY_FRAME_LEVELS + 0.5);
	float amount = saturate(bits / GI_HISTORY_FRAME_LEVELS);
	return mix(vec3_splat(grey), vec3(amount, 0.0, 0.0), GI_VISUALIZE_OVERLAY_OPACITY);
}

void main()
{
	vec2 uv = v_texcoord0;
	vec2 tile_size = u_gi_visualize_tile.xy;
	if(tile_size.x > 0.0 && !GiIsInsideVisualizeTile(floor(uv * tile_size), tile_size))
	{
		discard;
	}
	BRANCH
	if(int(u_gi_visualize.x + 0.5) == GI_VISUALIZE_SCREEN_PROBE_FRAMES)
	{
		gl_FragColor = vec4(GiScreenProbeFramesAccumulated(uv), 1.0);
	}
	else
	{
		gl_FragColor = vec4(GiDedicatedReflectionRays(uv), 1.0);
	}
}
