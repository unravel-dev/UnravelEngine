#ifndef SKINNING_PALETTE_SH_HEADER_GUARD
#define SKINNING_PALETTE_SH_HEADER_GUARD

/*
 * Bone palettes of instanced skinned draws (skinning_palette_buffer.h). A bone is the top three rows of its affine
 * skinning matrix (bone world x inverse bind), three vec4s in a row. An instance carries the first row of its
 * palette; a vertex's bone indices count bones from there.
 */

#include <bgfx_compute.sh>

/// SKINNING_PALETTE_STAGE in skinning_palette_buffer.h.
BUFFER_RO(s_skin_palettes, vec4, 7);

#define SKIN_PALETTE_ROWS_PER_BONE 3u

/// The skinning matrix of a vertex: the rows of its four bones blended by weight. Every instanced skinned shader
/// calls this one function, so the G-buffer and the velocity pass compute the same position bit for bit (the
/// velocity pass draws with an EQUAL depth test against the G-buffer depth).
///
/// The bottom row is the blend of the bones' (0, 0, 0, 1) rows, (0, 0, 0, sum of weights), exactly what blending
/// whole matrices gives: the importer keeps the first four influences without renormalizing them, and the projective
/// divide then renormalizes the position as the uniform-palette shaders always did.
mat4 skin_palette_matrix(float _first_row, vec4 _indices, vec4 _weights)
{
	uint base = uint(_first_row);
	uvec4 rows = uvec4(base, base, base, base) + uvec4(_indices) * SKIN_PALETTE_ROWS_PER_BONE;
	vec4 row0 = _weights.x * s_skin_palettes[rows.x]
	          + _weights.y * s_skin_palettes[rows.y]
	          + _weights.z * s_skin_palettes[rows.z]
	          + _weights.w * s_skin_palettes[rows.w];
	vec4 row1 = _weights.x * s_skin_palettes[rows.x + 1u]
	          + _weights.y * s_skin_palettes[rows.y + 1u]
	          + _weights.z * s_skin_palettes[rows.z + 1u]
	          + _weights.w * s_skin_palettes[rows.w + 1u];
	vec4 row2 = _weights.x * s_skin_palettes[rows.x + 2u]
	          + _weights.y * s_skin_palettes[rows.y + 2u]
	          + _weights.z * s_skin_palettes[rows.z + 2u]
	          + _weights.w * s_skin_palettes[rows.w + 2u];
	float weight_sum = _weights.x + _weights.y + _weights.z + _weights.w;
	return mtxFromRows(row0, row1, row2, vec4(0.0, 0.0, 0.0, weight_sum));
}

#endif // SKINNING_PALETTE_SH_HEADER_GUARD
