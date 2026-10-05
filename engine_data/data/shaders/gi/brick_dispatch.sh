#ifndef __GI_BRICK_DISPATCH_SH__
#define __GI_BRICK_DISPATCH_SH__

/*
 * A dispatch over a list of voxel boxes in bricks of 4 x 4 x 4 voxels (global_sdf_clipmap::append_brick_boxes): one
 * NUM_THREADS(4, 4, 4) group per brick, the groups numbered row by row, a box's bricks x fastest. One dispatch serves
 * every box of a level, where a dispatch per box would serialize on the volume they all write.
 *
 * The includer defines BRICK_DISPATCH_STAGE, the stage of b_brick_boxes (two vec4s per box: xyz = its first voxel,
 * w = its first brick; xyz = its size in voxels).
 */

BUFFER_RO(b_brick_boxes, vec4, BRICK_DISPATCH_STAGE);
/// x = the dispatch's first box in b_brick_boxes, y = its boxes, z = its bricks, w = groups per dispatch row.
uniform vec4 u_brick_dispatch;

#define BRICK_DISPATCH_EDGE 4

/// The voxel of thread @p local of group @p group, and whether it lies in a box.
struct BrickDispatchVoxel
{
	ivec3 voxel;
	bool valid;
};

BrickDispatchVoxel BrickDispatchFindVoxel(uvec3 group, uvec3 local)
{
	BrickDispatchVoxel result;
	result.voxel = ivec3(0, 0, 0);
	result.valid = false;
	uint brick = group.y * uint(u_brick_dispatch.w) + group.x;
	if(brick >= uint(u_brick_dispatch.z))
	{
		return result;
	}
	int first_box = int(u_brick_dispatch.x);
	int box_count = int(u_brick_dispatch.y);
	// The last box whose first brick is at or before this one.
	int box = first_box;
	for(int i = 1; i < box_count; ++i)
	{
		if(uint(b_brick_boxes[2 * (first_box + i)].w) <= brick)
		{
			box = first_box + i;
		}
	}
	vec4 box_min = b_brick_boxes[2 * box];
	ivec3 box_size = ivec3(b_brick_boxes[2 * box + 1].xyz);
	int round_up = BRICK_DISPATCH_EDGE - 1;
	ivec3 bricks = (box_size + ivec3(round_up, round_up, round_up)) / BRICK_DISPATCH_EDGE;
	int local_brick = int(brick - uint(box_min.w));
	ivec3 brick_coord = ivec3(local_brick % bricks.x, (local_brick / bricks.x) % bricks.y, local_brick / (bricks.x * bricks.y));
	ivec3 offset = brick_coord * BRICK_DISPATCH_EDGE + ivec3(local);
	result.voxel = ivec3(box_min.xyz) + offset;
	result.valid = all(lessThan(offset, box_size));
	return result;
}

#endif // __GI_BRICK_DISPATCH_SH__
