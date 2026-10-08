#ifndef __GI_GPU_LIGHTS_SH__
#define __GI_GPU_LIGHTS_SH__

/*
 * Scene lights as enumerable data.
 *
 * MIRROR OF engine/engine/rendering/gpu_light_buffer.h. The packing must match exactly.
 *
 * The deferred path draws one fullscreen pass per light with that light's parameters in
 * uniforms, which cannot answer "how much light reaches world point P" from inside a compute
 * shader -- there is only ever the one light currently bound. The Lumen surface cache needs that
 * question answered at every card texel it lights.
 *
 * The attenuation comes from light_attenuation.sh, the same code the per-light direct shaders
 * run. If the two drift, indirect light stops agreeing with the direct light it is supposed to
 * be a bounce of, and the mismatch reads as a lighting bug with no obvious source.
 *
 * RESERVED RESOURCE STAGE 5.
 */

#include "../bgfx_compute.sh"
#include "../light_attenuation.sh"

/// vec4 elements per light. Mirror of gpu_light_buffer::light_vec4_stride.
#define GPU_LIGHT_STRIDE 5

#define GPU_LIGHT_TYPE_SPOT        0
#define GPU_LIGHT_TYPE_POINT       1
#define GPU_LIGHT_TYPE_DIRECTIONAL 2

BUFFER_RO(b_gpu_lights, vec4, 5);

/// x = light count. yzw reserved.
uniform vec4 u_gpu_light_params;
#define u_gpu_light_count int(u_gpu_light_params.x)

struct GpuLight
{
	vec3 position;
	int type;
	vec3 direction;
	float range;
	vec3 color;
	float intensity;
	float cos_inner;
	float cos_outer;
	/// The emitting sphere's radius (light::source_radius).
	float source_radius;
	/// The light casts shadows (light::casts_shadows): an unshadowed light reaches every point in its range.
	bool casts_shadows;
	/// The emitting tube's axis scaled by its length (light::source_length; zero for a sphere).
	vec3 source_axis;
};

GpuLight GpuLoadLight(int index)
{
	uint base = uint(index) * uint(GPU_LIGHT_STRIDE);
	vec4 l0 = b_gpu_lights[base + 0u];
	vec4 l1 = b_gpu_lights[base + 1u];
	vec4 l2 = b_gpu_lights[base + 2u];
	vec4 l3 = b_gpu_lights[base + 3u];
	vec4 l4 = b_gpu_lights[base + 4u];
	GpuLight light;
	light.position = l0.xyz;
	light.type = int(l0.w);
	light.direction = l1.xyz;
	light.range = l1.w;
	light.color = l2.xyz;
	light.intensity = l2.w;
	light.cos_inner = l3.x;
	light.cos_outer = l3.y;
	light.source_radius = l3.z;
	light.casts_shadows = l3.w > 0.0;
	light.source_axis = l4.xyz;
	return light;
}

/// LocalLightSpotMask's SpotAngles for @p light: (cos outer, 1 / (cos inner - cos outer)).
vec2 GpuSpotAngles(GpuLight light)
{
	return vec2(light.cos_outer, 1.0 / max(light.cos_inner - light.cos_outer, 1e-4));
}

#endif // __GI_GPU_LIGHTS_SH__
