#ifndef VS_MATERIAL_GRAPH_SH_HEADER_GUARD
#define VS_MATERIAL_GRAPH_SH_HEADER_GUARD

/*
 * The vertex shader of a material graph that has a vertex stage: its World Position Offset moves the vertices, or its
 * pixels read what only its vertex shader writes (the object's transform, MATERIAL_OBJECT_DATA, or Vertex Interpolators,
 * MATERIAL_INTERPOLATORS). The generated shader includes common.sh, normal_basis.sh, material_graph.sh and, for the
 * skinned variants, skinning_palette.sh; defines evaluate_vertex(MaterialInputs i, inout MaterialVertex s) from the
 * graph; includes this, then the main() of its variant: deferred_geom/vs_deferred_geom.sh for the G-buffer, card
 * capture and shadow caster draws, velocity/vs_velocity.sh for the velocity ones (VELOCITY), and the main below for the
 * position-only ones (POSITION_ONLY), which the editor's picking and selection outline draw with. Every main moves a
 * vertex by the same call, so the velocity pass's EQUAL depth test holds; the velocity one moves the frame before's
 * position by the frame before's offset. A graph without an offset writes a zero offset, which moves nothing.
 */

#define MATERIAL_WORLD_POSITION_OFFSET 1

// The second texture coordinates and the vertex colour of the vertex, for the mains' calls: the attributes where the
// graph reads them (MATERIAL_UV1_ATTRIBUTE, MATERIAL_VERTEX_COLOR_ATTRIBUTE: its shader declares a_texcoord1,
// a_color0), the first texture coordinates and white otherwise.
#if MATERIAL_UV1_ATTRIBUTE
#define MG_VERTEX_UV1 a_texcoord1
#else
#define MG_VERTEX_UV1 a_texcoord0
#endif
#if MATERIAL_VERTEX_COLOR_ATTRIBUTE
#define MG_VERTEX_COLOR0 a_color0
#else
#define MG_VERTEX_COLOR0 vec4_splat(1.0)
#endif

// What the graph gives the vertex at world_position, drawn with model, of the packed normal, tangent and bitangent, the
// texture coordinates uv and uv1 and the vertex colour color0, at time seconds. A vertex has no pixel: the screen position is the middle of the view, and
// textures read their top mip.
MaterialVertex mg_evaluate_vertex(mat4 model, vec3 world_position, vec4 packed_normal, vec4 packed_tangent, vec4 packed_bitangent, vec2 uv, vec2 uv1, vec4 color0, float time)
{
	mat3 basis = get_upper_3x3(model);
	vec3 view_direction = u_camera_wpos.xyz - world_position;
	MaterialInputs i;
	i.uv0 = uv;
	i.uv1 = uv1;
	i.color0 = color0;
	i.world_position = world_position;
	i.vertex_normal = normalize(mul(compute_normal_matrix(basis), packed_normal.xyz * 2.0 - 1.0));
	i.tangent = normalize(mul(basis, packed_tangent.xyz * 2.0 - 1.0));
	i.bitangent = normalize(mul(basis, packed_bitangent.xyz * 2.0 - 1.0));
	i.two_sided_sign = 1.0;
	i.view_direction = normalize(view_direction);
	i.camera_position = u_camera_wpos.xyz;
	i.camera_direction = mg_camera_direction();
	i.screen_uv = vec2_splat(0.5);
	i.time = time;
	i.mip_bias = 0.0;
	mg_set_domain_input_defaults(i);
	mg_set_object_inputs(i, model);
#if MATERIAL_INTERPOLATORS
	i.interpolator0 = vec4_splat(0.0);
	i.interpolator1 = vec4_splat(0.0);
	i.interpolator2 = vec4_splat(0.0);
	i.interpolator3 = vec4_splat(0.0);
#endif
	MaterialVertex s;
	s.world_position_offset = vec3_splat(0.0);
#if MATERIAL_INTERPOLATORS
	s.interpolator0 = vec4_splat(0.0);
	s.interpolator1 = vec4_splat(0.0);
	s.interpolator2 = vec4_splat(0.0);
	s.interpolator3 = vec4_splat(0.0);
#endif
	evaluate_vertex(i, s);
	return s;
}

// The offset alone, for the mains that write nothing else (velocity, position only).
vec3 mg_world_position_offset(mat4 model, vec3 world_position, vec4 packed_normal, vec4 packed_tangent, vec4 packed_bitangent, vec2 uv, vec2 uv1, vec4 color0, float time)
{
	return mg_evaluate_vertex(model, world_position, packed_normal, packed_tangent, packed_bitangent, uv, uv1, color0, time).world_position_offset;
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
	wpos.xyz += mg_world_position_offset(model, wpos.xyz, a_normal, a_tangent, a_bitangent, a_texcoord0, MG_VERTEX_UV1, MG_VERTEX_COLOR0, u_material_time.x);
	gl_Position = mul(u_viewProj, wpos);
}
#endif

#endif // VS_MATERIAL_GRAPH_SH_HEADER_GUARD
