$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0, a_weight, a_indices, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_wpos, v_pos, v_wnormal, v_wtangent, v_wbitangent, v_texcoord0, v_lod_params, v_material0, v_material1, v_material2, v_material3

/*
 * vs_deferred_geom_skinned_instanced.sc for batches whose instances carry per-instance material values (material
 * instances that share their shared material's draw): i_data1..4 hold each instance's material constants
 * (skinned_instance_material_vertex_data, pbr_material::write_instance_constants), which
 * fs_deferred_geom_instance_material.sc reads in place of the material uniforms.
 *
 * The position math is the same as vs_deferred_geom_skinned_instanced.sc and vs_velocity_skinned_instanced.sc: the
 * velocity pass draws these instances again with an EQUAL depth test.
 */

#include "../common.sh"
#include "../skinning_palette.sh"
#include "normal_basis.sh"

void main()
{
	mat4 model = skin_palette_matrix(i_data0.x, a_indices, a_weight);
	vec4 wpos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);

	vec4 normal = a_normal * 2.0 - 1.0;
	vec4 tangent = a_tangent * 2.0 - 1.0;
	vec4 bitangent = a_bitangent * 2.0 - 1.0;

	mat3 basis = get_upper_3x3(model);
	vec3 wnormal = normalize(mul(compute_normal_matrix(basis), normal.xyz));
	vec3 wtangent = normalize(mul(basis, tangent.xyz));
	vec3 wbitangent = normalize(mul(basis, bitangent.xyz));

	v_wpos = wpos.xyz;
	v_pos = gl_Position.xyz / gl_Position.w;
	v_wnormal = wnormal;
	v_wtangent = wtangent;
	v_wbitangent = wbitangent;
	v_texcoord0 = a_texcoord0;
	v_lod_params = vec2(i_data0.z, 0.0);

	v_material0 = i_data1;
	v_material1 = i_data2;
	v_material2 = i_data3;
	v_material3 = i_data4;
}
