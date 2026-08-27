$input v_texcoord0

#include "../common.sh"

SAMPLER2D(s_depth, 0);

// Previous frame's view-projection, jitter removed (camera::get_taa_prev_view_projection).
uniform mat4 u_prev_view_proj;

// Fullscreen camera-motion velocity, reconstructed from depth for EVERY pixel so the buffer
// is complete before the movers pass overwrites its objects with true per-object motion.
// Reprojection mirrors TAA_PreviousScreenPos (fs_taa.sc): jittered current unprojection via
// the pass matrices (u_invProj/u_invView), unjittered previous reprojection - so a consumer
// doing prev_uv = uv - velocity reproduces the existing TAA behavior exactly for static
// pixels. Sky (depth 1) reconstructs at the far plane and still yields rotation velocity.
void main()
{
    vec2 uv = v_texcoord0;
    float depth01 = texture2DLod(s_depth, uv, 0.0).x;

    vec3 vs_pos = computeViewSpacePosition(uv, depth01);
    vec4 ws_pos = mul(u_invView, vec4(vs_pos, 1.0));
    vec4 prev_clip4 = mul(u_prev_view_proj, vec4(ws_pos.xyz, 1.0));

    // Behind the previous camera plane: no meaningful previous UV, report "still".
    if(prev_clip4.w <= 0.0)
    {
        gl_FragColor = vec4_splat(0.0);
        return;
    }

    vec3 prev_clip = clipTransform(prev_clip4.xyz / prev_clip4.w);
    vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;

    // RG = total velocity; BA = object-only component, exactly zero for camera-derived
    // pixels so consumers take their static/legacy path here unconditionally.
    gl_FragColor = vec4(uv - prev_uv, 0.0, 0.0);
}
