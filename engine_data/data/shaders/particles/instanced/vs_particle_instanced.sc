$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

uniform vec4 u_billboardRight;
uniform vec4 u_billboardUp;

void main()
{
    // Instance data
    vec3 worldPos = i_data0.xyz;    // Position from instance
    vec4 color = i_data1;           // Color
    vec2 uvOffset = i_data2.xy;     // UV offset for texture sheet animation
    vec2 uvScale = i_data2.zw;      // UV scale for texture sheet animation
    vec3 scale3d = i_data3.xyz;     // 3D scale (allows rectangular particles)
    
    // Use billboard vectors from uniforms (already calculated based on render mode in C++)
    // Apply 3D scale to the billboard vectors
    vec3 cameraRight = u_billboardRight.xyz * scale3d.x;
    vec3 cameraUp = u_billboardUp.xyz * scale3d.y;
    
    // Billboard the quad vertex (a_position is in [-0.5, 0.5] range)
    vec3 vertexWorldPos = worldPos + cameraRight * a_position.x + cameraUp * a_position.y;
    
    gl_Position = mul(u_modelViewProj, vec4(vertexWorldPos, 1.0));
    
    // Apply texture sheet animation UV offset and scale
    v_texcoord0 = a_texcoord0.xy * uvScale + uvOffset;
    
    v_color0 = color;
}
