#ifndef VS_DEFERRED_GEOM_SH_HEADER_GUARD
#define VS_DEFERRED_GEOM_SH_HEADER_GUARD

/*
 * main() of vs_deferred_geom.eshader, and of the vertex shader of a material graph with a vertex stage
 * (material_graph/vs_material_graph.sh defines MATERIAL_WORLD_POSITION_OFFSET): its offset moves the vertex, and it
 * writes what the graph's pixels read of the object (MATERIAL_OBJECT_DATA) and its Vertex Interpolators
 * (MATERIAL_INTERPOLATORS). The includer brings common.sh, normal_basis.sh and, for the skinned variants,
 * skinning_palette.sh.
 *
 * The position math of each variant is the same as velocity/vs_velocity.sh's variant of its name, except that a material
 * variant has its base's: the velocity pass draws these meshes again with an EQUAL depth test.
 */

#if !(INSTANCED || SKINNED_INSTANCED || SKINNED_BAKED_INSTANCED)
uniform vec4 u_lod_params;
#endif

#if DEFERRED_GEOM_CARD_CAPTURE
uniform mat4 u_card_capture_view_proj;
#define DEFERRED_GEOM_VIEW_PROJ u_card_capture_view_proj
#else
#define DEFERRED_GEOM_VIEW_PROJ u_viewProj
#endif

void main()
{
#if SKINNED
    mat4 model = skin_palette_matrix(u_skin_rows.x, a_indices, a_weight);
#elif SKINNED_INSTANCED
    mat4 model = skin_palette_matrix(i_data0.x, a_indices, a_weight);
#elif SKINNED_BAKED_INSTANCED
    mat4 model = baked_skin_matrix(i_data0, i_data1, i_data2, i_data3.x, a_indices, a_weight);
#elif INSTANCED
    // i_data0..3 are the world matrix, which instances store row by row, with the LOD parameter in its [3][3] slot.
    vec4 matrix_row3 = vec4(i_data3.xyz, 1.0);
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, matrix_row3);
#else
    mat4 model = u_world[0];
#endif
    // The vertex as an offset from the view origin (view_origin.sh): the view's matrices take offsets.
    vec4 wpos = vec4(mulWorldOffset(model, a_position), 1.0);
#if MATERIAL_WORLD_POSITION_OFFSET
    // The graph's vertex stage: its offset, and what it passes the pixels. Its nodes read absolute positions.
    MaterialVertex vertex_stage = mg_evaluate_vertex(model, viewOffsetToWorld(wpos.xyz), a_normal, a_tangent, a_bitangent, a_texcoord0, MG_VERTEX_UV1, MG_VERTEX_COLOR0, u_material_time.x);
    wpos.xyz += vertex_stage.world_position_offset;
#endif
    gl_Position = mul(DEFERRED_GEOM_VIEW_PROJ, wpos);

    vec4 normal = a_normal * 2.0 - 1.0;
    vec4 tangent = a_tangent * 2.0 - 1.0;
    vec4 bitangent = a_bitangent * 2.0 - 1.0;

    mat3 basis = get_upper_3x3(model);
#if SKINNED_BAKED_INSTANCED
    // One matrix for all three vectors. Tangents by the upper 3x3, as the other programs do, took this pass from 12.8 to
    // 18.4 ms with 120 enemies on the Galaxy S21 Ultra (Mali-G78, driver r38p0), though malioc scored it as cheaper.
    mat3 normal_matrix = compute_normal_matrix(basis);
    vec3 wnormal = normalize(mul(normal_matrix, normal.xyz));
    vec3 wtangent = normalize(mul(normal_matrix, tangent.xyz));
    vec3 wbitangent = normalize(mul(normal_matrix, bitangent.xyz));
#else
    vec3 wnormal = normalize(mul(compute_normal_matrix(basis), normal.xyz));
    vec3 wtangent = normalize(mul(basis, tangent.xyz));
    vec3 wbitangent = normalize(mul(basis, bitangent.xyz));
#endif

    v_wpos = wpos.xyz;
    v_pos = gl_Position.xyz / gl_Position.w;
    v_wnormal = wnormal;
    v_wtangent = wtangent;
    v_wbitangent = wbitangent;
    v_texcoord0 = a_texcoord0;
#if SKINNED_INSTANCED
    v_lod_params = vec2(i_data0.z, 0.0);
#elif SKINNED_BAKED_INSTANCED
    v_lod_params = vec2(i_data3.z, 0.0);
#elif INSTANCED
    v_lod_params = vec2(i_data3.w, 0.0);
#else
    v_lod_params = u_lod_params.xy;
#endif

#if MATERIAL_OBJECT_DATA
    // The model matrix for the pixels: each axis with one component of the pivot.
    vec3 object_position = mul(model, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    v_object0 = vec4(mul(model, vec4(1.0, 0.0, 0.0, 0.0)).xyz, object_position.x);
    v_object1 = vec4(mul(model, vec4(0.0, 1.0, 0.0, 0.0)).xyz, object_position.y);
    v_object2 = vec4(mul(model, vec4(0.0, 0.0, 1.0, 0.0)).xyz, object_position.z);
#endif
#if MATERIAL_UV1
    v_texcoord1 = a_texcoord1;
#endif
#if MATERIAL_VERTEX_COLOR
    v_color0 = a_color0;
#endif
#if MATERIAL_INTERPOLATORS > 0
    v_interpolator0 = vertex_stage.interpolator0;
#endif
#if MATERIAL_INTERPOLATORS > 1
    v_interpolator1 = vertex_stage.interpolator1;
#endif
#if MATERIAL_INTERPOLATORS > 2
    v_interpolator2 = vertex_stage.interpolator2;
#endif
#if MATERIAL_INTERPOLATORS > 3
    v_interpolator3 = vertex_stage.interpolator3;
#endif

#if INSTANCED && INSTANCE_MATERIAL
    // The instance's material constants (instance_material_vertex_data, pbr_material::write_instance_constants).
    v_material0 = i_data4;
    v_material1 = i_data5;
    v_material2 = i_data6;
    v_material3 = i_data7;
#elif INSTANCE_MATERIAL
    // The instance's material constants (skinned_instance_material_vertex_data, pbr_material::write_instance_constants).
    v_material0 = i_data1;
    v_material1 = i_data2;
    v_material2 = i_data3;
    v_material3 = i_data4;
#endif
}

#endif // VS_DEFERRED_GEOM_SH_HEADER_GUARD
