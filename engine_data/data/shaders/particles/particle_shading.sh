#ifndef PARTICLE_SHADING_SH_HEADER_GUARD
#define PARTICLE_SHADING_SH_HEADER_GUARD

/*
 * What the main of a material drawn on particles reads and writes besides its graph (material_graph/mg_particle.sh): the
 * view's pre-exposure, the fog in front of the particle, the soft fade against the scene's depth, the light a lit
 * particle's vertices passed on, and what the TAA reactive target takes.
 */

// The lit draws' vertex shaders read the sky SH at stage 12 (particle_lighting.sh); every particle draw reads the
// GPU's share of the pre-exposure from its texel 9 there (pre_exposure.sh PRE_EXPOSURE_ENVIRONMENT).
SAMPLER2D(s_particle_irradiance, 12);
#define PRE_EXPOSURE_ENVIRONMENT s_particle_irradiance
#include "../pre_exposure.sh"
#include "../shaderlib.sh"
#ifdef FOG_VOLUME_SAMPLER
// The volumetric fog's integrated froxel column, read by the unlit draws, which define FOG_VOLUME_SAMPLER. The lit
// draws hold the sun's cascades at stages 8-11 (particle_lighting.sh) and fog with the analytic fog alone.
SAMPLER3D(s_particle_fog_volume, 8);
#endif
#include "../fog/fog.sh"

// The view's opaque depth (device depth in .x), when u_particleParams.x is 1.
SAMPLER2D(s_sceneDepth, 15);
/// x = 1 when s_sceneDepth holds the scene depth, y = the blend the draw's blend state has (0 normal, 1 additive,
/// 2 multiply, 3 premultiplied: the material's Blend Mode).
uniform vec4 u_particleParams;

#define PARTICLE_BLEND_ADDITIVE 1.0
#define PARTICLE_BLEND_MULTIPLY 2.0
#define PARTICLE_BLEND_PREMULTIPLIED 3.0

bool particle_is_additive()
{
	return abs(u_particleParams.y - PARTICLE_BLEND_ADDITIVE) < 0.5;
}

bool particle_is_multiply()
{
	return abs(u_particleParams.y - PARTICLE_BLEND_MULTIPLY) < 0.5;
}

bool particle_is_premultiplied()
{
	return abs(u_particleParams.y - PARTICLE_BLEND_PREMULTIPLIED) < 0.5;
}

/// Soft particles: 0 where the particle at @p frag_coord touches the scene behind it, 1 at
/// @p fade_distance metres in front of it. @p particle_depth is the particle's view depth.
float particle_soft_fade(vec4 frag_coord, float fade_distance, float particle_depth)
{
	float fade = 1.0;
	if(u_particleParams.x > 0.5 && fade_distance > 0.0)
	{
		vec2 uv = frag_coord.xy * u_viewTexel.xy;
		float scene_depth = screenSpaceToViewSpaceDepth(texture2DLod(s_sceneDepth, uv, 0.0).x);
		fade = clamp((scene_depth - particle_depth) / fade_distance, 0.0, 1.0);
	}
	return fade;
}

/**
 * The fog between the camera and the particle (fog.sh) on what the blend state takes, @p color. The particles draw
 * after the fog composite, over a frame that holds the fog up to whatever is behind them. Normal and premultiplied
 * particles are seen through the fog in front of them and hide its in-scatter from behind them where they cover
 * the pixel: rgb x transmittance + in-scatter x alpha. Additive ones only dim with the transmittance. A multiply
 * filter fades toward white. @p particle_depth is the particle's view depth.
 */
vec4 particle_apply_fog(vec4 color, vec4 frag_coord, float particle_depth)
{
	if(!fog_is_enabled())
	{
		return color;
	}
	vec2 uv = frag_coord.xy * u_viewTexel.xy;
	// The pixel's ray at the particle's view depth (clip w of the perspective projection the fog requires).
	vec3 far_point = computeViewSpacePosition(uv, 1.0);
	vec3 view_position = far_point * (particle_depth / far_point.z);
	vec3 offset = mul(u_invView, vec4(view_position, 0.0)).xyz;
	float point_distance = length(offset);
	vec3 view_direction = offset / max(point_distance, 1e-6);
	vec4 fog = fog_to_point(s_particle_irradiance, uv, view_direction, point_distance, false, u_pre_exposure_value);
	if(particle_is_multiply())
	{
		return vec4(mix(vec3_splat(1.0), color.rgb, fog.a), color.a);
	}
	if(particle_is_additive())
	{
		return vec4(color.rgb * fog.a, color.a);
	}
	return vec4(color.rgb * fog.a + fog.rgb * color.a, color.a);
}

/// The second render target, blended with MAX: the share of the pixel the particle covers, scaled by its
/// emitter's TAA response. The TAA trusts its history less there: a long history smears what moves
/// without motion vectors.
vec4 particle_reactive(float coverage, float taa_response)
{
	return vec4(clamp(coverage * taa_response, 0.0, 1.0), 0.0, 0.0, 0.0);
}

/// The light a lit particle's pixel receives where its surface faces @p normal, in the quad's frame (x along its
/// width, y up it, z toward the camera): the per-particle part @p ambient, plus the sun wrapped around the surface
/// (@p sun, with the wrap in w; volumetric particles leave the sun out of it) from @p sun_local.
vec3 particle_surface_light(vec3 ambient, vec4 sun, vec3 sun_local, vec3 normal)
{
	float wrap = sun.w;
	float diffuse = clamp((dot(normal, sun_local) + wrap) / (1.0 + wrap), 0.0, 1.0);
	// The vertex shader's light carries the CPU's share of the pre-exposure only.
	return (ambient + sun.rgb * diffuse) * u_pre_exposure_gpu_scale;
}

#if MATERIAL_PARTICLE_SPECULAR
/// xyz: the direction the sun's light travels (the lit draws set it for their vertices, particle_lighting.sh).
uniform vec4 u_particle_sun_direction;
/// The least a reflecting normal faces the camera: the sphere a quad stands for ends before the quad's corners do.
#define PARTICLE_MIN_REFLECTING_FACING 0.1

/// The light a lit particle's pixel reflects off its surface: the sun's highlight, and the light around the particle
/// as a surface of @p specular_color and @p roughness mirrors it, so a metal particle shows its colour and not a
/// black body. @p ambient is the per-particle radiance factor and @p sun the sun's irradiance over pi with its shadow
/// (zero for volumetric particles, which show no highlight), as particle_surface_light takes them. @p toward_sun,
/// the unit @p normal and @p view (toward the camera) are in one frame, the pixel's own.
vec3 particle_reflected_light(vec3 ambient, vec4 sun, vec3 toward_sun, vec3 normal, vec3 view, vec3 specular_color, float roughness)
{
	// The light around stands for what the surface mirrors, as a surface seen head on mirrors it, the same over the
	// whole particle: a particle reads no reflection probe, and a rim would outline every sprite.
	vec3 reflected = ambient * EnvBRDFApprox(specular_color, roughness, 1.0);
	AreaLight light = MakeDirectionalAreaLight(normal, toward_sun);
	if(light.NoL > 0.0)
	{
		vec3 highlight = StandardShadingDirect(vec3_splat(0.0), specular_color, GetLightingRoughness(roughness), light, view, normal, 1.0);
		reflected += sun.rgb * (PI * light.NoL) * highlight;
	}
	return reflected * u_pre_exposure_gpu_scale;
}
#endif

#endif // PARTICLE_SHADING_SH_HEADER_GUARD
