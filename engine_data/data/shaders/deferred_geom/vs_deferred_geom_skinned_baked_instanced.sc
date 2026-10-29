$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0, a_weight, a_indices, i_data0, i_data1, i_data2, i_data3
$output v_wpos, v_pos, v_wnormal, v_wtangent, v_wbitangent, v_texcoord0, v_lod_params

/*
 * vs_deferred_geom_skinned_instanced.sc for baked animation draws (baked_animation.h): the palette rows are in the
 * mesh's model space and the instance carries its world matrix. i_data0..2 = world matrix rows, i_data3 = (frame
 * row, previous frame row, LOD parameter, unused); u_baked_part adds the drawn submesh's palette.
 *
 * The position math is the same as vs_velocity_skinned_baked_instanced.sc: the velocity pass draws these instances
 * again with an EQUAL depth test.
 */

#include "../common.sh"
#include "../skinning_palette.sh"

void main()
{
	mat4 model = baked_skin_matrix(i_data0, i_data1, i_data2, i_data3.x, a_indices, a_weight);
	vec4 wpos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);

	vec4 normal = a_normal * 2.0 - 1.0;
	vec4 tangent = a_tangent * 2.0 - 1.0;
	vec4 bitangent = a_bitangent * 2.0 - 1.0;

	mat3 modelIT = calculateInverseTranspose(model);
	vec3 wnormal = normalize(mul(modelIT, normal.xyz));
	vec3 wtangent = normalize(mul(modelIT, tangent.xyz));
	vec3 wbitangent = normalize(mul(modelIT, bitangent.xyz));

	v_wpos = wpos.xyz;
	v_pos = gl_Position.xyz / gl_Position.w;
	v_wnormal = wnormal;
	v_wtangent = wtangent;
	v_wbitangent = wbitangent;
	v_texcoord0 = a_texcoord0;
	v_lod_params = vec2(i_data3.z, 0.0);
}
