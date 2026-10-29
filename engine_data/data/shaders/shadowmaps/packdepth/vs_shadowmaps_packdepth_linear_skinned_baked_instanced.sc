$input a_position, a_weight, a_indices, i_data0, i_data1, i_data2, i_data3
$output v_depth

/*
 * vs_shadowmaps_packdepth_linear_skinned_instanced.sc for baked animation draws (baked_animation.h): model-space
 * palette rows from the instance's frame row (i_data3.x), under the instance's world matrix rows (i_data0..2).
 */

#include "../../common.sh"
#include "../../skinning_palette.sh"

void main()
{
	mat4 model = baked_skin_matrix(i_data0, i_data1, i_data2, i_data3.x, a_indices, a_weight);
	vec4 wpos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);
	v_depth = gl_Position.z * 0.5 + 0.5;
}
