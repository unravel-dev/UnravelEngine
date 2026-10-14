/*
 * Turns every batch's visible count into its indexed indirect draw (one instance per particle, the
 * batch's geometry from the batch table) and zeroes the counter for the next view.
 */

#include <bgfx_compute.sh>

/// uints per batch of s_particle_batches (gpu_batch_uints).
#define PARTICLE_BATCH_UINTS 2u

BUFFER_RW(s_particle_counters, uint, 0);
BUFFER_WO(s_particle_draw_args, uvec4, 1);
/// Per batch: index count, first index.
BUFFER_RO(s_particle_batches, uint, 2);
/// Per batch: its live count, for the vertex shaders (ribbons read their neighbours within it).
BUFFER_WO(s_particle_batch_counts, uint, 3);

/// x = batches.
uniform vec4 u_particle_args;

NUM_THREADS(64, 1, 1)
void main()
{
	uint batch = gl_GlobalInvocationID.x;
	if(batch >= uint(u_particle_args.x))
	{
		return;
	}
	uint count = s_particle_counters[batch];
	s_particle_counters[batch] = 0u;
	s_particle_batch_counts[batch] = count;
	uint index_count = s_particle_batches[batch * PARTICLE_BATCH_UINTS];
	uint first_index = s_particle_batches[batch * PARTICLE_BATCH_UINTS + 1u];
	// The layout of drawIndexedIndirect (bgfx_compute.sh), without its argument base: the particle index
	// buffer is a standalone static buffer, so first_index is absolute.
	s_particle_draw_args[batch * 2u] = uvec4(index_count, count, first_index, 0u);
	s_particle_draw_args[batch * 2u + 1u] = uvec4(0u, 0u, 0u, 0u);
}
