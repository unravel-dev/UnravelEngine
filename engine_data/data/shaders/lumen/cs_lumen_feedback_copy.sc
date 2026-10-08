/*
 * Lumen surface cache feedback, readback copy: the feedback hash table (cs_lumen_reflection_feedback.sc) into a
 * read-back image, one texel per slot: x = the element (0 for an empty slot), y = its count. The CPU decodes it
 * (lumen_surface_cache_pass) a few frames later.
 */

#include "bgfx_compute.sh"

BUFFER_RO(b_lumen_feedback_keys, uint, 0);
BUFFER_RO(b_lumen_feedback_counts, uint, 1);
UIMAGE2D_WO(i_lumen_feedback_readback, rg32ui, 2);

/// x = the table's slots, y = the image's width.
uniform vec4 u_lumen_feedback_copy;

NUM_THREADS(64, 1, 1)
void main()
{
	uint slot = gl_GlobalInvocationID.x;
	if(slot >= uint(u_lumen_feedback_copy.x))
	{
		return;
	}
	uint width = uint(u_lumen_feedback_copy.y);
	ivec2 texel = ivec2(int(slot % width), int(slot / width));
	imageStore(i_lumen_feedback_readback, texel, uvec4(b_lumen_feedback_keys[slot], b_lumen_feedback_counts[slot], 0u, 0u));
}
