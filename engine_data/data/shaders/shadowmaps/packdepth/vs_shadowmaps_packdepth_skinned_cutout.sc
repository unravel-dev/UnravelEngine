$input a_position, a_weight, a_indices, a_texcoord0
$output v_position, v_texcoord0

#include "../../common.sh"

void main()
{
	mat4 model = a_weight.x * u_world[int(a_indices.x)] +
	             a_weight.y * u_world[int(a_indices.y)] +
	             a_weight.z * u_world[int(a_indices.z)] +
	             a_weight.w * u_world[int(a_indices.w)];

	vec4 wpos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);
	v_position = gl_Position;
	v_texcoord0 = a_texcoord0;
}
