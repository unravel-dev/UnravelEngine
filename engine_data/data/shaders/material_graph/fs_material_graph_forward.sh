#ifndef FS_MATERIAL_GRAPH_FORWARD_SH_HEADER_GUARD
#define FS_MATERIAL_GRAPH_FORWARD_SH_HEADER_GUARD

/*
 * main() of every material graph's translucency-pass shader: surfaces of the Blend Modes Translucent, Premultiplied,
 * Additive and Modulate, drawn after the lighting over the lit scene, sorted far to near with the particles. The
 * surface from the graph's evaluate_material, lit as mg_forward.sh says, fogged, and written as the blend takes it:
 *   MATERIAL_BLEND_TRANSLUCENT    rgb = (diffuse light + emissive) x Opacity + specular light, alpha = Opacity
 *                                 (MATERIAL_PRESERVE_SPECULAR; without it the specular light is scaled by Opacity too);
 *                                 blended ONE, INV_SRC_ALPHA
 *   MATERIAL_BLEND_PREMULTIPLIED  rgb = the light and emissive as they are, alpha = Opacity; ONE, INV_SRC_ALPHA
 *   MATERIAL_BLEND_ADDITIVE       rgb = the light and emissive x Opacity; ONE, ONE
 *   MATERIAL_BLEND_MODULATE       rgb = Emissive faded to white by Opacity (unlit); DST_COLOR, ZERO
 * The second target is the TAA reactive mask (MAX blend): Opacity x the surface's TAA Response. A refracting surface
 * (MATERIAL_REFRACTION) adds the opaque scene behind it, bent, for the share its Opacity leaves, and covers the pixel.
 * The order-independent variant (FORWARD_OIT) writes weighed sums instead, composited by translucency/fs_oit_composite.
 * Camera Fade Distance fades the Opacity; LOD transitions keep the G-buffer's dither.
 */

#include "mg_forward.sh"

// per frame
uniform vec4 u_camera_clip_planes; //.x = near, .y = far, .z = material texture mip bias

#ifndef MATERIAL_TAA_RESPONSE
#define MATERIAL_TAA_RESPONSE 0.5
#endif
/// The most light (pre-exposed) one order-independent layer adds: with the largest weight, the sums of many layers stay
/// within half-float range.
#define FORWARD_OIT_MAX_LIGHT 64.0

void main()
{
	vec3 view_direction = u_camera_wpos.xyz - v_wpos;
	vec3 vertex_normal = mg_facing_vertex_normal(v_wpos, v_wnormal, view_direction);

	MaterialInputs i;
	i.uv0 = v_texcoord0;
	i.world_position = v_wpos;
	i.vertex_normal = normalize(vertex_normal);
	i.tangent = normalize(v_wtangent);
	i.bitangent = normalize(v_wbitangent);
	i.two_sided_sign = mg_two_sided_sign(v_wpos, v_wnormal, view_direction);
	i.view_direction = normalize(view_direction);
	i.camera_position = u_camera_wpos.xyz;
	i.camera_direction = mg_camera_direction();
	i.screen_uv = gl_FragCoord.xy * u_viewTexel.xy;
	i.time = u_material_time.x;
	i.mip_bias = u_camera_clip_planes.z;
	mg_set_domain_input_defaults(i);
#if MATERIAL_OBJECT_DATA
	mg_set_object_inputs_packed(i, v_object0, v_object1, v_object2);
#else
	mg_set_world_object_inputs(i);
#endif
#if MATERIAL_INTERPOLATORS
	i.interpolator0 = vec4_splat(0.0);
	i.interpolator1 = vec4_splat(0.0);
	i.interpolator2 = vec4_splat(0.0);
	i.interpolator3 = vec4_splat(0.0);
#endif
#if MATERIAL_INTERPOLATORS > 0
	i.interpolator0 = v_interpolator0;
#endif
#if MATERIAL_INTERPOLATORS > 1
	i.interpolator1 = v_interpolator1;
#endif
#if MATERIAL_INTERPOLATORS > 2
	i.interpolator2 = v_interpolator2;
#endif
#if MATERIAL_INTERPOLATORS > 3
	i.interpolator3 = v_interpolator3;
#endif
#ifdef MATERIAL_GRAPH_INSTANCE_MATERIAL
	i.instance_register0 = v_material0;
	i.instance_register1 = v_material1;
	i.instance_register2 = v_material2;
	i.instance_register3 = v_material3;
#endif

	MaterialSurface s = mg_default_surface();
	evaluate_material(i, s);

	// The LOD transition dithers as in the G-buffer (v_lod_params.x positive while the current LOD fades out, negative
	// while the target LOD fades in): two blended LODs over each other would show both.
	float dither = ign16x16(gl_FragCoord.xy);
	float lod_param = v_lod_params.x;
	float abs_param = abs(lod_param);
	float is_positive = step(0.0, lod_param);
	float threshold = mix(1.0 - abs_param, abs_param, is_positive);
	if((dither - threshold) * sign(lod_param) > 0.0)
	{
		discard;
	}
	// saturate(distance / threshold): within Camera Fade Distance the surface fades out, and it goes closer than twice
	// the near plane.
	float distance = length(view_direction) - u_camera_clip_planes.x * 2.0;
	float distance_factor = s.camera_fade_distance > 0.0 ? saturate(distance / s.camera_fade_distance) : step(0.0, distance);
	float opacity = saturate(s.opacity_mask) * distance_factor;

	vec4 gpu_pre_exposure = u_pre_exposure_gpu;
	float pre_exposure = pre_exposure_from_gpu(gpu_pre_exposure);
	vec3 emissive = max(s.emissive, vec3_splat(0.0)) * pre_exposure;
	mat3 tangent_to_world_space = constructTangentToWorldSpaceMatrix(normalize(v_wtangent), normalize(v_wbitangent), normalize(vertex_normal));
	vec3 N = normalize(mul(tangent_to_world_space, normalize(s.normal)).xyz);
#if MATERIAL_UNLIT
	vec3 diffuse_light = vec3_splat(0.0);
	vec3 specular_light = vec3_splat(0.0);
#else
#if MATERIAL_SPECULAR_WORKFLOW
	mg_specular_to_metallic(s.base_color, s.specular, s.glossiness, s.base_color, s.metallic, s.roughness);
#endif
	vec3 base_color = saturate(s.base_color);
	float metallic = saturate(s.metallic);
	float roughness = GeometricSpecularAA(N, saturate(s.roughness));
	ForwardLight light = forward_light(v_wpos,
	                                   N,
	                                   normalize(vertex_normal),
	                                   i.view_direction,
	                                   ComputeDiffuseColor(base_color, metallic),
	                                   ComputeF0(0.5, base_color, metallic),
	                                   roughness,
	                                   saturate(s.ambient_occlusion),
	                                   gpu_pre_exposure.x);
	vec3 diffuse_light = light.diffuse;
	vec3 specular_light = light.specular;
#endif

	vec4 color;
#if MATERIAL_BLEND_MODULATE
	color = vec4(mix(vec3_splat(1.0), max(s.emissive, vec3_splat(0.0)), opacity), 1.0);
#elif MATERIAL_BLEND_PREMULTIPLIED
	color = vec4(diffuse_light + specular_light + emissive, opacity);
#elif MATERIAL_BLEND_ADDITIVE
	color = vec4((diffuse_light + specular_light + emissive) * opacity, 0.0);
#elif MATERIAL_PRESERVE_SPECULAR
	color = vec4((diffuse_light + emissive) * opacity + specular_light, opacity);
#else
	color = vec4((diffuse_light + specular_light + emissive) * opacity, opacity);
#endif
	color = forward_apply_fog(color, i.screen_uv, -view_direction, pre_exposure);
#if MATERIAL_REFRACTION && !FORWARD_OIT
	// The scene behind, bent, replaces what the blend would show there: the surface covers the pixel whole. The scene
	// read is fogged already; other see-through surfaces and particles behind are not in it.
	if(u_fwd_scene.x > 0.5)
	{
#if MATERIAL_ROUGH_REFRACTION
		float refraction_roughness = roughness;
#else
		float refraction_roughness = 0.0;
#endif
		color.rgb += forward_refracted_scene(v_wpos, N, i.view_direction, s.refraction, i.screen_uv, refraction_roughness) *
		             (1.0 - color.a);
		color.a = 1.0;
	}
#endif

#if FORWARD_OIT
	// Weighted blended order-independent transparency: the premultiplied light times the weight into the first target
	// (ONE, ONE), the coverage into its alpha as the product of what each layer lets through (ZERO, INV_SRC_ALPHA),
	// and the coverage times the weight into the second target (ONE, ONE); the composite divides the first by the
	// second, which gives one layer back its own colour. The weight is the coverage times a falloff with the distance
	// (the near layers of a pixel count most) within a range, and the light is capped, so the sums of half floats do
	// not overflow.
	float layer_distance = length(view_direction);
	float coverage = max(color.a, 1e-3);
	float weight = clamp(10.0 / (1e-5 + pow(layer_distance / 5.0, 2.0) + pow(layer_distance / 200.0, 6.0)), 1e-2, 1e2) * coverage;
	gl_FragData[0] = vec4(min(color.rgb, vec3_splat(FORWARD_OIT_MAX_LIGHT)) * weight, color.a);
	gl_FragData[1] = vec4(coverage * weight, 0.0, 0.0, 0.0);
#else
	gl_FragData[0] = color;
	gl_FragData[1] = vec4(saturate(opacity * MATERIAL_TAA_RESPONSE), 0.0, 0.0, 0.0);
#endif
}

#endif // FS_MATERIAL_GRAPH_FORWARD_SH_HEADER_GUARD
