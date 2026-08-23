$input v_depth, v_texcoord0

#include "../../common.sh"
#include "../shadow_cutout.sh"

void main()
{
	shadowCutoutClip(v_texcoord0);
	gl_FragColor = vec4(v_depth, 0.0, 0.0, 0.0);
}
