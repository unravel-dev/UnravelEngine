/*
 * Resets ONE cascade level's surface-list append cursor, dispatched immediately before that
 * level's cs_gi_clipmap_attributes pass. A dedicated dispatch rather than a CPU buffer update
 * so the ordering guarantee is the view's own submission order and nothing else.
 */

#include "bgfx_compute.sh"

BUFFER_RW(b_surface_count, uint, 8);

/// x = level index whose cursor resets.
uniform vec4 u_surface_reset_params;

NUM_THREADS(1, 1, 1)
void main()
{
	b_surface_count[uint(u_surface_reset_params.x)] = 0u;
}
