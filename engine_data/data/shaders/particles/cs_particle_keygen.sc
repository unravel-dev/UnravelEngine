/*
 * Per view: culls every particle slot of the view's GPU-drawn emitters and writes what each draw reads.
 *
 * A task is one emitter of one draw (batch). Its slots [0, high water) map to threads
 * [thread_offset, thread_offset + high water). A sorted batch writes a key and a slot for EVERY thread
 * at a fixed place (out_base + local index): the key holds the batch's ordinal in its top bits, so the one
 * sort of the view groups every batch's particles together, and a dead or culled particle takes its
 * batch's largest key, so it sorts behind that batch's visible ones. An unsorted batch appends only its
 * visible slots. Either way the batch's counter ends as its visible count, which the draw arguments use.
 * Threads past the tasks pad the sorted region to its power of two with keys that sort last.
 */

#include <bgfx_compute.sh>

BUFFER_RO(s_particle_records, uint, 0);
BUFFER_RO(s_particle_static, vec4, 1);
BUFFER_RO(s_particle_dynamic, uint, 2);
BUFFER_RO(s_particle_luts, vec4, 3);
BUFFER_RO(s_particle_tasks, uint, 4);
BUFFER_RW(s_particle_sort, uint, 5);
BUFFER_RW(s_particle_counters, uint, 6);

#include "particle_common.sh"

/// xyz = camera position, w = tasks.
uniform vec4 u_particle_view0;
/// xyz = camera forward, w = threads of the tasks.
uniform vec4 u_particle_view1;
/// x = first padding slot (the sorted region's size), y = padding slots, z = bits the sort key keeps for
/// the distance (32 - batch bits), w = the camera's speed (m/s; stretched particles stretch against it).
uniform vec4 u_particle_view2;
/// The view frustum's planes (outward normals): a point is outside where dot(plane.xyz, p) + plane.w > 0.
uniform vec4 u_particle_frustum[6];

#define PARTICLE_TASK_UINTS 8u
#define PARTICLE_SORT_KIND_APPEND 0u
#define PARTICLE_SORT_KIND_DISTANCE 1u
#define PARTICLE_SORT_KIND_DEPTH 2u
#define PARTICLE_SORT_KIND_OLDEST_FIRST 3u
#define PARTICLE_SORT_KIND_YOUNGEST_FIRST 4u
#define PARTICLE_SORT_KIND_SLOT 5u
#define PARTICLE_SORT_KIND_RIBBON 6u

uint task_value(uint task, uint index)
{
	return s_particle_tasks[task * PARTICLE_TASK_UINTS + index];
}

uint find_task(uint thread_index, uint task_count)
{
	uint low = 0u;
	uint high = task_count - 1u;
	while(low < high)
	{
		uint middle = (low + high + 1u) / 2u;
		if(task_value(middle, 2u) <= thread_index)
		{
			low = middle;
		}
		else
		{
			high = middle - 1u;
		}
	}
	return low;
}

bool is_inside_frustum(vec3 position, float radius)
{
	for(uint plane = 0u; plane < 6u; ++plane)
	{
		if(dot(u_particle_frustum[plane].xyz, position) + u_particle_frustum[plane].w > radius)
		{
			return false;
		}
	}
	return true;
}

/// Threads in one row of a dispatch (particle_gpu.cpp, dispatch_threads): 65535 groups of 64.
#define PARTICLE_DISPATCH_ROW_THREADS (65535u * 64u)

/// Ascending order is draw order: the first key draws first, so far particles get small keys.
uint distance_order(float distance_squared)
{
	return ~floatBitsToUint(max(distance_squared, 0.0));
}

NUM_THREADS(64, 1, 1)
void main()
{
	uint thread_index = gl_GlobalInvocationID.y * PARTICLE_DISPATCH_ROW_THREADS + gl_GlobalInvocationID.x;
	uint task_threads = uint(u_particle_view1.w);
	uint distance_bits = uint(u_particle_view2.z);
	uint low_mask = distance_bits >= 32u ? 0xffffffffu : ((1u << distance_bits) - 1u);
	if(thread_index >= task_threads)
	{
		uint pad = thread_index - task_threads;
		if(pad < uint(u_particle_view2.y))
		{
			uint index = uint(u_particle_view2.x) + pad;
			s_particle_sort[index * 2u] = 0xffffffffu;
			s_particle_sort[index * 2u + 1u] = 0u;
		}
		return;
	}
	uint task = find_task(thread_index, uint(u_particle_view0.w));
	uint local_index = thread_index - task_value(task, 2u);
	uint slot = task_value(task, 0u) + local_index;
	uint out_base = task_value(task, 3u);
	uint batch = task_value(task, 4u);
	uint sort_kind = task_value(task, 5u);
	uint batch_key = task_value(task, 6u);
	particle_record record = particle_load(slot);
	// Culled and sorted where it draws, as the vertex shader places it.
	particle_state state = particle_place(record);
	bool is_visible = state.is_alive;
	// A ribbon connects every live particle: none is culled on its own.
	if(is_visible && sort_kind != PARTICLE_SORT_KIND_RIBBON)
	{
		float radius = particle_cull_radius(record, state) + particle_camera_stretch_reach(record, state, u_particle_view2.w);
		is_visible = is_inside_frustum(state.position, radius) &&
		             particle_is_within_draw_distance(record, state, radius, u_particle_view0.xyz);
	}
	if(sort_kind == PARTICLE_SORT_KIND_APPEND)
	{
		if(is_visible)
		{
			uint index;
			atomicFetchAndAdd(s_particle_counters[batch], 1u, index);
			s_particle_sort[(out_base + index) * 2u] = 0u;
			s_particle_sort[(out_base + index) * 2u + 1u] = slot;
		}
		return;
	}
	uint order = 0xffffffffu;
	if(is_visible)
	{
		vec3 to_particle = state.position - u_particle_view0.xyz;
		uint age_ticks = particle_dynamic_uint(record.emitter, PARTICLE_DYNAMIC_NOW) - record.birth;
		if(sort_kind == PARTICLE_SORT_KIND_DISTANCE)
		{
			order = distance_order(dot(to_particle, to_particle));
		}
		else if(sort_kind == PARTICLE_SORT_KIND_DEPTH)
		{
			order = distance_order(dot(to_particle, u_particle_view1.xyz));
		}
		// Every distance order is a 32-bit value whose top distance_bits bits are kept. Ages and slots keep their
		// low bits instead: exact while they fit (2^24 ticks is over half an hour), so a ribbon's order never ties.
		order = order >> (32u - distance_bits);
		uint age = min(age_ticks, low_mask - 1u);
		if(sort_kind == PARTICLE_SORT_KIND_OLDEST_FIRST || sort_kind == PARTICLE_SORT_KIND_RIBBON)
		{
			order = low_mask - 1u - age;
		}
		else if(sort_kind == PARTICLE_SORT_KIND_YOUNGEST_FIRST)
		{
			order = age;
		}
		else if(sort_kind == PARTICLE_SORT_KIND_SLOT)
		{
			order = slot & low_mask;
		}
		// Keep one step below the mask: the mask itself marks the dead and culled.
		order = min(order, low_mask - 1u);
		atomicAdd(s_particle_counters[batch], 1u);
	}
	else
	{
		order = low_mask;
	}
	uint index = out_base + local_index;
	s_particle_sort[index * 2u] = batch_key | order;
	s_particle_sort[index * 2u + 1u] = slot;
}
