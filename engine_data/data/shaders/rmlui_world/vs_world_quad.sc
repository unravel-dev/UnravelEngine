$input a_position, a_texcoord0
$output v_texcoord0

#include "common.sh"

// Screen pixels the quad grows past its border, so the pixels that straddle the border are drawn
// and the fragment shader can fade them by coverage. More than one pixel keeps them drawn on
// tilted quads, where a step along the quad covers less than a pixel on screen.
#define EDGE_EXPAND_PIXELS 1.5

void main()
{
    vec4 clipPos = mul(u_viewProj, mul(u_world[0], vec4(a_position, 1.0)));
    // World size of one screen pixel at this vertex's depth.
    float pixelWorldSize = 2.0 * abs(clipPos.w) / (u_viewRect.w * u_proj[1][1]);
    vec3 axisX = mul(u_world[0], vec4(1.0, 0.0, 0.0, 0.0)).xyz;
    vec3 axisY = mul(u_world[0], vec4(0.0, 1.0, 0.0, 0.0)).xyz;
    vec2 expand = EDGE_EXPAND_PIXELS * pixelWorldSize / vec2(length(axisX), length(axisY));
    // Corners sit at +-0.5 and texcoords at 0 / 1, so a corner moves outward by the same amount
    // in both spaces; the texcoords keep their mapping and run past [0, 1] in the grown border.
    vec3 position = vec3(a_position.xy + sign(a_position.xy) * expand, a_position.z);
    gl_Position = mul(u_viewProj, mul(u_world[0], vec4(position, 1.0)));
    v_texcoord0 = a_texcoord0 + (a_texcoord0 - 0.5) * 2.0 * expand;
}
