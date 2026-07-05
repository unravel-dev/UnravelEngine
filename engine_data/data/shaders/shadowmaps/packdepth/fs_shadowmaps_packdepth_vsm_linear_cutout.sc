$input v_depth, v_texcoord0

#include "../../common.sh"
#include "../shadow_cutout.sh"

void main()
{
	shadowCutoutClip(v_texcoord0);
	float depthSq = v_depth * v_depth;
	gl_FragColor = vec4(packHalfFloat(v_depth), packHalfFloat(depthSq));
}
