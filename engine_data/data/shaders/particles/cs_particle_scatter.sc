/*
 * Writes the particles spawned since the last upload into their pool slots. The staging buffer holds
 * `count` records (PARTICLE_RECORD_UINTS uints each) followed by `count` destination slots.
 */

#include <bgfx_compute.sh>

#define PARTICLE_RECORD_UINTS 16u

BUFFER_RO(s_particle_staging, uint, 0);
BUFFER_WO(s_particle_records, uint, 1);

/// x = records staged.
uniform vec4 u_particle_scatter;

/// Threads in one row of a dispatch (particle_gpu.cpp, dispatch_threads): 65535 groups of 64.
#define PARTICLE_DISPATCH_ROW_THREADS (65535u * 64u)

NUM_THREADS(64, 1, 1)
void main()
{
	uint index = gl_GlobalInvocationID.y * PARTICLE_DISPATCH_ROW_THREADS + gl_GlobalInvocationID.x;
	uint count = uint(u_particle_scatter.x);
	if(index >= count)
	{
		return;
	}
	uint destination = s_particle_staging[count * PARTICLE_RECORD_UINTS + index] * PARTICLE_RECORD_UINTS;
	uint source = index * PARTICLE_RECORD_UINTS;
	for(uint k = 0u; k < PARTICLE_RECORD_UINTS; ++k)
	{
		s_particle_records[destination + k] = s_particle_staging[source + k];
	}
}
