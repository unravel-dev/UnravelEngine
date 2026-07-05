$input a_position, a_texcoord0
$output v_depth, v_texcoord0

#include "../../common.sh"

void main()
{
	vec4 wpos = mul(u_world[0], vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);
	v_depth = gl_Position.z * 0.5 + 0.5;
	v_texcoord0 = a_texcoord0;
}
