$input v_texcoord0

#include "common.sh"

SAMPLER2D(s_input, 0);

void main()
{
	gl_FragColor = texture2D(s_input, v_texcoord0.xy);
}
