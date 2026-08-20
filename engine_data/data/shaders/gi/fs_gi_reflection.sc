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
	// CHECKERBOARD: half the texels trace per frame, by pixel parity against the frame
	// parity; the temporal pass fills the untraced half from clamped reprojected history
	// (or from the traced diagonal neighbours when there is none). The running mean counts
	// only traced frames, so the converged result is the same stochastic integral arriving
	// at half rate over a window twice as long - identical in expectation, unchanged
	// steady-state variance per accumulated sample.
	BRANCH
	if(u_gi_reflection_jitter.z > 0.0)
	{
		int chequer =
		    (int(gl_FragCoord.x) + int(gl_FragCoord.y) + int(u_gi_reflection_jitter.w)) & 1;
		if(chequer != 0)
		{
			gl_FragColor = vec4_splat(0.0);
			return;
		}
	}
	gl_FragColor = GiReflectionShade(v_texcoord0, gl_FragCoord.xy);
}
