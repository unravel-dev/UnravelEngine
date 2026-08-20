$input v_texcoord0

/*
 * SPLIT form of the GI integration - the fallback when the fused integrate+temporal
 * program (fs_gi_probe_integrate_temporal.sc) is unavailable, and the form that runs when
 * temporal accumulation is disabled outright. Shared body: gi_probe_integrate_kernel.sh.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_probe_integrate_kernel.sh"

void main()
{
	float depth_unused;
	vec3 world_position_unused;
	gl_FragColor = GiIntegrateGather(v_texcoord0, gl_FragCoord.xy, depth_unused, world_position_unused);
}
