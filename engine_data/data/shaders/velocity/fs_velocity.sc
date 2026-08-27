$input v_curr_pos, v_prev_pos, v_prev_static_pos

#include "../common.sh"

// Per-object motion vectors for movers, layered over the camera-derived fullscreen pass.
// Output (matches fs_velocity_camera and the TAA formulation, camera.h:267-275):
//   RG = total velocity     = uv_curr - uv_prev(previous pose)
//   BA = object-only delta  = uv_prev(current pose) - uv_prev(previous pose)
// Both reprojections use the SAME u_prev_view_proj in the same draw, so the camera/object
// split is exact by construction and consumers never re-derive camera velocity from their
// own matrices. Consumers reproject as prev_uv = uv - RG and classify motion from |BA|.
void main()
{
    vec3 curr_ndc = clipTransform(v_curr_pos.xyz / v_curr_pos.w);
    vec2 curr_uv = curr_ndc.xy * 0.5 + 0.5;

    // Behind the previous camera plane (either reprojection): no meaningful previous UV.
    // Degrade to "still" - the consumer's own rejection handles the fresh pixels.
    if(v_prev_pos.w <= 0.0 || v_prev_static_pos.w <= 0.0)
    {
        gl_FragColor = vec4_splat(0.0);
        return;
    }

    vec3 prev_ndc = clipTransform(v_prev_pos.xyz / v_prev_pos.w);
    vec2 prev_uv = prev_ndc.xy * 0.5 + 0.5;

    vec3 prev_static_ndc = clipTransform(v_prev_static_pos.xyz / v_prev_static_pos.w);
    vec2 prev_static_uv = prev_static_ndc.xy * 0.5 + 0.5;

    gl_FragColor = vec4(curr_uv - prev_uv, prev_static_uv - prev_uv);
}
