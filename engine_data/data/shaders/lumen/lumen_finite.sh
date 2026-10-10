#ifndef __LUMEN_FINITE_SH__
#define __LUMEN_FINITE_SH__

/// @p v, or 0 when it is NaN or infinite (an exponent test, which no float optimisation removes).
float LumenMakeFinite(float v)
{
	return (floatBitsToUint(v) & 0x7F800000u) == 0x7F800000u ? 0.0 : v;
}

vec3 LumenMakeFinite3(vec3 v)
{
	return vec3(LumenMakeFinite(v.x), LumenMakeFinite(v.y), LumenMakeFinite(v.z));
}

#endif // __LUMEN_FINITE_SH__
