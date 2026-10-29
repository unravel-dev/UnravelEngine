$input a_position, a_weight, a_indices, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5, i_data6
$output v_curr_pos, v_prev_pos, v_prev_static_pos

#include "../common.sh"
#include "../skinning_palette.sh"

// Previous frame's view-projection, jitter removed (camera::get_prev_view_projection_unjittered).
uniform mat4 u_prev_view_proj;

void main()
{
	// The current pose MUST go through baked_skin_matrix exactly as vs_deferred_geom_skinned_baked_instanced does:
	// the EQUAL depth test against the G-buffer's raster depends on identical position math. i_data0..2 are the
	// current world matrix rows and i_data3.x the row of the current frame; i_data4..6 and i_data3.y are the
	// previous frame's.
	mat4 model = baked_skin_matrix(i_data0, i_data1, i_data2, i_data3.x, a_indices, a_weight);
	mat4 prev_model = baked_skin_matrix(i_data4, i_data5, i_data6, i_data3.y, a_indices, a_weight);

	vec4 wpos = mul(model, vec4(a_position, 1.0));
	vec4 prev_wpos = mul(prev_model, vec4(a_position, 1.0));

	// Rasterize with the SAME jittered view-projection as the G-buffer pass so the EQUAL depth test holds; the
	// velocity itself uses the unjittered previous matrix.
	gl_Position = mul(u_viewProj, wpos);
	v_curr_pos = gl_Position;
	v_prev_pos = mul(u_prev_view_proj, prev_wpos);
	// Camera-only reference (current pose through the previous view-projection); see vs_velocity.sc for the
	// rationale of the object/camera split.
	v_prev_static_pos = mul(u_prev_view_proj, wpos);
}
