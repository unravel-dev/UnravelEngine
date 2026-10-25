#ifndef FOG_LIGHTS_SH_HEADER_GUARD
#define FOG_LIGHTS_SH_HEADER_GUARD

/*
 * The point and spot lights the volumetric fog scatters (fog_pass), shared by the light culling (cs_fog_cull.sc) and
 * the froxel lighting (cs_fog_lighting.sc). The grid splits into clusters of FOG_CLUSTER_COLUMNS x FOG_CLUSTER_COLUMNS
 * froxels by FOG_CLUSTER_SLICES slices; the culling writes, per cluster, a mask of the lights whose range reaches
 * into it, and the lighting reads only those. Includers include fog_volume.sh (u_fog_grid) first.
 */

/// Point and spot lights of u_fog_lights (fog_pass::max_local_lights): one bit each in a cluster's mask.
#define FOG_MAX_LOCAL_LIGHTS 32
/// vec4s per light in u_fog_lights.
#define FOG_LOCAL_LIGHT_VEC4S 3
/// Froxels across and slices deep of a cluster: a multiple of the lighting's 4x4x4 groups, so a group reads one mask.
#define FOG_CLUSTER_COLUMNS 8
#define FOG_CLUSTER_SLICES 8

/// x = point and spot lights, y = half a froxel's height per metre of view depth (the falloff's distance bias).
uniform vec4 u_fog_light_info;
/// Per light: (camera-relative position, 1 / range), (axis the light travels along, cos outer half angle; -2 for a
/// point light), (linear color x candela x volumetric intensity, 1 / (cos inner - cos outer half angle)).
uniform vec4 u_fog_lights[96];

/// Clusters along each axis of the grid.
ivec3 fog_cluster_counts()
{
	ivec3 cluster_size = ivec3(FOG_CLUSTER_COLUMNS, FOG_CLUSTER_COLUMNS, FOG_CLUSTER_SLICES);
	return (ivec3(u_fog_grid.xyz) + cluster_size - 1) / cluster_size;
}

/// Where cluster @p cluster's mask sits in the masks.
int fog_cluster_index(ivec3 cluster)
{
	ivec3 counts = fog_cluster_counts();
	return (cluster.z * counts.y + cluster.y) * counts.x + cluster.x;
}

/// The cluster froxel @p cell falls in.
ivec3 fog_cell_cluster(ivec3 cell)
{
	return cell / ivec3(FOG_CLUSTER_COLUMNS, FOG_CLUSTER_COLUMNS, FOG_CLUSTER_SLICES);
}

#endif // FOG_LIGHTS_SH_HEADER_GUARD
