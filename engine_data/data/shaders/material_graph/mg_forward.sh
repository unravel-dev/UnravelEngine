#ifndef MG_FORWARD_SH_HEADER_GUARD
#define MG_FORWARD_SH_HEADER_GUARD

/*
 * What the translucency pass's surfaces read besides their graph (fs_material_graph_forward.sh), bound by the renderer
 * before each of their draws (deferred::draw_translucent_model): the sky's irradiance SH, whose texel 9 holds the GPU's
 * share of the pre-exposure, the volumetric fog's froxel column, and for lit surfaces the sun with its shadow cascades,
 * up to FORWARD_MAX_LIGHTS nearby point / spot lights with their maps in the shadow atlas, and the reflection probe of
 * the draw. These sit on the top
 * stages; the graph's own textures take the stages below them (get_material_graph_max_textures).
 *
 * Lighting follows the deferred pass's: the same BRDF (StandardShadingDirect, EnvBRDFApprox), lights pre-exposed by the
 * CPU's share on the CPU and by the GPU's share here, the SH absolute. What only exists for the opaque surface behind
 * the pixel (the GI, SSR, screen-space AO, contact shadows) does not light these surfaces.
 */

#define FORWARD_IRRADIANCE_STAGE 15
#if MATERIAL_UNLIT
#define FORWARD_FOG_VOLUME_STAGE 14
#else
#define FORWARD_PROBE_STAGE 14
#define FORWARD_SHADOW_STAGE0 10
#define FORWARD_SHADOW_STAGE1 11
#define FORWARD_SHADOW_STAGE2 12
#define FORWARD_SHADOW_STAGE3 13
#define FORWARD_FOG_VOLUME_STAGE 9
#define FORWARD_LIGHT_SHADOW_STAGE 8
#endif

SAMPLER2D(s_fwd_irradiance, FORWARD_IRRADIANCE_STAGE);
#define PRE_EXPOSURE_ENVIRONMENT s_fwd_irradiance
#include "../pre_exposure.sh"
SAMPLER3D(s_fwd_fog_volume, FORWARD_FOG_VOLUME_STAGE);
#define FOG_VOLUME_SAMPLER s_fwd_fog_volume
#include "../fog/fog.sh"

#if (MG_READS_SCENE_COLOR || MG_READS_SCENE_DEPTH) && SHADOW_CASTER && BGFX_SHADER_TYPE_FRAGMENT
// The shadow pass binds no scene: a caster reads the scene as far away and black, so Depth Fade passes the Opacity.
#if MG_READS_SCENE_COLOR
vec4 mg_scene_texture_scene_color(vec2 uv)
{
	return vec4(0.0, 0.0, 0.0, 1.0);
}
#endif
#if MG_READS_SCENE_DEPTH
vec4 mg_scene_texture_scene_depth(vec2 uv)
{
	return vec4_splat(1.0e6);
}
#endif
#elif MG_READS_SCENE_COLOR || MG_READS_SCENE_DEPTH
// The opaque scene behind the surface (Scene Texture Scene Color and Scene Depth, Depth Fade, Refraction): the first
// read at the stage below the engine's, the second at the next free one (get_forward_scene_texture_stages).
#if MATERIAL_UNLIT
#define FORWARD_SCENE_FIRST_STAGE 13
#define FORWARD_SCENE_SECOND_STAGE 12
#else
#define FORWARD_SCENE_FIRST_STAGE 6
#define FORWARD_SCENE_SECOND_STAGE 5
// The blurred copy of a rough Refraction (lit surfaces only: an unlit one has no roughness).
#define FORWARD_SCENE_BLURRED_STAGE 4
#endif
/// x: 1 when s_fwd_scene_color holds the copy of the lit scene, 0 when the view took none (it reads black then).
uniform vec4 u_fwd_scene;
#if MG_READS_SCENE_COLOR
SAMPLER2D(s_fwd_scene_color, FORWARD_SCENE_FIRST_STAGE);
#if MG_READS_SCENE_DEPTH
SAMPLER2D(s_fwd_scene_depth, FORWARD_SCENE_SECOND_STAGE);
#endif
#else
SAMPLER2D(s_fwd_scene_depth, FORWARD_SCENE_FIRST_STAGE);
#endif
#if MATERIAL_ROUGH_REFRACTION
/// The lit scene at a quarter of the view's size, blurred (pipeline::prepare_forward_scene_reads).
SAMPLER2D(s_fwd_scene_blurred, FORWARD_SCENE_BLURRED_STAGE);
#endif

#if BGFX_SHADER_TYPE_FRAGMENT
// A read at @p uv kept half a texel inside the view: no read lands on its edge.
vec2 mg_forward_clamp_uv(vec2 uv)
{
	vec2 half_texel = u_viewTexel.xy * 0.5;
	return clamp(uv, half_texel, vec2_splat(1.0) - half_texel);
}

#if MG_READS_SCENE_COLOR
// The light of the opaque scene at @p uv, as the scene has it (the pre-exposure taken out).
vec4 mg_scene_texture_scene_color(vec2 uv)
{
	vec3 stored = texture2DLod(s_fwd_scene_color, mg_forward_clamp_uv(uv), 0.0).rgb;
	return vec4(stored / max(u_pre_exposure_value, 0.000001), 1.0);
}
#endif

#if MG_READS_SCENE_DEPTH
// How far the opaque scene at @p uv lies from the camera along its direction, in world units.
vec4 mg_scene_texture_scene_depth(vec2 uv)
{
	float device_depth = texture2DLod(s_fwd_scene_depth, mg_forward_clamp_uv(uv), 0.0).x;
	return vec4_splat(abs(screenSpaceToViewSpaceDepth(device_depth)));
}
#endif
#endif // BGFX_SHADER_TYPE_FRAGMENT
#endif // MG_READS_SCENE_COLOR || MG_READS_SCENE_DEPTH

#if MATERIAL_REFRACTION && BGFX_SHADER_TYPE_FRAGMENT && !SHADOW_CASTER
/// The farthest, in world units, the opaque scene behind a refracting surface bends its view as if it lay.
#define FORWARD_REFRACTION_MAX_GAP 1.0

/// @p clip as a position on the view, 0 to 1 as gl_FragCoord runs (from the bottom on OpenGL, the top elsewhere).
vec2 forward_clip_to_view_uv(vec4 clip)
{
	vec2 ndc = clip.xy / max(clip.w, 1e-6);
#if BGFX_SHADER_LANGUAGE_GLSL
	return ndc * 0.5 + 0.5;
#else
	return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
#endif
}

#if MATERIAL_ROUGH_REFRACTION
/// Roughness from which, and to which, a refracting surface goes from the sharp scene to the blurred one.
#define FORWARD_ROUGH_REFRACTION_START 0.03
#define FORWARD_ROUGH_REFRACTION_FULL 0.3

/// The blurred scene at @p uv, pre-exposed: four reads of the quarter-size copy, spread wider the rougher the surface.
vec3 forward_blurred_scene(vec2 uv, float roughness)
{
	vec2 spread = u_viewTexel.xy * (2.0 + 10.0 * saturate(roughness));
	vec3 sum = texture2DLod(s_fwd_scene_blurred, mg_forward_clamp_uv(uv + vec2(-spread.x, -spread.y)), 0.0).rgb;
	sum += texture2DLod(s_fwd_scene_blurred, mg_forward_clamp_uv(uv + vec2(spread.x, -spread.y)), 0.0).rgb;
	sum += texture2DLod(s_fwd_scene_blurred, mg_forward_clamp_uv(uv + vec2(-spread.x, spread.y)), 0.0).rgb;
	sum += texture2DLod(s_fwd_scene_blurred, mg_forward_clamp_uv(uv + vec2(spread.x, spread.y)), 0.0).rgb;
	return sum * 0.25;
}
#endif

/**
 * The opaque scene seen through a surface of index of refraction @p ior at @p position (the view @p V and the normal
 * @p N in world space, @p uv its pixel), pre-exposed: the view ray bent by the surface, carried to the depth of the
 * scene behind (at most FORWARD_REFRACTION_MAX_GAP further), and read where that lands. A read that lands on something
 * in front of the surface takes the unbent pixel, so nothing nearer smears into it. A rough surface
 * (MATERIAL_ROUGH_REFRACTION) blurs it by its @p roughness.
 */
vec3 forward_refracted_scene(vec3 position, vec3 N, vec3 V, float ior, vec2 uv, float roughness)
{
	float pixel_depth = dot(position - u_camera_wpos.xyz, mg_camera_direction());
	float gap = clamp(mg_scene_texture_scene_depth(uv).x - pixel_depth, 0.0, FORWARD_REFRACTION_MAX_GAP);
	vec3 bent = refract(-V, N, 1.0 / max(ior, 1.0));
	vec2 shift = forward_clip_to_view_uv(mul(u_viewProj, vec4(position + bent * gap, 1.0))) -
	             forward_clip_to_view_uv(mul(u_viewProj, vec4(position, 1.0)));
	vec2 bent_uv = mg_forward_clamp_uv(uv + shift);
	if(mg_scene_texture_scene_depth(bent_uv).x < pixel_depth)
	{
		bent_uv = uv;
	}
	vec3 scene = texture2DLod(s_fwd_scene_color, bent_uv, 0.0).rgb;
#if MATERIAL_ROUGH_REFRACTION
	float blur = smoothstep(FORWARD_ROUGH_REFRACTION_START, FORWARD_ROUGH_REFRACTION_FULL, roughness);
	scene = mix(scene, forward_blurred_scene(bent_uv, roughness), blur);
#endif
	return scene;
}
#endif // MATERIAL_REFRACTION

/// Metres a receiver moves along its normal, and toward the sun, before its shadow test.
#define FORWARD_SHADOW_NORMAL_OFFSET 0.05
#define FORWARD_SHADOW_DEPTH_BIAS 0.02

#if !MATERIAL_UNLIT
#include "../reflection_probe/sphere_probe.sh"
#include "../reflection_probe/box_probe.sh"

SAMPLERCUBE(s_fwd_probe, FORWARD_PROBE_STAGE);
// The sun's shadow cascades and their world-to-shadow matrices (shadowmap_generator::submit_uniforms).
SAMPLER2D(s_shadowMap0, FORWARD_SHADOW_STAGE0);
SAMPLER2D(s_shadowMap1, FORWARD_SHADOW_STAGE1);
SAMPLER2D(s_shadowMap2, FORWARD_SHADOW_STAGE2);
SAMPLER2D(s_shadowMap3, FORWARD_SHADOW_STAGE3);
uniform mat4 u_shadowMapMtx0;
uniform mat4 u_shadowMapMtx1;
uniform mat4 u_shadowMapMtx2;
uniform mat4 u_shadowMapMtx3;
/// z: one texel of the shadow map, in uv.
uniform vec4 u_params2;
/// y: stored shadow depth per world unit.
uniform vec4 u_shadowBiasParams;

#define FORWARD_MAX_LIGHTS 4
/// xyz: the direction the sun's light travels; w: its shadow cascades (0: no shadow read).
uniform vec4 u_fwd_sun_direction;
/// rgb: the sun's irradiance (linear, lux, times the CPU's share of the pre-exposure); w: the stored depth offset of
/// linear shadow maps.
uniform vec4 u_fwd_sun_color;
/// x: local lights; y: the reflection probe (0 none, 1 sphere, 2 box).
uniform vec4 u_fwd_lighting;
/// Per light: (position, 1 / range), (axis, cos outer half angle), (linear color x candela x the CPU's pre-exposure,
/// 1 / (cos inner - cos outer half angle)). A point light has cos outer -2, so its cone mask is 1.
uniform vec4 u_fwd_lights[12];
/// The probe: (position, influence radius), (mips, intensity x the CPU's pre-exposure, 1 when its mips are
/// prefiltered, 0), (a box's half extents, transition distance).
uniform vec4 u_fwd_probe[3];
/// A box probe's world to its -1..1 space.
uniform mat4 u_fwd_probe_inv_world;
/// The vec4s of one light's record in u_fwd_light_shadows.
#define FORWARD_LIGHT_SHADOW_VEC4S 18
/// Per light of u_fwd_lights, in their order, the record fog_pass::pack_light_shadow writes: (the map's rect in the
/// shadow atlas, uv), (0 unshadowed, 1 spot, 2 point; 1 for a linear-depth map; half a texel of the map in its own uv;
/// 0), then the world -> map matrix of each map by columns (one for a spot light, four for a point light).
uniform vec4 u_fwd_light_shadows[72];
/// The point and spot lights' maps (shadow::shadow_atlas).
SAMPLER2D(s_fwd_light_shadow_atlas, FORWARD_LIGHT_SHADOW_STAGE);

/// The sun's visibility through one cascade at @p position, 4 taps; -1 when the cascade does not cover it.
float forward_cascade_visibility(sampler2D shadow_map, mat4 shadow_matrix, vec3 position)
{
	vec4 coord = mul(shadow_matrix, vec4(position, 1.0));
	vec2 uv = coord.xy / coord.w;
	float depth = (coord.z + u_fwd_sun_color.w) / coord.w;
	if(any(lessThan(uv, vec2_splat(0.01))) || any(greaterThan(uv, vec2_splat(0.99))) || depth <= 0.0 || depth >= 1.0)
	{
		return -1.0;
	}
	float receiver = depth - FORWARD_SHADOW_DEPTH_BIAS * u_shadowBiasParams.y;
	float texel = u_params2.z;
	float lit = step(receiver, texture2DLod(shadow_map, uv + vec2(-0.5, -0.5) * texel, 0.0).x);
	lit += step(receiver, texture2DLod(shadow_map, uv + vec2(0.5, -0.5) * texel, 0.0).x);
	lit += step(receiver, texture2DLod(shadow_map, uv + vec2(-0.5, 0.5) * texel, 0.0).x);
	lit += step(receiver, texture2DLod(shadow_map, uv + vec2(0.5, 0.5) * texel, 0.0).x);
	return lit * 0.25;
}

/// The sun's visibility at @p position: the nearest cascade that covers it (lit outside them all).
float forward_sun_visibility(vec3 position)
{
	float cascades = u_fwd_sun_direction.w;
	float visibility = -1.0;
	if(cascades > 0.5)
	{
		visibility = forward_cascade_visibility(s_shadowMap0, u_shadowMapMtx0, position);
	}
	if(visibility < 0.0 && cascades > 1.5)
	{
		visibility = forward_cascade_visibility(s_shadowMap1, u_shadowMapMtx1, position);
	}
	if(visibility < 0.0 && cascades > 2.5)
	{
		visibility = forward_cascade_visibility(s_shadowMap2, u_shadowMapMtx2, position);
	}
	if(visibility < 0.0 && cascades > 3.5)
	{
		visibility = forward_cascade_visibility(s_shadowMap3, u_shadowMapMtx3, position);
	}
	return visibility < 0.0 ? 1.0 : visibility;
}

/// The face of a point light's tetrahedron map that the light-relative @p offset falls in: 0 green, 1 yellow, 2 blue,
/// 3 red, picked as fs_pbr_lighting.sh picks it.
int forward_point_shadow_face(vec3 offset)
{
	vec4 selection = vec4(dot(vec3(0.0, -0.57735026, 0.81649661), offset),
	                      dot(vec3(0.0, -0.57735026, -0.81649661), offset),
	                      dot(vec3(-0.81649661, 0.57735026, 0.0), offset),
	                      dot(vec3(0.81649661, 0.57735026, 0.0), offset));
	float best = max(max(selection.x, selection.y), max(selection.z, selection.w));
	if(selection.y == best)
	{
		return 1;
	}
	if(selection.z == best)
	{
		return 2;
	}
	if(selection.w == best)
	{
		return 3;
	}
	return 0;
}

/**
 * Local light @p light's visibility at @p position, @p light_offset from the light: 4 taps of its map in the atlas, 1
 * for a light without one or outside its map.
 */
float forward_light_visibility(int light, vec3 position, vec3 light_offset)
{
	int record = light * FORWARD_LIGHT_SHADOW_VEC4S;
	vec4 params = u_fwd_light_shadows[record + 1];
	if(params.x < 0.5)
	{
		return 1.0;
	}
	int map = params.x > 1.5 ? forward_point_shadow_face(light_offset) : 0;
	int column = record + 2 + map * 4;
	vec4 coord = u_fwd_light_shadows[column] * position.x + u_fwd_light_shadows[column + 1] * position.y +
	             u_fwd_light_shadows[column + 2] * position.z + u_fwd_light_shadows[column + 3];
	if(coord.w <= 0.0)
	{
		return 1.0;
	}
	vec2 uv = coord.xy / coord.w;
	if(any(lessThan(uv, vec2_splat(0.0))) || any(greaterThan(uv, vec2_splat(1.0))))
	{
		return 1.0;
	}
	float receiver = params.y > 0.5 ? coord.z + 0.5 : coord.z / coord.w;
	vec4 rect = u_fwd_light_shadows[record];
	float half_texel = params.z;
	vec2 low = vec2_splat(half_texel);
	vec2 high = vec2_splat(1.0 - half_texel);
	float lit = step(receiver, texture2DLod(s_fwd_light_shadow_atlas, rect.xy + clamp(uv + vec2(-half_texel, -half_texel), low, high) * rect.zw, 0.0).x);
	lit += step(receiver, texture2DLod(s_fwd_light_shadow_atlas, rect.xy + clamp(uv + vec2(half_texel, -half_texel), low, high) * rect.zw, 0.0).x);
	lit += step(receiver, texture2DLod(s_fwd_light_shadow_atlas, rect.xy + clamp(uv + vec2(-half_texel, half_texel), low, high) * rect.zw, 0.0).x);
	lit += step(receiver, texture2DLod(s_fwd_light_shadow_atlas, rect.xy + clamp(uv + vec2(half_texel, half_texel), low, high) * rect.zw, 0.0).x);
	return lit * 0.25;
}

/// The draw's reflection probe at a receiver: its capture along @p R in rgb (pre-exposed by the CPU's share), its
/// coverage in alpha; zero without a probe or beyond it.
vec4 forward_probe_sample(vec3 position, vec3 R, float roughness)
{
	if(u_fwd_lighting.y < 0.5)
	{
		return vec4_splat(0.0);
	}
	if(u_fwd_lighting.y < 1.5)
	{
		return SampleSphereProbe(s_fwd_probe, position, R, roughness, u_fwd_probe[0], u_fwd_probe[1].x, u_fwd_probe[1].y, u_fwd_probe[1].z);
	}
	return SampleBoxProbe(s_fwd_probe,
	                      position,
	                      R,
	                      roughness,
	                      u_fwd_probe[0],
	                      u_fwd_probe_inv_world,
	                      u_fwd_probe[2],
	                      u_fwd_probe[1].x,
	                      u_fwd_probe[1].y,
	                      u_fwd_probe[1].z);
}

/// The light a lit surface reflects, in two parts: what its diffuse color reflects and what its specular color does.
struct ForwardLight
{
	vec3 diffuse;
	vec3 specular;
};

/**
 * The direct and indirect light a default-lit surface reflects toward the viewer at @p position, pre-exposed. @p N is
 * the shading normal and @p vertex_normal the geometric one (the shadow test moves along it); @p gpu_pre_exposure is
 * the GPU's share of the pre-exposure.
 */
ForwardLight forward_light(vec3 position,
                           vec3 N,
                           vec3 vertex_normal,
                           vec3 V,
                           vec3 diffuse_color,
                           vec3 specular_color,
                           float roughness,
                           float ao,
                           float gpu_pre_exposure)
{
	ForwardLight result;
	result.diffuse = vec3_splat(0.0);
	result.specular = vec3_splat(0.0);
	float lighting_roughness = GetLightingRoughness(roughness);

	// The sun.
	vec3 toward_sun = -u_fwd_sun_direction.xyz;
	AreaLight sun = MakeDirectionalAreaLight(N, toward_sun);
	BRANCH
	if(sun.NoL > 0.0 && dot(u_fwd_sun_color.rgb, u_fwd_sun_color.rgb) > 0.0)
	{
		vec3 shadow_position = position + vertex_normal * FORWARD_SHADOW_NORMAL_OFFSET + toward_sun * FORWARD_SHADOW_NORMAL_OFFSET;
		vec3 radiance = u_fwd_sun_color.rgb * (sun.NoL * forward_sun_visibility(shadow_position));
		result.diffuse += radiance * StandardShadingDirect(diffuse_color, vec3_splat(0.0), lighting_roughness, sun, V, N, ao);
		result.specular += radiance * StandardShadingDirect(vec3_splat(0.0), specular_color, lighting_roughness, sun, V, N, ao);
	}

	// The draw's nearby lights.
	for(int i = 0; i < FORWARD_MAX_LIGHTS; ++i)
	{
		if(float(i) >= u_fwd_lighting.x)
		{
			break;
		}
		vec4 placement = u_fwd_lights[i * 3];
		vec4 cone = u_fwd_lights[i * 3 + 1];
		vec4 intensity = u_fwd_lights[i * 3 + 2];
		vec3 to_light = placement.xyz - position;
		float distance_squared = dot(to_light, to_light);
		vec3 L = to_light * inversesqrt(max(distance_squared, 1e-8));
		float mask = LocalLightRangeMask(to_light, placement.w) * LocalLightSpotMask(L, cone.xyz, vec2(cone.w, intensity.w));
		AreaLight light = MakeDirectionalAreaLight(N, L);
		BRANCH
		if(mask > 0.0 && light.NoL > 0.0)
		{
			vec3 shadow_position = position + vertex_normal * FORWARD_SHADOW_NORMAL_OFFSET + L * FORWARD_SHADOW_NORMAL_OFFSET;
			mask *= forward_light_visibility(i, shadow_position, shadow_position - placement.xyz);
			vec3 radiance = intensity.rgb * (light.NoL * mask / (distance_squared + LOCAL_LIGHT_DISTANCE_BIAS_SQR));
			result.diffuse += radiance * StandardShadingDirect(diffuse_color, vec3_splat(0.0), lighting_roughness, light, V, N, ao);
			result.specular += radiance * StandardShadingDirect(vec3_splat(0.0), specular_color, lighting_roughness, light, V, N, ao);
		}
	}
	result.diffuse *= gpu_pre_exposure;
	result.specular *= gpu_pre_exposure;

	// Indirect: the sky's SH for the diffuse light, as the deferred pass lights a pixel no GI covers; the probe where it
	// covers the surface, the SH's lobe elsewhere, for the reflections.
	float NoV = max(dot(N, V), 1e-5);
	vec3 R = reflect(-V, N);
	vec4 probe = forward_probe_sample(position, R, roughness);
	float pre_exposure = u_pre_exposure.x * gpu_pre_exposure;
	vec3 indirect_diffuse = eval_irradiance_sh(s_fwd_irradiance, N) * (RECIP_PI * pre_exposure * ao);
	vec3 environment = eval_radiance_sh_lobe(s_fwd_irradiance, R, roughness) * pre_exposure;
	// The probe's rgb is not weighted by its coverage: premultiplied, as the probe layer holds it.
	vec3 indirect_specular = probe.rgb * (probe.w * gpu_pre_exposure) + environment * (1.0 - probe.w);
	float specular_occlusion = GetSpecularOcclusion(NoV, roughness * roughness, ao);
	result.diffuse += diffuse_color * indirect_diffuse;
	result.specular += indirect_specular * EnvBRDFApprox(specular_color, roughness, NoV) * specular_occlusion;
	return result;
}
#endif // !MATERIAL_UNLIT

/// @p color (rgb, alpha) fogged as its blend puts it over the scene, as particles fog themselves: an alpha blend's
/// light is dimmed by the fog's transmittance and its in-scatter covers by its alpha; added light is only dimmed; a
/// modulate's tint fades toward white.
vec4 forward_apply_fog(vec4 color, vec2 uv, vec3 view_offset, float pre_exposure)
{
	if(!fog_is_enabled())
	{
		return color;
	}
	float point_distance = length(view_offset);
	vec3 view_direction = view_offset / max(point_distance, 1e-6);
	vec4 fog = fog_to_point(s_fwd_irradiance, uv, view_direction, point_distance, false, pre_exposure);
#if MATERIAL_BLEND_MODULATE
	return vec4(mix(vec3_splat(1.0), color.rgb, fog.a), color.a);
#elif MATERIAL_BLEND_ADDITIVE
	return vec4(color.rgb * fog.a, color.a);
#else
	return vec4(color.rgb * fog.a + fog.rgb * color.a, color.a);
#endif
}

#endif // MG_FORWARD_SH_HEADER_GUARD
