$input v_texcoord0, v_color0

#include "../common.sh"

SAMPLER2D(s_tex, 0);

uniform vec4 u_tex_requires_premultiplication;

void main()
{
    vec4 texColor = texture2D(s_tex, v_texcoord0);
    if(u_tex_requires_premultiplication.x > 0.5)
    {
        texColor.rgb *= texColor.a;
    }
    gl_FragColor = v_color0 * texColor;
}
