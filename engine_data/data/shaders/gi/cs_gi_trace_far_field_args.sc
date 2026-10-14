/*
 * The indirect dispatch arguments of the probe trace's far-field and hit passes:
 * ceil(count / GI_TRACE_FAR_FIELD_GROUP) groups, in rows of GI_TRACE_FAR_FIELD_ROW_GROUPS, over a list whose
 * first uint counts it: the rays the screen pass left, or the hits the far-field pass left (cs_gi_probe_trace.sc).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"

BUFFER_RO(b_gi_trace_list, uint, 0);
BUFFER_RW(b_gi_trace_args, uvec4, 1);

NUM_THREADS(1, 1, 1)
void main()
{
	uint group_size = uint(GI_TRACE_FAR_FIELD_GROUP);
	uint groups = (b_gi_trace_list[0] + group_size - 1u) / group_size;
	uint row = min(groups, uint(GI_TRACE_FAR_FIELD_ROW_GROUPS));
	uint rows = row > 0u ? (groups + row - 1u) / row : 0u;
	dispatchIndirect(b_gi_trace_args, 0u, row, rows, 1u);
}
