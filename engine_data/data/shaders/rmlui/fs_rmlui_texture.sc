$input v_texcoord0, v_color0

#include "../common.sh"

SAMPLER2D(s_tex, 0);

void main()
{
    vec4 texColor = texture2D(s_tex, v_texcoord0);
    gl_FragColor = v_color0 * texColor;
}
