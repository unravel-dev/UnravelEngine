/*
 * Screen probes, the per-probe dispatch arguments: the probe atlas rows that hold this frame's probes, the
 * uniform probes' and the adaptive probes' spawned within the capacity, as
 *  0 one group per probe (generate rays, trace, composite, filter, SH);
 *  1 8x8 thread groups over the bordered radiance texels (border), u_gi_adaptive_border_res per probe and axis;
 *  2 8x8 thread groups over the probes.
 * Slots past the last probe of the last row hold records without probe; the passes skip them.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"
#include "gi/gi_adaptive_probes.sh"

BUFFER_RW(b_gi_adaptive, uint, 0);
BUFFER_RW(b_gi_probe_args, uvec4, 1);

NUM_THREADS(1, 1, 1)
void main()
{
	uint probes_x = uint(u_gi_probe_count.x);
	uint adaptive = min(b_gi_adaptive[GI_ADAPTIVE_COUNTER], u_gi_adaptive_capacity);
	uint probes = probes_x * uint(u_gi_probe_count.y) + adaptive;
	uint rows = (probes + probes_x - 1u) / probes_x;
	uint border_res = u_gi_adaptive_border_res;
	dispatchIndirect(b_gi_probe_args, 0u, probes_x, rows, 1u);
	dispatchIndirect(b_gi_probe_args, 1u, (probes_x * border_res + 7u) / 8u, (rows * border_res + 7u) / 8u, 1u);
	dispatchIndirect(b_gi_probe_args, 2u, (probes_x + 7u) / 8u, (rows + 7u) / 8u, 1u);
}
