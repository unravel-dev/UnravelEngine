$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

// View matrix (glm column-major upload). Billboard uses camera axes from the view matrix.
uniform mat4 u_viewCamera;
uniform vec4 u_eyePos; // xyz = camera / eye position (w unused)

// Helper function to rotate a vector by a quaternion (xyzw)
vec3 quatRotate(vec4 q, vec3 v)
{
    vec3 uv = cross(q.xyz, v);
    vec3 uuv = cross(q.xyz, uv);
    return v + ((uv * q.w) + uuv) * 2.0;
}

// i_data5.x = renderMode, i_data5.yzw = emitterQuat.xyz with w >= 0 (reconstructed here).
vec4 decodeEmitterQuat(vec4 facingData)
{
    vec3 qv = facingData.yzw;
    float qw = sqrt(max(0.0, 1.0 - dot(qv, qv)));
    return vec4(qv, qw);
}

void billboardBaseAxes(vec3 camRight, vec3 camUp, float renderMode, vec4 emitterQuat, out vec3 outRight, out vec3 outUp)
{
    vec3 emitterRight = normalize(quatRotate(emitterQuat, vec3(1.0, 0.0, 0.0)));
    vec3 emitterUp = normalize(quatRotate(emitterQuat, vec3(0.0, 1.0, 0.0)));
    vec3 emitterForward = normalize(quatRotate(emitterQuat, vec3(0.0, 0.0, 1.0)));
    if(renderMode < 0.5)
    {
        outRight = normalize(camRight);
        outUp = normalize(camUp);
    }
    else if(renderMode < 1.5)
    {
        // Horizontal: quad lies in emitter local XZ plane.
        outRight = emitterRight;
        outUp = emitterForward;
    }
    else
    {
        // Vertical: stay upright along emitter local Y, yaw toward camera.
        vec3 rn = camRight - emitterUp * dot(camRight, emitterUp);
        float len = length(rn);
        outRight = len > 0.0001 ? rn * (1.0 / len) : emitterRight;
        outUp = emitterUp;
    }
}

void main()
{
    vec3 worldPos = i_data0.xyz;
    float pivotX = i_data0.w;
    vec4 rotation = i_data1;
    vec3 scale3d = i_data2.xyz;
    float pivotY = i_data2.w;
    vec2 uvOffset = i_data3.xy;
    vec2 uvScale = i_data3.zw;
    vec4 color = i_data4;

    float renderMode = i_data5.x;
    vec4 emitterQuat = decodeEmitterQuat(i_data5);

    vec3 camRight = mtxGetColumn(u_viewCamera, 0).xyz;
    vec3 camUp = mtxGetColumn(u_viewCamera, 1).xyz;

    vec3 right;
    vec3 up;
    billboardBaseAxes(camRight, camUp, renderMode, emitterQuat, right, up);

    vec2 pivot = vec2(pivotX, pivotY);

    float rotXYZLengthSq = dot(rotation.xyz, rotation.xyz);
    float hasRotation = step(0.001, rotXYZLengthSq);

    if(hasRotation > 0.001)
    {
        float fullRotLengthSq = dot(rotation, rotation);
        vec4 normalizedRot = rotation * inversesqrt(max(fullRotLengthSq, 0.0001));

        vec3 toEye = u_eyePos.xyz - worldPos;
        float toEyeLen = length(toEye);
        toEye = toEye * (1.0 / max(toEyeLen, 0.0001));

        vec3 emitterRight = normalize(quatRotate(emitterQuat, vec3(1.0, 0.0, 0.0)));
        vec3 emitterUp = normalize(quatRotate(emitterQuat, vec3(0.0, 1.0, 0.0)));

        // Particle spin is applied relative to the emitter frame so local-space
        // vertical/horizontal constraints keep following the emitter.
        vec3 rotatedUpBasis = quatRotate(normalizedRot, emitterUp);
        vec3 rotatedRightBasis = quatRotate(normalizedRot, emitterRight);

        vec3 rotatedRightCPU = quatRotate(normalizedRot, right);
        vec3 rotatedUpCPU = quatRotate(normalizedRot, up);

        float isStandard = 1.0 - step(0.5, renderMode);
        float isHorizontal = step(0.5, renderMode) * (1.0 - step(1.5, renderMode));
        float isVertical = step(1.5, renderMode);

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

        vec3 verticalRight = cross(toEye, rotatedUpBasis);
        float vertRightLen = length(verticalRight);
        verticalRight = mix(rotatedRightBasis, verticalRight * (1.0 / max(vertRightLen, 0.0001)), step(0.001, vertRightLen));
        vec3 verticalUp = rotatedUpBasis;

        vec3 horizontalRight = rotatedRightCPU;
        vec3 horizontalUp = rotatedUpCPU;

        vec3 rotatedRightFinal = standardRight * isStandard + verticalRight * isVertical + horizontalRight * isHorizontal;
        vec3 rotatedUpFinal = standardUp * isStandard + verticalUp * isVertical + horizontalUp * isHorizontal;

        right = mix(right, rotatedRightFinal, hasRotation);
        up = mix(up, rotatedUpFinal, hasRotation);
    }

    vec3 scaledRight = right * scale3d.x;
    vec3 scaledUp = up * scale3d.y;

    vec2 pivotOffset = pivot - vec2(0.5, 0.5);
    vec3 localVertexPos = scaledRight * (a_position.x + pivotOffset.x) + scaledUp * (a_position.y + pivotOffset.y);
    vec3 vertexWorldPos = worldPos + localVertexPos;

    gl_Position = mul(u_modelViewProj, vec4(vertexWorldPos, 1.0));

    v_texcoord0 = a_texcoord0.xy * uvScale + uvOffset;
    v_color0 = color;
}
