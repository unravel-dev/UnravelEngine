$input a_position, a_weight, a_indices
$output v_curr_pos, v_prev_pos, v_prev_static_pos

#include "../common.sh"

// Previous-frame skinning palette (see gfx::set_prev_world_transform).
uniform mat4 u_prev_world[BGFX_CONFIG_MAX_BONES];
// Previous frame's view-projection, jitter removed (camera::get_taa_prev_view_projection).
uniform mat4 u_prev_view_proj;

void main()
{
    // Same 4-bone linear blend as vs_deferred_geom_skinned; u_world is already the
    // pre-multiplied (bind-inverse x world) palette.
    mat4 model = a_weight.x * u_world[int(a_indices.x)] +
                 a_weight.y * u_world[int(a_indices.y)] +
                 a_weight.z * u_world[int(a_indices.z)] +
                 a_weight.w * u_world[int(a_indices.w)];

    mat4 prev_model = a_weight.x * u_prev_world[int(a_indices.x)] +
                      a_weight.y * u_prev_world[int(a_indices.y)] +
                      a_weight.z * u_prev_world[int(a_indices.z)] +
                      a_weight.w * u_prev_world[int(a_indices.w)];

    vec4 wpos = mul(model, vec4(a_position, 1.0));
    vec4 prev_wpos = mul(prev_model, vec4(a_position, 1.0));

    // Rasterize with the SAME jittered view-projection as the G-buffer pass so the EQUAL
    // depth test holds; the velocity itself uses the unjittered previous matrix.
    gl_Position = mul(u_viewProj, wpos);
    v_curr_pos = gl_Position;
    v_prev_pos = mul(u_prev_view_proj, prev_wpos);
    // Camera-only reference (current pose through the previous view-projection); see
    // vs_velocity.sc for the rationale.
    v_prev_static_pos = mul(u_prev_view_proj, wpos);
}
