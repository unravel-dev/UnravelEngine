#ifndef __GI_FINITE_SH__
#define __GI_FINITE_SH__

/// @p v, or 0 when it is NaN or infinite (an exponent test, which no float optimisation removes).
float GiMakeFinite(float v)
{
	return (floatBitsToUint(v) & 0x7F800000u) == 0x7F800000u ? 0.0 : v;
}

vec3 GiMakeFinite3(vec3 v)
{
	return vec3(GiMakeFinite(v.x), GiMakeFinite(v.y), GiMakeFinite(v.z));
}

#endif // __GI_FINITE_SH__
