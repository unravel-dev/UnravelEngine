$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_depth, v_texcoord0

#include "../../common.sh"

void main()
{
	vec4 matrix_row3 = vec4(i_data3.xyz, 1.0);
	mat4 world_matrix = mtxFromCols(i_data0, i_data1, i_data2, matrix_row3);

	vec4 wpos = mul(world_matrix, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);
	v_depth = gl_Position.z * 0.5 + 0.5;
	v_texcoord0 = a_texcoord0;
}
