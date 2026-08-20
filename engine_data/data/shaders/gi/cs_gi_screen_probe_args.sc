/*
 * Converts the classify pass's traced-probe count into the trace's indirect dispatch args.
 * One thread, between classify and trace.
 *
 * TWO arg entries, because the two trace programs pack differently: entry 0 launches one
 * group per traced probe (the full 8x8 program), entry 1 launches ceil(count / 4) groups
 * (the compacted program packs four 16-thread probes into each 64-lane group). The traced
 * COUNT itself rides to the kernel as a plain float in the list's first entry's y lane -
 * a partial final group bounds-checks against it; a value (not a bit pattern) because
 * typed-UAV float traffic may canonicalise NaN payloads, which a bit-cast sentinel would
 * be one driver away from becoming.
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
	dispatchIndirect(b_gi_probe_args, 1u, (count + 3u) / 4u, 1u, 1u);
	uint list_base = GiProbeTracedListBase();
	vec4 head = b_gi_probes[list_base];
	// Whole-vec4 store: the D3D path binds this as a typed UAV, and typed UAV stores must
	// write every component (the same rule the classify pass documents).
	b_gi_probes[list_base] = vec4(head.x, float(count), head.z, head.w);
}
