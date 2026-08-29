/*
 * Converts the classify pass's traced-probe count into the trace's indirect dispatch args.
 * One thread, between classify and trace. One entry: one 8x8 group per traced probe.
 *
 * The X group-count limit is 65535 on the D3D11 class of backends - every lattice up to a
 * 4K full-resolution trace target (32.4k probes) fits with headroom; revisit the packing
 * (fold into Y) if a larger lattice ever appears.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"

BUFFER_RO(b_gi_probe_traced, uint, 6);
BUFFER_RW(b_gi_probe_args, uvec4, 5);
BUFFER_RW(b_gi_probes, vec4, 7);

NUM_THREADS(1, 1, 1)
void main()
{
	uint count = b_gi_probe_traced[0];
	dispatchIndirect(b_gi_probe_args, 0u, count, 1u, 1u);
}
