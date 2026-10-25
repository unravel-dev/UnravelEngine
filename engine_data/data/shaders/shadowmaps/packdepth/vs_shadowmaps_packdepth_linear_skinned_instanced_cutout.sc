$input a_position, a_weight, a_indices, a_texcoord0, i_data0
$output v_depth, v_texcoord0

/*
 * vs_shadowmaps_packdepth_linear_skinned_cutout.sc for instanced skinned batches: the bone palette comes from the
 * palette buffer (skinning_palette.sh) at the instance's first row (i_data0.x).
 */

#include "../../common.sh"
#include "../../skinning_palette.sh"

void main()
{
	mat4 model = skin_palette_matrix(i_data0.x, a_indices, a_weight);
	vec4 wpos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);
	v_depth = gl_Position.z * 0.5 + 0.5;
	v_texcoord0 = a_texcoord0;
}
