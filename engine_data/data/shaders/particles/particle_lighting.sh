#ifndef PARTICLE_LIGHTING_SH_HEADER_GUARD
#define PARTICLE_LIGHTING_SH_HEADER_GUARD

/*
 * Lit particles (vs_particle_lit.sc), lit at the particle: the sky's irradiance SH, the sun with its cascaded
 * shadow, and up to PARTICLE_MAX_LIGHTS nearby point / spot lights the CPU picked for the draw. Every term is
 * irradiance; the particle's color is its albedo (Lambert: radiance = albedo x irradiance / pi). Bound by
 * the renderer per lit draw (pipeline.cpp, the particle lighting).
 */

#include "../common.sh"
#include "../lighting.sh"

// The sun's shadow cascades and their world-to-shadow matrices (shadowmap_generator::submit_uniforms(8)).
SAMPLER2D(s_shadowMap0, 8);
SAMPLER2D(s_shadowMap1, 9);
SAMPLER2D(s_shadowMap2, 10);
SAMPLER2D(s_shadowMap3, 11);
uniform mat4 u_shadowMapMtx0;
uniform mat4 u_shadowMapMtx1;
uniform mat4 u_shadowMapMtx2;
uniform mat4 u_shadowMapMtx3;
/// z: one texel of the shadow map, in uv.
uniform vec4 u_params2;
/// y: stored shadow depth per world unit.
uniform vec4 u_shadowBiasParams;
// The sky's irradiance: 9 SH coefficients (lighting.sh), absolute.
SAMPLER2D(s_particle_irradiance, 12);
/// xyz: the direction the sun's light travels; w: its shadow cascades (0 = no shadow).
uniform vec4 u_particle_sun_direction;
/// rgb: the sun's irradiance (linear, lux, pre-exposed); w: the stored depth offset of linear shadow maps.
uniform vec4 u_particle_sun_color;
/// x: local lights; y: the view's pre-exposure (the SH is absolute); z: 1 when the batch has a normal map, w: 1
/// when it stores x and y only (the fragment shaders read both).
uniform vec4 u_particle_lighting;
/// Per light: (position, 1 / range), (axis, cos outer half angle), (linear color x candela x pre-exposure,
/// 1 / (cos inner - cos outer half angle)). A point light has cos outer -2, so its cone mask is 1.
uniform vec4 u_particle_lights[12];

#define PARTICLE_MAX_LIGHTS 4
/// Metres a receiver is pulled toward the sun before its shadow test (particles have no surface to bias by).
#define PARTICLE_SHADOW_BIAS 0.1

/// The sun's visibility through one cascade at @p position, 4 taps; -1 when the cascade does not cover it.
float particle_cascade_visibility(sampler2D shadow_map, mat4 shadow_matrix, vec3 position)
{
	vec4 coord = mul(shadow_matrix, vec4(position, 1.0));
	vec2 uv = coord.xy / coord.w;
	float depth = (coord.z + u_particle_sun_color.w) / coord.w;
	if(any(lessThan(uv, vec2_splat(0.01))) || any(greaterThan(uv, vec2_splat(0.99))) || depth <= 0.0 || depth >= 1.0)
	{
		return -1.0;
	}
	float receiver = depth - PARTICLE_SHADOW_BIAS * u_shadowBiasParams.y;
	float texel = u_params2.z;
	float lit = step(receiver, texture2DLod(shadow_map, uv + vec2(-0.5, -0.5) * texel, 0.0).x);
	lit += step(receiver, texture2DLod(shadow_map, uv + vec2(0.5, -0.5) * texel, 0.0).x);
	lit += step(receiver, texture2DLod(shadow_map, uv + vec2(-0.5, 0.5) * texel, 0.0).x);
	lit += step(receiver, texture2DLod(shadow_map, uv + vec2(0.5, 0.5) * texel, 0.0).x);
	return lit * 0.25;
}

/// The sun's visibility at @p position: the nearest cascade that covers it (lit outside them all).
float particle_sun_visibility(vec3 position)
{
	float cascades = u_particle_sun_direction.w;
	float visibility = -1.0;
	if(cascades > 0.5)
	{
		visibility = particle_cascade_visibility(s_shadowMap0, u_shadowMapMtx0, position);
	}
	if(visibility < 0.0 && cascades > 1.5)
	{
		visibility = particle_cascade_visibility(s_shadowMap1, u_shadowMapMtx1, position);
	}
	if(visibility < 0.0 && cascades > 2.5)
	{
		visibility = particle_cascade_visibility(s_shadowMap2, u_shadowMapMtx2, position);
	}
	if(visibility < 0.0 && cascades > 3.5)
	{
		visibility = particle_cascade_visibility(s_shadowMap3, u_shadowMapMtx3, position);
	}
	return visibility < 0.0 ? 1.0 : visibility;
}

/// Irradiance at @p position from the draw's nearby lights (no shadows), the same from every side.
vec3 particle_local_irradiance(vec3 position)
{
	vec3 total = vec3_splat(0.0);
	for(int i = 0; i < PARTICLE_MAX_LIGHTS; ++i)
	{
		if(float(i) >= u_particle_lighting.x)
		{
			break;
		}
		vec4 placement = u_particle_lights[i * 3];
		vec4 cone = u_particle_lights[i * 3 + 1];
		vec4 intensity = u_particle_lights[i * 3 + 2];
		vec3 to_light = placement.xyz - position;
		float distance_squared = dot(to_light, to_light);
		vec3 light_direction = to_light * inversesqrt(max(distance_squared, 1e-8));
		float falloff = LocalLightRangeMask(to_light, placement.w) / (distance_squared + LOCAL_LIGHT_DISTANCE_BIAS_SQR);
		total += intensity.rgb * (falloff * LocalLightSpotMask(light_direction, cone.xyz, vec2(cone.w, intensity.w)));
	}
	return total;
}

/**
 * The light a lit particle's vertices pass on (particle_fragment.sh, particle_pixel_light): @p ambient the
 * per-particle radiance factor, @p sun the sun's radiance factor and the wrap for surface lighting (zero for
 * volumetric), @p sun_local the direction toward the sun in the quad's (x, y, normal) frame.
 */
void particle_light_vertex(particle_vertex vertex, out vec3 ambient, out vec4 sun, out vec3 sun_local)
{
	uint emitter = vertex.emitter;
	uint flags = particle_flags(emitter);
	vec3 position = vertex.position;
	float pre_exposure = u_particle_lighting.y;
	float visibility = particle_static(emitter, PARTICLE_STATIC_NOISE).w > 0.5 ? particle_sun_visibility(position) : 1.0;
	vec3 sun_irradiance = u_particle_sun_color.rgb * visibility;
	vec3 local = particle_local_irradiance(position);
	vec3 toward_sun = -u_particle_sun_direction.xyz;
	sun_local = vec3(dot(toward_sun, vertex.axis_x), dot(toward_sun, vertex.axis_y), dot(toward_sun, vertex.normal));
	if((flags & PARTICLE_FLAG_LIT_SURFACE) != 0u)
	{
		vec3 sky = eval_irradiance_sh(s_particle_irradiance, vertex.normal) * pre_exposure;
		ambient = (sky + local) * RECIP_PI;
		sun = vec4(sun_irradiance * RECIP_PI, particle_static(emitter, PARTICLE_STATIC_DISTANCE).z);
		return;
	}
	// A small cloud: the sky's average irradiance (band 0) and the sun from every side.
	vec3 sky = eval_irradiance_sh_scaled(s_particle_irradiance, vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0)) * pre_exposure;
	ambient = (sky + sun_irradiance + local) * RECIP_PI;
	sun = vec4_splat(0.0);
}

#endif // PARTICLE_LIGHTING_SH_HEADER_GUARD
