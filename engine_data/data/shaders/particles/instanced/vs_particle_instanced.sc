$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

uniform vec4 u_billboardRight;
uniform vec4 u_billboardUp;
uniform vec4 u_eyePos; // xyz = eye position, w = render mode (0=Billboard, 1=Horizontal, 2=Vertical)

// Helper function to rotate a vector by a quaternion
vec3 quatRotate(vec4 q, vec3 v)
{
    vec3 uv = cross(q.xyz, v);
    vec3 uuv = cross(q.xyz, uv);
    return v + ((uv * q.w) + uuv) * 2.0;
}

void main()
{
    // Instance data (reordered: position, rotation, scale, uv, color)
    vec3 worldPos = i_data0.xyz;        // Position from instance
    float pivotX = i_data0.w;           // Pivot X (0,0 = bottom-left, 0.5,0.5 = center, 1,1 = top-right)
    vec4 rotation = i_data1;            // Rotation quaternion (xyzw)
    vec3 scale3d = i_data2.xyz;         // 3D scale (allows rectangular particles)
    float pivotY = i_data2.w;           // Pivot Y
    vec2 uvOffset = i_data3.xy;         // UV offset for texture sheet animation
    vec2 uvScale = i_data3.zw;          // UV scale for texture sheet animation
    vec4 color = i_data4;               // Color
    
    vec2 pivot = vec2(pivotX, pivotY);  // Combine pivot components
    
    // Get base billboard vectors from uniforms
    vec3 right = u_billboardRight.xyz;
    vec3 up = u_billboardUp.xyz;
    
    // Get render mode: 0=Billboard, 1=Horizontal, 2=Vertical
    float renderMode = u_eyePos.w;
    
    // Rotate billboard vectors by quaternion if rotation is meaningful
    // Identity quaternion is (0,0,0,1), so check if it's NOT identity
    float rotXYZLengthSq = dot(rotation.xyz, rotation.xyz);
    float hasRotation = step(0.001, rotXYZLengthSq);
    
    if(hasRotation > 0.001)
    {
        // Normalize the quaternion (safe even if no rotation due to hasRotation multiplier)
        float fullRotLengthSq = dot(rotation, rotation);
        vec4 normalizedRot = rotation * inversesqrt(max(fullRotLengthSq, 0.0001));
        
        // Calculate common values
        vec3 toEye = u_eyePos.xyz - worldPos;
        float toEyeLen = length(toEye);
        toEye = toEye * (1.0 / max(toEyeLen, 0.0001));
        
        // Rotate standard basis vectors (for Standard and Vertical modes)
        vec3 rotatedUpBasis = quatRotate(normalizedRot, vec3(0.0, 1.0, 0.0));
        vec3 rotatedRightBasis = quatRotate(normalizedRot, vec3(1.0, 0.0, 0.0));
        
        // Rotate CPU-provided billboard vectors (for Horizontal mode)
        vec3 rotatedRightCPU = quatRotate(normalizedRot, right);
        vec3 rotatedUpCPU = quatRotate(normalizedRot, up);
        
        // Mode flags (branchless)
        float isStandard = step(renderMode, 0.5);
        float isVertical = step(1.5, renderMode);
        float isHorizontal = (1.0 - isStandard) * (1.0 - isVertical);
        
        // Standard billboard path: project rotated up onto billboard plane
        vec3 projectedUp = rotatedUpBasis - toEye * dot(rotatedUpBasis, toEye);
        float projUpLen = length(projectedUp);
        float useProjectedUp = step(0.001, projUpLen);
        projectedUp = projectedUp * (1.0 / max(projUpLen, 0.0001));
        
        vec3 projectedRight = rotatedRightBasis - toEye * dot(rotatedRightBasis, toEye);
        float projRightLen = length(projectedRight);
        projectedRight = projectedRight * (1.0 / max(projRightLen, 0.0001));
        
        vec3 standardRight = mix(projectedRight, cross(toEye, projectedUp), useProjectedUp);
        vec3 standardUp = mix(cross(toEye, projectedRight), projectedUp, useProjectedUp);
        standardRight = normalize(standardRight);
        standardUp = normalize(standardUp);
        
        // Vertical billboard path: maintain rotated up, recalculate right
        vec3 verticalRight = cross(toEye, rotatedUpBasis);
        float vertRightLen = length(verticalRight);
        verticalRight = mix(rotatedRightBasis, verticalRight * (1.0 / max(vertRightLen, 0.0001)), step(0.001, vertRightLen));
        vec3 verticalUp = rotatedUpBasis;
        
        // Horizontal billboard path: rotate CPU-provided vectors
        vec3 horizontalRight = rotatedRightCPU;
        vec3 horizontalUp = rotatedUpCPU;
        
        // Blend between modes (only one will be active)
        vec3 rotatedRightFinal = standardRight * isStandard + verticalRight * isVertical + horizontalRight * isHorizontal;
        vec3 rotatedUpFinal = standardUp * isStandard + verticalUp * isVertical + horizontalUp * isHorizontal;
        
        // Apply rotation or keep original vectors based on hasRotation
        right = mix(right, rotatedRightFinal, hasRotation);
        up = mix(up, rotatedUpFinal, hasRotation);
    }
    
    // Apply 3D scale to the (potentially rotated) billboard vectors
    vec3 scaledRight = right * scale3d.x;
    vec3 scaledUp = up * scale3d.y;
    
    // Apply pivot offset
    // a_position is in [-0.5, 0.5] range
    // pivot is in [0, 1] range where 0.5,0.5 is center
    // Convert pivot from [0,1] to [-0.5, 0.5] offset
    vec2 pivotOffset = pivot - vec2(0.5, 0.5);
    
    // Calculate local vertex position with pivot applied
    vec3 localVertexPos = scaledRight * (a_position.x + pivotOffset.x) + scaledUp * (a_position.y + pivotOffset.y);
    
    // Transform to world position
    vec3 vertexWorldPos = worldPos + localVertexPos;
    
    gl_Position = mul(u_modelViewProj, vec4(vertexWorldPos, 1.0));
    
    // Apply texture sheet animation UV offset and scale
    v_texcoord0 = a_texcoord0.xy * uvScale + uvOffset;
    
    v_color0 = color;
}
