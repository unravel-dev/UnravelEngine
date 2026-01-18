$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_wpos, v_pos, v_wnormal, v_wtangent, v_wbitangent, v_texcoord0, v_lod_params

#include "../common.sh"

void main()
{
    // Reconstruct world matrix from instance data
    // i_data0-i_data3 represent the 4 rows/columns of the world transformation matrix
    // The LOD parameter is packed in i_data3.w (matrix[3][3] position)
    
    // Extract LOD parameter before matrix reconstruction
    float lod_param = i_data3.w;
    
    // Restore the homogeneous coordinate for proper matrix reconstruction
    vec4 matrix_row3 = vec4(i_data3.xyz, 1.0);
    
    // Reconstruct the world transformation matrix from instance data
    // Using mtxFromRows since our instance data is stored row-major
    mat4 world_matrix = mtxFromCols(i_data0, i_data1, i_data2, matrix_row3);
    
    // Transform vertex position to world space using instance world matrix
    vec4 wpos = mul(world_matrix, vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, wpos);

    // Decode normal, tangent, and bitangent from [0,1] to [-1,1] range
    vec4 normal = a_normal * 2.0 - 1.0;
    vec4 tangent = a_tangent * 2.0 - 1.0;
    vec4 bitangent = a_bitangent * 2.0 - 1.0;

    // Calculate inverse transpose matrix for normal transformation
    // Using the instance world matrix instead of u_world[0]
    mat3 modelIT = calculateInverseTranspose(world_matrix);
    
    // Transform normals, tangents, and bitangents to world space
    vec3 wnormal = normalize(mul(modelIT, normal.xyz));
    vec3 wtangent = normalize(mul(modelIT, tangent.xyz));
    vec3 wbitangent = normalize(mul(modelIT, bitangent.xyz));
    
    // Output world space position
    v_wpos = wpos.xyz;
    v_pos = gl_Position.xyz / gl_Position.w;

    // Output world space normal vectors
    v_wnormal = wnormal;
    v_wtangent = wtangent;
    v_wbitangent = wbitangent;

    // Pass through texture coordinates
    v_texcoord0 = a_texcoord0;
    
    // Pass LOD parameter to fragment shader (using vec2 for consistency)
    v_lod_params = vec2(lod_param, 0.0);
}
