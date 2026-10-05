/*
 * The dispatch arguments of the probe trace's far-field pass (UE 5.8 CompactTraces' indirect arguments):
 * ceil(rays / LUMEN_TRACE_FAR_FIELD_GROUP) groups over the rays the screen pass left (cs_lumen_probe_trace.sc), in
 * rows of LUMEN_TRACE_FAR_FIELD_ROW_GROUPS.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"

BUFFER_RO(b_lumen_trace_rays, uint, 0);
BUFFER_RW(b_lumen_far_field_args, uvec4, 1);

NUM_THREADS(1, 1, 1)
void main()
{
	uint group_size = uint(LUMEN_TRACE_FAR_FIELD_GROUP);
	uint groups = (b_lumen_trace_rays[0] + group_size - 1u) / group_size;
	uint row = min(groups, uint(LUMEN_TRACE_FAR_FIELD_ROW_GROUPS));
	uint rows = row > 0u ? (groups + row - 1u) / row : 0u;
	dispatchIndirect(b_lumen_far_field_args, 0u, row, rows, 1u);
}
