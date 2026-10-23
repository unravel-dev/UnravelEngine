$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5, i_data6, i_data7
$output v_wpos, v_pos, v_wnormal, v_wtangent, v_wbitangent, v_texcoord0, v_lod_params, v_material0, v_material1, v_material2, v_material3

/*
 * vs_deferred_geom_instanced.sc for batches whose instances carry per-instance material values (material instances
 * that share their shared material's draw): i_data4..7 hold each instance's material constants
 * (instance_material_vertex_data, pbr_material::write_instance_constants), which fs_deferred_geom_instance_material.sc
 * reads in place of the material uniforms.
 *
 * The position math is the same as vs_deferred_geom_instanced.sc and vs_velocity_instanced.sc, line for line: the
 * velocity pass draws these instances again with an EQUAL depth test.
 */

#include "../common.sh"

void main()
{
    // The LOD parameter is packed in i_data3.w (matrix[3][3] position)
    float lod_param = i_data3.w;
    vec4 matrix_row3 = vec4(i_data3.xyz, 1.0);
    mat4 world_matrix = mtxFromCols(i_data0, i_data1, i_data2, matrix_row3);
    vec4 wpos = mul(world_matrix, vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, wpos);

    // Decode normal, tangent, and bitangent from [0,1] to [-1,1] range
    vec4 normal = a_normal * 2.0 - 1.0;
    vec4 tangent = a_tangent * 2.0 - 1.0;
    vec4 bitangent = a_bitangent * 2.0 - 1.0;

    mat3 modelIT = calculateInverseTranspose(world_matrix);
    vec3 wnormal = normalize(mul(modelIT, normal.xyz));
    vec3 wtangent = normalize(mul(modelIT, tangent.xyz));
    vec3 wbitangent = normalize(mul(modelIT, bitangent.xyz));

    v_wpos = wpos.xyz;
    v_pos = gl_Position.xyz / gl_Position.w;
    v_wnormal = wnormal;
    v_wtangent = wtangent;
    v_wbitangent = wbitangent;
    v_texcoord0 = a_texcoord0;
    v_lod_params = vec2(lod_param, 0.0);

    v_material0 = i_data4;
    v_material1 = i_data5;
    v_material2 = i_data6;
    v_material3 = i_data7;
}
