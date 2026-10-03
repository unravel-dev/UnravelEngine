/*
 * Lumen screen probes, the per-probe dispatch arguments (UE 5.8 SetupAdaptiveProbeIndirectArgsCS,
 * LumenScreenProbeGather.usf:318-335): the probe atlas rows that hold this frame's probes, the uniform probes' and
 * the adaptive probes' spawned within the capacity, as
 *  0 one group per probe (generate rays, trace, composite, filter, SH);
 *  1 8x8 thread groups over the bordered radiance texels (border);
 *  2 8x8 thread groups over the probes.
 * Slots past the last probe of the last row hold records without probe; the passes skip them.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_adaptive_probes.sh"

BUFFER_RW(b_lumen_adaptive, uint, 0);
BUFFER_RW(b_lumen_probe_args, uvec4, 1);

#define LUMEN_PROBE_BORDER_RES (LUMEN_PROBE_TRACE_RES + 2 * LUMEN_PROBE_RADIANCE_BORDER)

NUM_THREADS(1, 1, 1)
void main()
{
	uint probes_x = uint(u_lumen_probe_count.x);
	uint adaptive = min(b_lumen_adaptive[LUMEN_ADAPTIVE_COUNTER], u_lumen_adaptive_capacity);
	uint probes = probes_x * uint(u_lumen_probe_count.y) + adaptive;
	uint rows = (probes + probes_x - 1u) / probes_x;
	uint border_res = uint(LUMEN_PROBE_BORDER_RES);
	dispatchIndirect(b_lumen_probe_args, 0u, probes_x, rows, 1u);
	dispatchIndirect(b_lumen_probe_args, 1u, (probes_x * border_res + 7u) / 8u, (rows * border_res + 7u) / 8u, 1u);
	dispatchIndirect(b_lumen_probe_args, 2u, (probes_x + 7u) / 8u, (rows + 7u) / 8u, 1u);
}
