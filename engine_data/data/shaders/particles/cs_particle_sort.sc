/*
 * Bitonic sort of (key, value) pairs, ascending by key and then by value, over a power-of-two count.
 * Interleaved in one buffer: key 2i, value 2i + 1. The values (pool slots) are unique, so the order is
 * total: equal keys (particles born on the same tick, at the same distance) draw the same way every
 * frame, as the CPU sort orders them.
 *
 * Every step of the network whose compare distance fits in one block of PARTICLE_SORT_BLOCK elements
 * runs in groupshared memory, so a sort of n keys takes one dispatch up to a block, and beyond it one
 * dispatch per global step plus one block dispatch per stage:
 *   mode 0 (block)  : sorts each block completely (every stage up to the block size);
 *   mode 1 (global) : one compare-exchange step of stage `stage` at distance `distance` >= the block;
 *   mode 2 (inner)  : the remaining steps (distance < the block) of stage `stage` within each block.
 */

#include <bgfx_compute.sh>

#define PARTICLE_SORT_THREADS 512u
#define PARTICLE_SORT_BLOCK 1024u

BUFFER_RW(s_particle_sort, uint, 0);

/// x = mode, y = stage size, z = compare distance (global mode).
uniform vec4 u_particle_sort;

SHARED uint shared_keys[PARTICLE_SORT_BLOCK];
SHARED uint shared_values[PARTICLE_SORT_BLOCK];

void load_block(uint block_start, uint thread_index)
{
	for(uint k = 0u; k < 2u; ++k)
	{
		uint local_index = thread_index + k * PARTICLE_SORT_THREADS;
		uint global_index = block_start + local_index;
		shared_keys[local_index] = s_particle_sort[global_index * 2u];
		shared_values[local_index] = s_particle_sort[global_index * 2u + 1u];
	}
}

void store_block(uint block_start, uint thread_index)
{
	for(uint k = 0u; k < 2u; ++k)
	{
		uint local_index = thread_index + k * PARTICLE_SORT_THREADS;
		uint global_index = block_start + local_index;
		s_particle_sort[global_index * 2u] = shared_keys[local_index];
		s_particle_sort[global_index * 2u + 1u] = shared_values[local_index];
	}
}

/// True when the pair (@p key_a, @p value_a) sorts after (@p key_b, @p value_b).
bool is_after(uint key_a, uint value_a, uint key_b, uint value_b)
{
	return key_a > key_b || (key_a == key_b && value_a > value_b);
}

/// One compare-exchange of the pair the thread owns at @p distance within stage @p stage.
void exchange_shared(uint block_start, uint thread_index, uint stage, uint distance)
{
	uint low = 2u * distance * (thread_index / distance) + (thread_index % distance);
	uint high = low + distance;
	bool is_ascending = ((block_start + low) & stage) == 0u;
	uint low_key = shared_keys[low];
	uint high_key = shared_keys[high];
	uint low_value = shared_values[low];
	uint high_value = shared_values[high];
	bool is_low_after = is_after(low_key, low_value, high_key, high_value);
	bool is_high_after = is_after(high_key, high_value, low_key, low_value);
	if(is_ascending ? is_low_after : is_high_after)
	{
		shared_keys[low] = high_key;
		shared_keys[high] = low_key;
		shared_values[low] = high_value;
		shared_values[high] = low_value;
	}
}

NUM_THREADS(512, 1, 1)
void main()
{
	uint mode = uint(u_particle_sort.x);
	uint stage = uint(u_particle_sort.y);
	uint distance = uint(u_particle_sort.z);
	uint thread_index = gl_LocalInvocationID.x;
	uint block_start = gl_WorkGroupID.x * PARTICLE_SORT_BLOCK;
	if(mode == 1u)
	{
		uint pair = gl_GlobalInvocationID.x;
		uint low = 2u * distance * (pair / distance) + (pair % distance);
		uint high = low + distance;
		bool is_ascending = (low & stage) == 0u;
		uint low_key = s_particle_sort[low * 2u];
		uint high_key = s_particle_sort[high * 2u];
		uint low_value = s_particle_sort[low * 2u + 1u];
		uint high_value = s_particle_sort[high * 2u + 1u];
		bool is_low_after = is_after(low_key, low_value, high_key, high_value);
		bool is_high_after = is_after(high_key, high_value, low_key, low_value);
		if(is_ascending ? is_low_after : is_high_after)
		{
			s_particle_sort[low * 2u] = high_key;
			s_particle_sort[high * 2u] = low_key;
			s_particle_sort[low * 2u + 1u] = high_value;
			s_particle_sort[high * 2u + 1u] = low_value;
		}
		return;
	}
	load_block(block_start, thread_index);
	barrier();
	if(mode == 0u)
	{
		for(uint block_stage = 2u; block_stage <= PARTICLE_SORT_BLOCK; block_stage <<= 1u)
		{
			for(uint step = block_stage >> 1u; step > 0u; step >>= 1u)
			{
				exchange_shared(block_start, thread_index, block_stage, step);
				barrier();
			}
		}
	}
	else
	{
		for(uint step = PARTICLE_SORT_BLOCK >> 1u; step > 0u; step >>= 1u)
		{
			exchange_shared(block_start, thread_index, stage, step);
			barrier();
		}
	}
	store_block(block_start, thread_index);
}
