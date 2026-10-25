$input a_position, a_weight, a_indices, i_data0
$output v_curr_pos, v_prev_pos, v_prev_static_pos

#include "../common.sh"
#include "../skinning_palette.sh"

// Previous frame's view-projection, jitter removed (camera::get_prev_view_projection_unjittered).
uniform mat4 u_prev_view_proj;

void main()
{
	// The current pose MUST go through skin_palette_matrix exactly as vs_deferred_geom_skinned_instanced does: the
	// EQUAL depth test against the G-buffer's raster depends on identical position math. i_data0.x is the first row of
	// the current palette, i_data0.y the first row of the previous one.
	mat4 model = skin_palette_matrix(i_data0.x, a_indices, a_weight);
	mat4 prev_model = skin_palette_matrix(i_data0.y, a_indices, a_weight);

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
