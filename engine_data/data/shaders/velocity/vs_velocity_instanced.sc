$input a_position, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5, i_data6, i_data7
$output v_curr_pos, v_prev_pos, v_prev_static_pos

#include "../common.sh"

// Previous frame's view-projection, jitter removed (camera::get_prev_view_projection_unjittered).
uniform mat4 u_prev_view_proj;

void main()
{
    // Current world matrix reconstruction MUST mirror vs_deferred_geom_instanced bit for
    // bit (LOD parameter packed in i_data3.w, homogeneous 1.0 restored) - the EQUAL depth
    // test against the G-buffer's batched raster depends on identical position math.
    vec4 matrix_row3 = vec4(i_data3.xyz, 1.0);
    mat4 world_matrix = mtxFromCols(i_data0, i_data1, i_data2, matrix_row3);

    // Previous world matrix rides i_data4..7 (the [3][3] slot is never read).
    mat4 prev_world_matrix = mtxFromCols(i_data4, i_data5, i_data6, vec4(i_data7.xyz, 1.0));

    vec4 wpos = mul(world_matrix, vec4(a_position, 1.0));
    vec4 prev_wpos = mul(prev_world_matrix, vec4(a_position, 1.0));

    // Rasterize with the SAME jittered view-projection as the G-buffer pass so the EQUAL
    // depth test holds; the velocity itself uses the unjittered previous matrix.
    gl_Position = mul(u_viewProj, wpos);
    v_curr_pos = gl_Position;
    v_prev_pos = mul(u_prev_view_proj, prev_wpos);
    // Camera-only reference (current transform through the previous view-projection); see
    // vs_velocity.sc for the rationale of the object/camera split.
    v_prev_static_pos = mul(u_prev_view_proj, wpos);
}
