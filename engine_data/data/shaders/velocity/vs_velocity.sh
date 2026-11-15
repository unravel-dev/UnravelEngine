#ifndef VS_VELOCITY_SH_HEADER_GUARD
#define VS_VELOCITY_SH_HEADER_GUARD

/*
 * main() of vs_velocity.eshader, and of the velocity variants of a material graph's vertex shader whose World Position
 * Offset moves its vertices (material_graph/vs_material_graph.sh defines MATERIAL_WORLD_POSITION_OFFSET). The includer
 * brings common.sh and, for the skinned variants, skinning_palette.sh.
 */

#if !(SKINNED || INSTANCED || SKINNED_INSTANCED || SKINNED_BAKED_INSTANCED)
// Previous-frame counterpart of u_world (gfx::set_prev_world_transform).
uniform mat4 u_prev_world[1];
#endif
// Previous frame's view-projection, jitter removed (camera::get_prev_view_projection_unjittered).
uniform mat4 u_prev_view_proj;

void main()
{
    // The current pose MUST go through the same math as deferred_geom/vs_deferred_geom.sh's variant of this name: the
    // EQUAL depth test against the G-buffer's raster depends on identical position math.
#if SKINNED
    // u_skin_rows.x is the first row of the current palette, u_skin_rows.y the first row of the previous one.
    mat4 model = skin_palette_matrix(u_skin_rows.x, a_indices, a_weight);
    mat4 prev_model = skin_palette_matrix(u_skin_rows.y, a_indices, a_weight);
#elif SKINNED_INSTANCED
    // i_data0.x is the first row of the current palette, i_data0.y the first row of the previous one.
    mat4 model = skin_palette_matrix(i_data0.x, a_indices, a_weight);
    mat4 prev_model = skin_palette_matrix(i_data0.y, a_indices, a_weight);
#elif SKINNED_BAKED_INSTANCED
    // i_data0..2 are the current world matrix rows and i_data3.x the row of the current frame; i_data4..6 and i_data3.y
    // are the previous frame's.
    mat4 model = baked_skin_matrix(i_data0, i_data1, i_data2, i_data3.x, a_indices, a_weight);
    mat4 prev_model = baked_skin_matrix(i_data4, i_data5, i_data6, i_data3.y, a_indices, a_weight);
#elif INSTANCED
    // The LOD parameter is packed in i_data3.w, so the homogeneous 1.0 is restored.
    vec4 matrix_row3 = vec4(i_data3.xyz, 1.0);
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, matrix_row3);

    // Previous world matrix rides i_data4..7 (the [3][3] slot is never read).
    mat4 prev_model = mtxFromCols(i_data4, i_data5, i_data6, vec4(i_data7.xyz, 1.0));
#else
    mat4 model = u_world[0];
    mat4 prev_model = u_prev_world[0];
#endif

    vec4 wpos = mul(model, vec4(a_position, 1.0));
    vec4 prev_wpos = mul(prev_model, vec4(a_position, 1.0));
#if MATERIAL_WORLD_POSITION_OFFSET
    // The offset of each frame at its time: the material's animation is motion too.
    wpos.xyz += mg_world_position_offset(model, wpos.xyz, a_normal, a_texcoord0, u_material_time.x);
    prev_wpos.xyz += mg_world_position_offset(prev_model, prev_wpos.xyz, a_normal, a_texcoord0, u_material_time.y);
#endif

    // Rasterize with the SAME jittered view-projection as the G-buffer pass so the EQUAL
    // depth test holds; the velocity itself uses the unjittered previous matrix.
    gl_Position = mul(u_viewProj, wpos);
    v_curr_pos = gl_Position;
    v_prev_pos = mul(u_prev_view_proj, prev_wpos);
    // The CURRENT world position through the previous view-projection: the camera-only
    // reference for the object-motion split, computed with the same matrices as v_prev_pos
    // so the split never depends on matrix consistency with any OTHER pass.
    v_prev_static_pos = mul(u_prev_view_proj, wpos);
}

#endif // VS_VELOCITY_SH_HEADER_GUARD
