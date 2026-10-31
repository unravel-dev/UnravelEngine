#ifndef DEFERRED_GEOM_NORMAL_BASIS_SH_HEADER_GUARD
#define DEFERRED_GEOM_NORMAL_BASIS_SH_HEADER_GUARD

/// The upper 3x3 of @p _m: what a vertex's tangent and bitangent transform by. They lie in the surface and follow it
/// under any scale.
mat3 get_upper_3x3(mat4 _m)
{
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV
	return (mat3)_m;
#else
	return mat3(_m);
#endif
}

/// What a vertex's normal transforms by under @p _m: its cofactor matrix, the inverse transpose times the determinant,
/// so the normal stays perpendicular to the surface under any scale. Negated for a mirroring @p _m, where the
/// determinant is negative. Callers normalize, so the determinant's size does not matter and nothing divides by it.
mat3 compute_normal_matrix(mat3 _m)
{
	vec3 cross12 = cross(_m[1], _m[2]);
	float mirror = dot(_m[0], cross12) < 0.0 ? -1.0 : 1.0;
	return mat3(cross12 * mirror, cross(_m[2], _m[0]) * mirror, cross(_m[0], _m[1]) * mirror);
}

#endif // DEFERRED_GEOM_NORMAL_BASIS_SH_HEADER_GUARD
