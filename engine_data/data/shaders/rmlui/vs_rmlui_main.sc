$input a_position, a_color0, a_texcoord0
$output v_texcoord0, v_color0

#include "../common.sh"

uniform vec4 u_translate;
uniform mat4 u_transform;

void main()
{
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;

    vec2 translatedPos = a_position.xy + u_translate.xy;
    vec4 outPos = mul(u_transform, vec4(translatedPos, 0.0, 1.0));

    gl_Position = outPos;
}
