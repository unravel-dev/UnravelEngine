/*
 * One pass of a global SDF level's coarse mip (UE 5.8 PropagateMipDistanceCS, GlobalDistanceFieldMip.usf:83-110;
 * GlobalDistanceField.cpp:3106-3178), one thread per mip texel.
 *
 * Mip texel c of a level covers its voxels [c, c + 1) x SDF_CLIPMAP_MIP_FACTOR. The first pass reads the level at
 * every texel's centre (trilinear across the eight voxels there); a reading short of the level's encode range becomes
 * that distance in the mip's encoding, a saturated one "far" (1). Every pass then lowers each texel to the eikonal
 * solution from its six neighbours, one mip texel apart: UE runs five passes, which carries the distance
 * SDF_CLIPMAP_MIP_PROPAGATION_PASSES mip texels away from the surfaces the level holds. The mip therefore holds
 * exactly the level's own objects, so stepping by it never skips one the level shows.
 *
 * Encoding: normalised n, distance = (n - 0.5) x 2 x encode range x SDF_CLIPMAP_MIP_FACTOR level voxels; one mip texel
 * is 1 / (2 x encode range) of it (UE Step = 1 / (2 x GLOBAL_DISTANCE_FIELD_INFLUENCE_RANGE_IN_VOXELS)).
 */

#include "bgfx_compute.sh"
#include "gi/sdf_clipmap.sh"

/// The previous pass's mip texels (the scratch or the level's slab of the mip).
SAMPLER3D(s_clipmap_mip_prev, 1);
IMAGE3D_WO(i_clipmap_mip_out, r8, 2);

/// x = level index, y = mip texels per axis, z = the source slab's first z in s_clipmap_mip_prev, w = the
/// destination slab's first z in i_clipmap_mip_out.
uniform vec4 u_clipmap_mip_params;
#define u_mip_level        int(u_clipmap_mip_params.x)
#define u_mip_resolution   int(u_clipmap_mip_params.y)
#define u_mip_source_z     int(u_clipmap_mip_params.z)
#define u_mip_destination_z int(u_clipmap_mip_params.w)
/// x > 0: the first pass, which reads the level (s_sdf_clipmap) instead of a previous pass.
uniform vec4 u_clipmap_mip_mode;
#define u_mip_reads_level (u_clipmap_mip_mode.x > 0.0)

/// A level reading at or above this is saturated: nothing within the encode range.
#define SDF_CLIPMAP_MIP_SATURATED 0.999

float Eikonal1(float x, float step_size)
{
	return x + step_size;
}

/// The two- and three-neighbour updates have no solution where the neighbours differ by more than the step allows;
/// they then answer SDF_CLIPMAP_MIP_NO_SOLUTION, which the minimum ignores (UE's NaN, dropped by min on D3D).
#define SDF_CLIPMAP_MIP_NO_SOLUTION 1e6

float Eikonal2(float x, float y, float step_size)
{
	float sum_u = x + y;
	float sum_u_sq = x * x + y * y;
	float distance_sq = sum_u * sum_u - 2.0 * (sum_u_sq - step_size * step_size);
	return distance_sq >= 0.0 ? 0.5 * (sum_u + sqrt(max(distance_sq, 0.0))) : SDF_CLIPMAP_MIP_NO_SOLUTION;
}

float Eikonal3(float x, float y, float z, float step_size)
{
	float sum_u = x + y + z;
	float sum_u_sq = x * x + y * y + z * z;
	float distance_sq = sum_u * sum_u - 3.0 * (sum_u_sq - step_size * step_size);
	return distance_sq >= 0.0 ? (1.0 / 3.0) * (sum_u + sqrt(max(distance_sq, 0.0))) : SDF_CLIPMAP_MIP_NO_SOLUTION;
}

/// The level read at mip texel @p texel's centre, in the mip's encoding.
float LoadLevel(ivec3 texel)
{
	float resolution = u_sdf_clipmap_resolution;
	vec3 grid = clamp((vec3(texel) + vec3_splat(0.5)) * float(SDF_CLIPMAP_MIP_FACTOR),
	                  vec3_splat(0.5),
	                  vec3_splat(resolution - 0.5));
	vec3 uvw = vec3(grid.x / resolution, grid.y / resolution, (grid.z + float(u_mip_level) * resolution) / u_sdf_clipmap_depth);
	float encoded = texture3DLod(s_sdf_clipmap, uvw, 0.0).x;
	float scale = 1.0 / float(SDF_CLIPMAP_MIP_FACTOR);
	return encoded < SDF_CLIPMAP_MIP_SATURATED ? encoded * scale + (0.5 - 0.5 * scale) : 1.0;
}

float LoadPrevious(ivec3 texel, ivec3 offset)
{
	int last = u_mip_resolution - 1;
	ivec3 c = clamp(texel + offset, ivec3(0, 0, 0), ivec3(last, last, last));
	BRANCH
	if(u_mip_reads_level)
	{
		return LoadLevel(c);
	}
	return texelFetch(s_clipmap_mip_prev, c + ivec3(0, 0, u_mip_source_z), 0).x;
}

NUM_THREADS(4, 4, 4)
void main()
{
	ivec3 texel = ivec3(gl_GlobalInvocationID.xyz);
	if(any(greaterThanEqual(texel, ivec3(u_mip_resolution, u_mip_resolution, u_mip_resolution))))
	{
		return;
	}
	float center = LoadPrevious(texel, ivec3(0, 0, 0));
	vec3 v = vec3(min(LoadPrevious(texel, ivec3(-1, 0, 0)), LoadPrevious(texel, ivec3(1, 0, 0))),
	              min(LoadPrevious(texel, ivec3(0, -1, 0)), LoadPrevious(texel, ivec3(0, 1, 0))),
	              min(LoadPrevious(texel, ivec3(0, 0, -1)), LoadPrevious(texel, ivec3(0, 0, 1))));
	float min_v = min(v.x, min(v.y, v.z));
	float max_v = max(v.x, max(v.y, v.z));
	float mid_v = v.x + v.y + v.z - min_v - max_v;
	float step_size = 1.0 / (2.0 * u_sdf_clipmap_encode_range);
	center = min(center, Eikonal1(min_v, step_size));
	center = min(center, Eikonal2(min_v, mid_v, step_size));
	center = min(center, Eikonal3(min_v, mid_v, max_v, step_size));
	imageStore(i_clipmap_mip_out, texel + ivec3(0, 0, u_mip_destination_z), vec4_splat(saturate(center)));
}
