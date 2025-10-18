$input a_position, a_texcoord0, i_data0, i_data1
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

void main()
{
    // Instance data
    vec3 worldPos = i_data0.xyz;    // Position from instance
    float scale = i_data0.w;        // Scale from instance
    uint packedColor = floatBitsToUint(i_data1.x);  // Packed color
    float blend = i_data1.y;        // Blend factor
    float angle = i_data1.z;        // Rotation angle (unused for now)
    
    // Extract camera right/up vectors from view matrix and scale them
    vec3 cameraRight = vec3(u_view[0][0], u_view[0][1], u_view[0][2]) * scale;
    vec3 cameraUp = vec3(u_view[1][0], u_view[1][1], u_view[1][2]) * scale;
    
    // Billboard the quad vertex (a_position is in [-0.5, 0.5] range)
    vec3 vertexWorldPos = worldPos + cameraRight * a_position.x + cameraUp * a_position.y;
    
    gl_Position = mul(u_modelViewProj, vec4(vertexWorldPos, 1.0));
    
    v_texcoord0 = vec4(a_texcoord0.xy, blend, 0.0f);
    
    // Unpack color from uint32
    v_color0 = vec4(
        float((packedColor >> 0u) & 0xFFu) / 255.0,
        float((packedColor >> 8u) & 0xFFu) / 255.0, 
        float((packedColor >> 16u) & 0xFFu) / 255.0,
        float((packedColor >> 24u) & 0xFFu) / 255.0
    );
}
