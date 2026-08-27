$input a_position
$output v_curr_pos, v_prev_pos, v_prev_static_pos

#include "../common.sh"

// Previous-frame counterpart of u_world (see gfx::set_prev_world_transform).
uniform mat4 u_prev_world[BGFX_CONFIG_MAX_BONES];
// Previous frame's view-projection, jitter removed (camera::get_taa_prev_view_projection).
uniform mat4 u_prev_view_proj;

void main()
{
    vec4 wpos = mul(u_world[0], vec4(a_position, 1.0));
    vec4 prev_wpos = mul(u_prev_world[0], vec4(a_position, 1.0));

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
