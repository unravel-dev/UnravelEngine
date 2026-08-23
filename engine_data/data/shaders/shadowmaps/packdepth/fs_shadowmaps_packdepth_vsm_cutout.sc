$input v_position, v_texcoord0

#include "../../common.sh"
#include "../shadow_cutout.sh"

void main()
{
	shadowCutoutClip(v_texcoord0);
	float depth = v_position.z / v_position.w * 0.5 + 0.5;
	float depthSq = depth * depth;
	gl_FragColor = vec4(depth, depthSq, 0.0, 0.0);
}
