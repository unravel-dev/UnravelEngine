$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5, i_data6, i_data7
$output v_texcoord0, v_texcoord1, v_color0, v_particle

#include <bgfx_shader.sh>

// View matrix (glm column-major upload). Billboard uses camera axes from the view matrix.
uniform mat4 u_viewCamera;

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

    // align_to_direction: i_data1 turns +Z onto the particle's motion (identity when not aligned).
    // The quad turns in its own plane - the camera's, the emitter's XZ plane or the upright one - so
    // the texture's up axis (its top edge) follows the motion projected into that plane, in every
    // render mode.
    if(dot(rotation.xyz, rotation.xyz) > 1e-8)
    {
        vec4 motionRotation = rotation * inversesqrt(max(dot(rotation, rotation), 1e-8));
        vec3 motion = quatRotate(motionRotation, vec3(0.0, 0.0, 1.0));
        vec2 inPlane = vec2(dot(motion, right), dot(motion, up));
        float inPlaneLengthSq = dot(inPlane, inPlane);
        if(inPlaneLengthSq > 1e-6)
        {
            inPlane *= inversesqrt(inPlaneLengthSq);
            vec3 alignedUp = right * inPlane.x + up * inPlane.y;
            vec3 alignedRight = right * inPlane.y - up * inPlane.x;
            right = alignedRight;
            up = alignedUp;
        }
    }

    // i_data6 = spin angle (radians), soft fade distance, frame blend; i_data7.xy = next frame's uv offset.
    // The spin turns the corner around the particle position in the quad's own plane.
    vec2 pivotOffset = pivot - vec2(0.5, 0.5);
    vec2 corner = (a_position.xy + pivotOffset) * scale3d.xy;
    float spinSin = sin(i_data6.x);
    float spinCos = cos(i_data6.x);
    corner = vec2(corner.x * spinCos - corner.y * spinSin, corner.x * spinSin + corner.y * spinCos);
    vec3 localVertexPos = right * corner.x + up * corner.y;
    vec3 vertexWorldPos = worldPos + localVertexPos;

    gl_Position = mul(u_modelViewProj, vec4(vertexWorldPos, 1.0));

    v_texcoord0 = a_texcoord0.xy * uvScale + uvOffset;
    v_texcoord1 = a_texcoord0.xy * uvScale + i_data7.xy;
    // Clip w is the view depth for a perspective projection; the soft fade compares it with the scene's.
    v_particle = vec3(i_data6.z, i_data6.y, gl_Position.w);
    v_color0 = color;
}
