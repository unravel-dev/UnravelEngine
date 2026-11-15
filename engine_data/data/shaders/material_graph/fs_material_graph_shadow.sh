#ifndef FS_MATERIAL_GRAPH_SHADOW_SH_HEADER_GUARD
#define FS_MATERIAL_GRAPH_SHADOW_SH_HEADER_GUARD

/*
 * main() of the shadow caster variants of a material graph's shader (SHADOW_CASTER), which only a graph whose Opacity
 * Mask clips or whose World Position Offset moves its vertices has. The mask clips the shadow as it clips the surface,
 * with the hard test alone: a shadow map is not dithered.
 *
 * The vertex shader is vs_deferred_geom.sh's main(), the graph's own where its offset moves the vertices, so the depth
 * comes from the world position through the shadow view's view-projection, as fs_shadowmaps_packdepth.eshader computes
 * it from its own varyings:
 *   LINEAR    the light's linear depth (the projection's z row is divided by the far plane)
 *   PACK_VSM  the moments of a variance shadow map
 *
 * The camera position (u_camera_wpos) is the one the renderer set for the material's draws.
 */

void main()
{
#if MATERIAL_OPACITY_MASK
	vec3 view_direction = u_camera_wpos.xyz - v_wpos;
	vec3 vertex_normal = mg_facing_vertex_normal(v_wpos, v_wnormal, view_direction);

	MaterialInputs i;
	i.uv0 = v_texcoord0;
	i.world_position = v_wpos;
	i.vertex_normal = normalize(vertex_normal);
	i.view_direction = normalize(view_direction);
	i.camera_position = u_camera_wpos.xyz;
	// A shadow map is not the view: the middle of it stands in.
	i.screen_uv = vec2_splat(0.5);
	i.time = u_material_time.x;
	i.mip_bias = 0.0;

	MaterialSurface s = mg_default_surface();
	evaluate_material(i, s);
	if(s.opacity_mask < s.opacity_mask_clip)
	{
		discard;
	}
#endif

	vec4 light_position = mul(u_viewProj, vec4(v_wpos, 1.0));
#if LINEAR
	float depth = light_position.z * 0.5 + 0.5;
#else
	float depth = light_position.z / light_position.w * 0.5 + 0.5;
#endif
#if PACK_VSM
	gl_FragColor = vec4(depth, depth * depth, 0.0, 0.0);
#else
	gl_FragColor = vec4(depth, 0.0, 0.0, 0.0);
#endif
}

#endif // FS_MATERIAL_GRAPH_SHADOW_SH_HEADER_GUARD
