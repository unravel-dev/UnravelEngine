$input v_texcoord0

/*
 * Lumen's screen-space debug views (UE 5.8 r.Lumen.Visualize, value in brackets):
 *  mode 0, Dedicated Reflection Rays [7] (LumenVisualize.ush LumenVisualizationFinalize): the diffuse albedo's
 *          luminance lit from one direction in grey, and in red, brighter the smoother, every pixel Lumen traces
 *          reflection rays for (LumenCombineReflectionsAlpha: below the max roughness to trace, fading over 0.1);
 *          behind the scene, the sky along the camera ray (UE EvaluateSkyRadiance: here the environment's radiance
 *          SH our rays read), tone mapped.
 *  mode 1, ScreenProbeGather Frames Accumulated [23] (LumenVisualize.usf VisualizeBitFieldFloatTexturePS): the
 *          gather history's 4-bit frame count / 15 in red over the grey of the finished image.
 * In a tile (u_lumen_visualize_tile.xy > 0: UE's overview) the view fills the tile, whose corners are rounded.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "../tonemapping/tonemapping.sh"
#include "lumen/lumen_visualize.sh"

/// The G-buffer's colour and normal targets and its depth.
SAMPLER2D(s_gbuffer0, 0);
SAMPLER2D(s_gbuffer1, 1);
SAMPLER2D(s_gbuffer_depth, 2);
/// The environment's radiance SH (9 texels, absolute radiance).
SAMPLER2D(s_lumen_env_sh, 3);
/// This frame's gather history: a = the frames accumulated, quantized to multiples of the maximum / 15.
SAMPLER2D(s_lumen_history, 4);
/// A copy of the finished image.
SAMPLER2D(s_scene_color, 5);

/// x = mode, y = the roughness below which Lumen traces reflection rays, z = the lit image's tone mapping operator,
/// w = the view's exposure.
uniform vec4 u_lumen_visualize;
/// xy = the tile's size in pixels (0 = the whole view), z = the gather's maximum frame count.
uniform vec4 u_lumen_visualize_tile;

#define LUMEN_VISUALIZE_DEDICATED_REFLECTION_RAYS 0
#define LUMEN_VISUALIZE_SCREEN_PROBE_FRAMES 1
/// LumenCombineReflectionsAlpha's fade above the max roughness to trace.
#define LUMEN_REFLECTION_ROUGHNESS_FADE_LENGTH 0.1
/// The history's frame count has 4 bits.
#define LUMEN_HISTORY_FRAME_LEVELS 15.0
/// UE draws the frame count over the image at this opacity.
#define LUMEN_VISUALIZE_OVERLAY_OPACITY 0.8

vec3 LumenCameraDirection(vec2 uv)
{
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(0.5)));
	vec3 world_point = clipToWorld(u_invViewProj, clip);
	vec3 origin = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	return normalize(world_point - origin);
}

vec3 LumenDedicatedReflectionRays(vec2 uv)
{
	GBufferData data = DecodeGBufferSimple(uv, s_gbuffer0, s_gbuffer1, s_gbuffer_depth);
	if(data.depth01 >= 1.0)
	{
		vec3 sky = eval_radiance_sh(s_lumen_env_sh, LumenCameraDirection(uv)) * u_lumen_visualize.w;
		return apply_tonemapping(sky, int(u_lumen_visualize.z), 1.0);
	}
	vec3 light_direction = vec3(-0.707, 0.707, 0.0);
	float lighting = 0.5 * dot(light_direction, normalize(data.world_normal)) + 0.5;
	vec3 color = vec3_splat(sqrt(dot(data.diffuse_color, vec3(0.3, 0.59, 0.11)) * lighting * 1.5));
	if(saturate((u_lumen_visualize.y - data.roughness) / LUMEN_REFLECTION_ROUGHNESS_FADE_LENGTH) > 0.0)
	{
		color = vec3(1.0, 0.0, 0.0) * ((1.0 - data.roughness) * 0.8 + 0.2);
	}
	return color;
}

vec3 LumenScreenProbeFramesAccumulated(vec2 uv)
{
	vec3 scene = texture2D(s_scene_color, uv).xyz;
	float grey = dot(scene, vec3_splat(1.0)) / 3.0;
	ivec2 size = textureSize(s_lumen_history, 0);
	ivec2 texel = min(ivec2(uv * vec2(size)), size - ivec2(1, 1));
	float frames = texelFetch(s_lumen_history, texel, 0).w;
	float bits = floor(saturate(frames / max(u_lumen_visualize_tile.z, 1.0)) * LUMEN_HISTORY_FRAME_LEVELS + 0.5);
	float amount = saturate(bits / LUMEN_HISTORY_FRAME_LEVELS);
	return mix(vec3_splat(grey), vec3(amount, 0.0, 0.0), LUMEN_VISUALIZE_OVERLAY_OPACITY);
}

void main()
{
	vec2 uv = v_texcoord0;
	vec2 tile_size = u_lumen_visualize_tile.xy;
	if(tile_size.x > 0.0 && !LumenIsInsideVisualizeTile(floor(uv * tile_size), tile_size))
	{
		discard;
	}
	BRANCH
	if(int(u_lumen_visualize.x + 0.5) == LUMEN_VISUALIZE_SCREEN_PROBE_FRAMES)
	{
		gl_FragColor = vec4(LumenScreenProbeFramesAccumulated(uv), 1.0);
	}
	else
	{
		gl_FragColor = vec4(LumenDedicatedReflectionRays(uv), 1.0);
	}
}
