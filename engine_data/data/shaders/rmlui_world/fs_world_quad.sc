$input v_texcoord0

#include "common.sh"

SAMPLER2D(s_tex, 0);

void main()
{
    vec4 texColor = texture2D(s_tex, v_texcoord0);
    if (texColor.a < 0.01)
        discard;
    gl_FragColor = texColor;
}
