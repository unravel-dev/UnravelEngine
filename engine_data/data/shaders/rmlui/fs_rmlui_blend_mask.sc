$input v_texcoord0

#include "../common.sh"

SAMPLER2D(s_tex, 0);
SAMPLER2D(s_texMask, 1);

void main()
{
    vec4 texColor = texture2D(s_tex, v_texcoord0);
    float maskAlpha = texture2D(s_texMask, v_texcoord0).a;
    gl_FragColor = texColor * maskAlpha;
}
