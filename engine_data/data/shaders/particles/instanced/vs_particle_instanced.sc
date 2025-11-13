$input a_position, a_texcoord0, i_data0, i_data1, i_data2
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

void main()
{
    // Instance data
    vec3 worldPos = i_data0.xyz;    // Position from instance
    float scale = i_data0.w;        // Scale from instance
    vec4 color = i_data1;  // color
    float angle = i_data2.x;        // Rotation angle (unused for now)
    
    // Extract camera right/up vectors from view matrix and scale them
#if BGFX_SHADER_LANGUAGE_GLSL
	vec3 cameraRight = vec3(u_view[0][0], u_view[1][0], u_view[2][0]) * scale;
    vec3 cameraUp = vec3(u_view[0][1], u_view[1][1], u_view[2][1]) * scale;
#else
    vec3 cameraRight = vec3(u_view[0][0], u_view[0][1], u_view[0][2]) * scale;
    vec3 cameraUp = vec3(u_view[1][0], u_view[1][1], u_view[1][2]) * scale;
#endif
    
    // Billboard the quad vertex (a_position is in [-0.5, 0.5] range)
    vec3 vertexWorldPos = worldPos + cameraRight * a_position.x + cameraUp * a_position.y;
    
    gl_Position = mul(u_modelViewProj, vec4(vertexWorldPos, 1.0));
    
    v_texcoord0 = a_texcoord0.xy;
    
    v_color0 = color;
}
