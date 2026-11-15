#ifndef VS_MATERIAL_GRAPH_SH_HEADER_GUARD
#define VS_MATERIAL_GRAPH_SH_HEADER_GUARD

/*
 * The vertex shader of a material graph whose World Position Offset moves its vertices. The generated shader includes
 * common.sh, normal_basis.sh, material_graph.sh and, for the skinned variants, skinning_palette.sh; defines
 * evaluate_vertex(MaterialInputs i, inout MaterialVertex s) from the graph; includes this, then the main() of its
 * variant: deferred_geom/vs_deferred_geom.sh for the G-buffer, card capture and shadow caster draws,
 * velocity/vs_velocity.sh for the velocity ones (VELOCITY), and the main below for the position-only ones
 * (POSITION_ONLY), which the editor's picking and selection outline draw with. Every main moves a vertex by the same
 * call, so the velocity pass's EQUAL depth test holds; the velocity one moves the frame before's position by the frame
 * before's offset.
 */

#define MATERIAL_WORLD_POSITION_OFFSET 1

// The offset the graph gives the vertex at world_position, drawn with model, of the packed normal and the texture
// coordinates uv, at time seconds. A vertex has no pixel: the screen position is the middle of the view, and textures
// read their top mip.
vec3 mg_world_position_offset(mat4 model, vec3 world_position, vec4 packed_normal, vec2 uv, float time)
{
	vec3 normal = packed_normal.xyz * 2.0 - 1.0;
	vec3 view_direction = u_camera_wpos.xyz - world_position;
	MaterialInputs i;
	i.uv0 = uv;
	i.world_position = world_position;
	i.vertex_normal = normalize(mul(compute_normal_matrix(get_upper_3x3(model)), normal));
	i.view_direction = normalize(view_direction);
	i.camera_position = u_camera_wpos.xyz;
	i.screen_uv = vec2_splat(0.5);
	i.time = time;
	i.mip_bias = 0.0;
	MaterialVertex s;
	s.world_position_offset = vec3_splat(0.0);
	evaluate_vertex(i, s);
	return s.world_position_offset;
}

#if POSITION_ONLY
// The moved position alone, as the editor's vs_picking_id.eshader and vs_outline_mask.eshader place a vertex.
void main()
{
#if SKINNED
	mat4 model = skin_palette_matrix(u_skin_rows.x, a_indices, a_weight);
#else
	mat4 model = u_world[0];
#endif
	vec4 wpos = mul(model, vec4(a_position, 1.0));
	wpos.xyz += mg_world_position_offset(model, wpos.xyz, a_normal, a_texcoord0, u_material_time.x);
	gl_Position = mul(u_viewProj, wpos);
}
#endif

#endif // VS_MATERIAL_GRAPH_SH_HEADER_GUARD
