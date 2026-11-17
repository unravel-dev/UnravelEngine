#ifndef FS_MATERIAL_GRAPH_PREVIEW_SH_HEADER_GUARD
#define FS_MATERIAL_GRAPH_PREVIEW_SH_HEADER_GUARD

/*
 * main() of a material graph's preview shader (material_graph_compile_options::is_preview): the value of one node over
 * a tile of the editor's swatch atlas, drawn by vs_clip_quad.eshader. evaluate_preview(i, node) computes every node and
 * returns the one u_mg_preview.x names as a colour.
 *
 * The tile stands for a flat patch facing the viewer: its UV is the mesh's, the world position spans -1..1 across it,
 * the normal and the view look along +Z (the camera along -Z), and the tangent and bitangent along +X and +Y. The value is clamped to 0..1 and shown sRGB-encoded, as a colour of the
 * surface would be.
 */

// x: the index in the graph of the node to show.
uniform vec4 u_mg_preview;

void main()
{
	MaterialInputs i;
	i.uv0 = v_texcoord0;
	i.world_position = vec3(v_texcoord0 * 2.0 - 1.0, 0.0);
	i.vertex_normal = vec3(0.0, 0.0, 1.0);
	i.tangent = vec3(1.0, 0.0, 0.0);
	i.bitangent = vec3(0.0, 1.0, 0.0);
	i.two_sided_sign = 1.0;
	i.view_direction = vec3(0.0, 0.0, 1.0);
	i.camera_position = vec3(0.0, 0.0, 2.0);
	i.camera_direction = vec3(0.0, 0.0, -1.0);
	i.screen_uv = v_texcoord0;
	i.time = u_material_time.x;
	i.mip_bias = 0.0;
	mg_set_domain_input_defaults(i);
	mg_set_world_object_inputs(i);

	vec4 value = evaluate_preview(i, u_mg_preview.x);
	gl_FragColor = vec4(toGamma(saturate(value.rgb)), 1.0);
}

#endif // FS_MATERIAL_GRAPH_PREVIEW_SH_HEADER_GUARD
