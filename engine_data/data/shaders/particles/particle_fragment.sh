#ifndef PARTICLE_FRAGMENT_SH_HEADER_GUARD
#define PARTICLE_FRAGMENT_SH_HEADER_GUARD

// The lit programs' vertex shaders read the sky SH at stage 12 (particle_lighting.sh); every particle program
// reads the GPU's share of the pre-exposure from its texel 9 there (pre_exposure.sh PRE_EXPOSURE_ENVIRONMENT).
SAMPLER2D(s_particle_irradiance, 12);
#define PRE_EXPOSURE_ENVIRONMENT s_particle_irradiance
#include "../pre_exposure.sh"
#include "../shaderlib.sh"

// Stages above the vertex shaders' buffers (particle_gpu_schema.h).
SAMPLER2D(s_texColor, 13);
// The view's opaque depth (device depth in .x), when u_particleParams.x is 1.
SAMPLER2D(s_sceneDepth, 15);
/// x = 1 when s_sceneDepth holds the scene depth, y = blend mode (0 normal, 1 additive, 2 multiply,
/// 3 premultiplied), z = 1 for a mask texture (its luminance is the opacity), w = 1 when an emitter of the draw
/// blends its texture sheet frames.
uniform vec4 u_particleParams;

#define PARTICLE_BLEND_MULTIPLY 2.0
#define PARTICLE_BLEND_PREMULTIPLIED 3.0

bool particle_is_multiply()
{
	return abs(u_particleParams.y - PARTICLE_BLEND_MULTIPLY) < 0.5;
}

/// The texel at @p uv, crossfaded into the next texture sheet frame's texel at @p next_uv by @p blend
/// (0 without frame blending). The next frame is sampled only in draws that blend frames, with the first
/// sample's gradients (both frames cover the same texels): an implicit-gradient sample must not sit in flow
/// control.
vec4 particle_sample_texture(vec2 uv, vec2 next_uv, float blend)
{
	vec2 uv_dx = dFdx(uv);
	vec2 uv_dy = dFdy(uv);
	vec4 texel = texture2D(s_texColor, uv);
	if(u_particleParams.w > 0.5)
	{
		texel = mix(texel, texture2DGrad(s_texColor, next_uv, uv_dx, uv_dy), blend);
	}
	return texel;
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
 * What the blend state takes for a particle of color @p color (linear, not premultiplied) covering
 * @p coverage of the pixel. Normal and additive blend premultiplied scene light (ONE, INV_SRC_ALPHA /
 * ONE, ONE). Multiply (DST_COLOR, ZERO) takes a unitless filter that fades to white where the particle
 * does not cover, and is never scaled by the view's pre-exposure.
 */
vec4 particle_output(vec3 color, float coverage)
{
	if(particle_is_multiply())
	{
		return vec4(mix(vec3_splat(1.0), color, coverage), 1.0);
	}
	return vec4(color * coverage * u_pre_exposure_value, coverage);
}

/// Alpha erosion: texels below @p threshold vanish and the rest rescale to stay whole at 1.
float particle_erode(float alpha, float threshold)
{
	if(threshold <= 0.0)
	{
		return alpha;
	}
	return clamp((alpha - threshold) / max(1.0 - threshold, 1e-3), 0.0, 1.0);
}

bool particle_is_mask_texture()
{
	return u_particleParams.z > 0.5;
}

bool particle_is_premultiplied()
{
	return abs(u_particleParams.y - PARTICLE_BLEND_PREMULTIPLIED) < 0.5;
}

/// The second render target, blended with MAX: the share of the pixel the particle covers, scaled by its
/// emitter's TAA response. The TAA trusts its history less there: a long history smears what moves
/// without motion vectors.
vec4 particle_reactive(float coverage, float taa_response)
{
	return vec4(clamp(coverage * taa_response, 0.0, 1.0), 0.0, 0.0, 0.0);
}

/**
 * A particle with a multi-channel texture: what the color target and the reactive target take. @p light
 * multiplies the particle's color (1 for unlit particles: their color is the light they give off).
 */
void particle_shade_multi_channel(vec4 color,
                                  vec3 texcoord0,
                                  vec2 texcoord1,
                                  vec4 particle,
                                  vec4 frag_coord,
                                  vec3 light,
                                  out vec4 out_color,
                                  out vec4 out_reactive)
{
	vec4 texel = particle_sample_texture(texcoord0.xy, texcoord1.xy, particle.x);
	float alpha = particle_erode(texel.a, particle.w);
	float fade = color.a * particle_soft_fade(frag_coord, particle.y, particle.z);
	out_reactive = particle_reactive(alpha * fade, texcoord0.z);
	if(particle_is_premultiplied())
	{
		// The texel's color carries its alpha already; erosion rescales both together.
		float erosion_scale = texel.a > 1e-4 ? alpha / texel.a : 0.0;
		out_color = vec4(texel.rgb * erosion_scale * color.rgb * light * fade * u_pre_exposure_value, alpha * fade);
		return;
	}
	out_color = particle_output(texel.rgb * color.rgb * light, alpha * fade);
}

/**
 * A particle with a mask texture: the texel's luminance is its opacity and its color tints the particle. A
 * grayscale mask at full particle opacity adds exactly its texel times the particle color; fading the particle
 * (color alpha, opacity, soft fade) fades that color with it.
 */
void particle_shade_mask(vec4 color,
                         vec3 texcoord0,
                         vec2 texcoord1,
                         vec4 particle,
                         vec4 frag_coord,
                         vec3 light,
                         out vec4 out_color,
                         out vec4 out_reactive)
{
	vec4 texel = particle_sample_texture(texcoord0.xy, texcoord1.xy, particle.x);
	float luminance = particle_erode(dot(texel.rgb, vec3(0.299, 0.587, 0.114)), particle.w);
	float fade = color.a * particle_soft_fade(frag_coord, particle.y, particle.z);
	out_reactive = particle_reactive(fade * luminance, texcoord0.z);
	if(particle_is_multiply())
	{
		// A multiply mask filters by the particle color where the mask is bright.
		out_color = particle_output(color.rgb, fade * luminance);
		return;
	}
	out_color = particle_output(texel.rgb * color.rgb * light, fade);
	// The color is already as bright as the mask; only the occlusion takes the luminance.
	out_color.a *= luminance;
}

// Lit particles: the normal map of surface lighting, read when u_particle_lighting.z is 1.
SAMPLER2D(s_particle_normal, 14);
uniform vec4 u_particle_lighting;

/// The light a lit particle's pixel receives: the per-particle part, plus the sun on the sphere the quad
/// stands for (surface lighting; volumetric particles leave the sun out of @p sun). With a normal map its
/// tangent-space normal at @p uv (crossfaded into @p next_uv by @p blend, like the color) stands in for the
/// sphere's: x along the quad's width, y up the quad (green up), z toward the camera.
vec3 particle_pixel_light(vec3 ambient, vec4 sun, vec3 sun_local, vec2 quad_uv, vec2 uv, vec2 next_uv, float blend)
{
	float radius_squared = min(dot(quad_uv, quad_uv), 1.0);
	vec3 normal = vec3(quad_uv, sqrt(1.0 - radius_squared));
	// Explicit gradients: the uniform branch below must not hold an implicit-gradient sample.
	vec2 uv_dx = dFdx(uv);
	vec2 uv_dy = dFdy(uv);
	if(u_particle_lighting.z > 0.5)
	{
		vec3 mapped = mix(texture2DGrad(s_particle_normal, uv, uv_dx, uv_dy).xyz,
		                  texture2DGrad(s_particle_normal, next_uv, uv_dx, uv_dy).xyz,
		                  blend) * 2.0 - 1.0;
		// A two-channel format (u_particle_lighting.w) stores x and y only.
		if(u_particle_lighting.w > 0.5)
		{
			mapped.z = sqrt(max(0.0, 1.0 - dot(mapped.xy, mapped.xy)));
		}
		normal = dot(mapped, mapped) > 1e-8 ? normalize(mapped) : vec3(0.0, 0.0, 1.0);
	}
	float wrap = sun.w;
	float diffuse = clamp((dot(normal, sun_local) + wrap) / (1.0 + wrap), 0.0, 1.0);
	// The vertex shader's light carries the CPU's share of the pre-exposure only.
	return (ambient + sun.rgb * diffuse) * u_pre_exposure_gpu_scale;
}

#endif // PARTICLE_FRAGMENT_SH_HEADER_GUARD
