#ifndef FOG_VOLUME_SH_HEADER_GUARD
#define FOG_VOLUME_SH_HEADER_GUARD

/*
 * The froxel grid of the volumetric fog (fog_pass), shared by its lighting and integration dispatches. Cell (x, y)
 * covers screen uv ((x, y) + [0, 1)) / (columns, rows), in the render targets' uv (row 0 is the first row in memory),
 * which is the uv the composite and the particles read the column at; slice z covers slice coordinates
 * (z + [0, 1)) / slices (fog.sh fog_slice_depth).
 */

/// x = columns, y = rows, z = slices.
uniform vec4 u_fog_grid;
/// Camera-relative world rays through the grid's corners, scaled to view depth 1: uv (0, 0), (1, 0), (0, 1), (1, 1).
/// Rays at view depth 1 are linear in screen uv, so the bilinear blend of the corners is exact.
uniform vec4 u_fog_rays[4];

/// The camera-relative ray at view depth 1 through screen @p uv.
vec3 fog_grid_ray(vec2 uv)
{
	vec3 first_row = mix(u_fog_rays[0].xyz, u_fog_rays[1].xyz, uv.x);
	vec3 last_row = mix(u_fog_rays[2].xyz, u_fog_rays[3].xyz, uv.x);
	return mix(first_row, last_row, uv.y);
}

#endif // FOG_VOLUME_SH_HEADER_GUARD
