$input v_depth, v_texcoord0

#include "../../common.sh"
#include "../shadow_cutout.sh"

void main()
{
	shadowCutoutClip(v_texcoord0);
	gl_FragColor = packFloatToRgba(v_depth);
}
