#ifndef FS_MATERIAL_GRAPH_POST_SH_HEADER_GUARD
#define FS_MATERIAL_GRAPH_POST_SH_HEADER_GUARD

/*
 * main() of a post-process material: one pixel of the view at the material's Location, drawn over a full-screen
 * quad (vs_clip_quad.eshader) from the location's colour into a target of its own (post_material_pass.cpp).
 *
 * The inputs are the pixel's: its screen position (also the UV), the opaque surface's world position from the depth
 * (the far plane on the sky) and its G-buffer normal. The new colour is the graph's Color (unlinked: the scene's),
 * blended over the scene by Opacity times the entry's weight, and with Run Where Mask (POST_PROCESS_RUN_WHERE_MASK)
 * only where the custom mask holds that value.
 */

void main()
{
	vec2 uv = v_texcoord0;
	// The alpha passes through: the passes after this one read it as they would read the location's own colour.
	vec4 stored = texture2DLod(s_post_color, uv, 0.0);
	vec3 scene = mg_post_decode_color(stored.rgb);
	vec3 world_position = mg_post_world_position(uv);
	vec3 view_offset = u_camera_wpos.xyz - world_position;

	MaterialInputs i;
	i.uv0 = uv;
	i.uv1 = uv;
	i.color0 = vec4_splat(1.0);
	i.world_position = world_position;
	i.vertex_normal = mg_scene_texture_world_normal(uv).xyz;
	i.tangent = mul(u_invView, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
	i.bitangent = mul(u_invView, vec4(0.0, 1.0, 0.0, 0.0)).xyz;
	i.two_sided_sign = 1.0;
	i.view_direction = normalize(view_offset);
	i.camera_position = u_camera_wpos.xyz;
	i.camera_direction = mg_camera_direction();
	i.screen_uv = uv;
	i.time = u_material_time.x;
	i.mip_bias = 0.0;
	mg_set_domain_input_defaults(i);
#if !POST_PROCESS_DISPLAY
	i.exposure = mg_post_pre_exposure();
#endif
	mg_set_world_object_inputs(i);

	MaterialSurface s = mg_default_surface();
	s.color = scene;
	evaluate_material(i, s);

	float opacity = saturate(s.opacity * u_post_params.x);
#if POST_PROCESS_RUN_WHERE_MASK
	opacity *= step(abs(mg_post_custom_mask_at(uv) - float(POST_PROCESS_RUN_WHERE_MASK)), 0.5);
#endif
	gl_FragColor = vec4(mg_post_encode_color(mix(scene, s.color, opacity)), stored.a);
}

#endif // FS_MATERIAL_GRAPH_POST_SH_HEADER_GUARD
