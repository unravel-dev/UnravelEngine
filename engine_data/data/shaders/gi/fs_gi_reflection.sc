$input v_texcoord0

/*
 * FRAGMENT form of the GI reflection trace - the fallback when the compute chain
 * (cs_gi_reflection_classify / _args / _trace) is unavailable. Shared body:
 * gi_reflection_kernel.sh, where the whole tiering/tracing story is documented.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_reflection_kernel.sh"

void main()
{
	gl_FragColor = GiReflectionShade(v_texcoord0, gl_FragCoord.xy);
}
