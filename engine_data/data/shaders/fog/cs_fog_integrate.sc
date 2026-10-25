/*
 * Integrates each froxel column of the volumetric fog front to back (fog_pass), with Hillaire's energy-conserving
 * slab integral: a slab of scattering S (per metre) and extinction sigma over d metres adds S (1 - exp(-sigma d)) /
 * sigma, dimmed by the transmittance in front of it. Slab lengths run along each column's ray, not along the view
 * axis. Texel k holds the column from the camera to the far side of slice k: rgb = pre-exposed in-scatter,
 * a = transmittance.
 */

#include "../bgfx_compute.sh"

IMAGE3D_WO(i_fog_integrated, rgba16f, 0);
// This frame's lit froxels with the point and spot lights (cs_fog_lighting.sc i_fog_scatter).
SAMPLER3D(s_fog_lit, 1);

#include "fog.sh"
#include "fog_volume.sh"

/// Below this extinction (1/m) a slab adds its scattering times its length.
#define FOG_MIN_EXTINCTION 1e-7

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 column = ivec2(gl_GlobalInvocationID.xy);
	if(float(column.x) >= u_fog_grid.x || float(column.y) >= u_fog_grid.y)
	{
		return;
	}
	// The ray at view depth 1 is as long as a metre of view depth is along it.
	float ray_scale = length(fog_grid_ray((vec2(column) + 0.5) / u_fog_grid.xy));
	int slices = int(u_fog_grid.z);
	vec3 inscatter = vec3_splat(0.0);
	float transmittance = 1.0;
	float near_depth = 0.0;
	for(int slice = 0; slice < slices; ++slice)
	{
		float far_depth = fog_slice_depth(float(slice + 1) / float(slices));
		float slab_length = (far_depth - near_depth) * ray_scale;
		vec4 froxel = texelFetch(s_fog_lit, ivec3(column, slice), 0);
		float slab_transmittance = exp(-froxel.a * slab_length);
		vec3 slab_inscatter = froxel.a > FOG_MIN_EXTINCTION ? froxel.rgb * ((1.0 - slab_transmittance) / froxel.a)
		                                                    : froxel.rgb * slab_length;
		inscatter += transmittance * slab_inscatter;
		transmittance *= slab_transmittance;
		imageStore(i_fog_integrated, ivec3(column, slice), vec4(min(inscatter, vec3_splat(FOG_HALF_MAX)), transmittance));
		near_depth = far_depth;
	}
}
