#ifndef FS_MATERIAL_GRAPH_GBUFFER_SH_HEADER_GUARD
#define FS_MATERIAL_GRAPH_GBUFFER_SH_HEADER_GUARD

/*
 * main() of every material graph's G-buffer shader, and of fs_deferred_geom.eshader, which draws the default surface
 * while a graph's shader is not compiled: the surface from the graph's evaluate_material, the distance and LOD
 * dithering, the alpha clip, two-sided normals and the encoding.
 *
 * The generated shader defines, from its Surface Output node:
 *   MATERIAL_TWO_SIDED          back faces turn their normal toward the viewer
 *   MATERIAL_OPACITY_MASK       Opacity Mask clips pixels below Opacity Mask Clip, dithered like a masked material
 *   MATERIAL_DITHERED           the surface is see-through: dithered out by its Opacity as an alpha
 *   MATERIAL_UNLIT              the surface shows its Emissive alone: written as a black surface that reflects
 *                               nothing (Metallic 1 makes F0 0, which the shading's micro-occlusion term turns into no
 *                               specular; AO 0 takes the indirect light) and glows with its Emissive
 *   MATERIAL_SPECULAR_WORKFLOW  the surface is diffuse, specular and glossiness, converted to the G-buffer's base
 *                               color, metallic and roughness (mg_specular_to_metallic)
 * Opacity Mask Clip and Camera Fade Distance are the surface's.
 */

// per frame
uniform vec4 u_camera_clip_planes; //.x = near, .y = far, .z = material texture mip bias

// The clip a dithered surface's alpha dithers at: alpha 1 draws every pixel, alpha 0 none, and the pixels between
// spread over the dither pattern.
#define TRANSLUCENT_DITHER_CLIP 0.25

void main()
{
	// v_wpos is an offset from the view origin (view_origin.sh); the graph's nodes read absolute positions.
	vec3 view_direction = mg_camera_offset() - v_wpos;
	vec3 vertex_normal = mg_facing_vertex_normal(v_wpos, v_wnormal, view_direction);

	MaterialInputs i;
	i.uv0 = v_texcoord0;
	mg_set_vertex_attribute_inputs(i);
	i.world_position = viewOffsetToWorld(v_wpos);
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

	// Within Camera Fade Distance the surface dithers out as the camera comes closer, gone at twice the near plane, so
	// the near plane never cuts it, at the view's corners either. A distance of 0 leaves the surface to the near plane.
	float distance = length(view_direction) - u_camera_clip_planes.x * 2.0;
	float distance_factor = s.camera_fade_distance > 0.0 ? saturate(distance / s.camera_fade_distance) : 1.0;
	float dither = ign16x16(gl_FragCoord.xy);

	// LOD transition: v_lod_params.x positive while the current LOD fades out, negative while the target LOD fades in.
	float lod_param = v_lod_params.x;
	float abs_param = abs(lod_param);
	float is_positive = step(0.0, lod_param);
	float threshold = mix(1.0 - abs_param, abs_param, is_positive);
	bool lod_discard = (dither - threshold) * sign(lod_param) > 0.0;

	bool alpha_discard = false;
#if MATERIAL_DITHERED
	// Card captures too: the GI lets light through the surface, and its cards show what the view shows.
	alpha_discard = s.opacity_mask + 0.01 + (dither * (1.0 - TRANSLUCENT_DITHER_CLIP)) < 1.0;
#elif MATERIAL_OPACITY_MASK && !DEFERRED_GEOM_CARD_CAPTURE
	// A card capture keeps every texel of a masked material: its cards cover the whole surface the mesh shows them.
	alpha_discard = s.opacity_mask + 0.01 + (dither * (1.0 - s.opacity_mask_clip)) < 1.0;
#endif
	if(alpha_discard || (distance_factor + dither < 1.0) || lod_discard)
	{
		discard;
	}

#if MATERIAL_UNLIT
	s.base_color = vec3_splat(0.0);
	s.metallic = 1.0;
	s.roughness = 1.0;
	s.ambient_occlusion = 0.0;
	s.subsurface_color = vec3_splat(0.0);
	s.subsurface_opacity = 0.0;
	s.normal = vec3(0.0, 0.0, 1.0);
#elif MATERIAL_SPECULAR_WORKFLOW
	mg_specular_to_metallic(s.base_color, s.specular, s.glossiness, s.base_color, s.metallic, s.roughness);
#endif

	mat3 tangent_to_world_space = constructTangentToWorldSpaceMatrix(normalize(v_wtangent), normalize(v_wbitangent), normalize(vertex_normal));
	vec3 world_normal = normalize(mul(tangent_to_world_space, normalize(s.normal)).xyz);

	// Not named "buffer": that is a reserved word in GLSL 4.30, the OpenGL shader baseline.
	GBufferData gbuffer;
	gbuffer.base_color = s.base_color;
	gbuffer.ambient_occlusion = saturate(s.ambient_occlusion);
	gbuffer.world_normal = world_normal;
	gbuffer.roughness = saturate(s.roughness);
	gbuffer.emissive_color = max(s.emissive, vec3_splat(0.0));
	gbuffer.metallic = saturate(s.metallic);
	gbuffer.subsurface_color = saturate(s.subsurface_color);
	gbuffer.subsurface_opacity = saturate(s.subsurface_opacity);

	vec4 result[4];
	EncodeGBuffer(gbuffer, result);

	gl_FragData[0] = result[0];
	gl_FragData[1] = result[1];
	gl_FragData[2] = result[2];
	gl_FragData[3] = result[3];
}

#endif // FS_MATERIAL_GRAPH_GBUFFER_SH_HEADER_GUARD
