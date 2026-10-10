#ifndef __LUMEN_IMAGE_FORMATS_SH__
#define __LUMEN_IMAGE_FORMATS_SH__

/*
 * Image formats the bgfx compute macros (bgfx_compute.sh, copied from bgfx at every build) do not name, as format
 * tokens for IMAGE2D_RO / _WO / _RW. Include after bgfx_compute.sh and before the image declarations.
 *
 *  - rg11b10f: bgfx::TextureFormat::RG11B10F (R11G11B10 float, no alpha, no sign). GLSL names it r11f_g11f_b10f and
 *    the SPIR-V front end format_r11fg11fb10f; HLSL declares it float4 so imageLoad / imageStore take a vec4 (the view's
 *    format keeps rgb, the store drops alpha).
 */

#if BGFX_SHADER_LANGUAGE_GLSL
#	define rg11b10f r11f_g11f_b10f
#else
#	define COMP_rg11b10f float4
#	define format_rg11b10f format_r11fg11fb10f
#endif

/// R11G11B10's relative precision per channel: 6, 6 and 5 mantissa bits, a step of 2^-6, 2^-6 and 2^-5 of the value.
#define LUMEN_RG11B10F_QUANTIZATION_ERROR vec3(0.015625, 0.015625, 0.03125)

/// @p color ready for an rg11b10f store (dithered quantization): up to
/// one step of the format's precision at @p color added, scaled by @p e in [0, 1), so the store's truncation rounds
/// without bias on average. A history blended and stored every frame would otherwise lose part of a step per frame,
/// which the blend compounds: undithered, the rough specular settled 7% darker.
vec3 LumenQuantizeForRg11b10f(vec3 color, float e)
{
	vec3 error = color * LUMEN_RG11B10F_QUANTIZATION_ERROR;
	// The step as a power of two: the error's exponent alone.
	error = uintBitsToFloat(floatBitsToUint(error) & uvec3_splat(0xFF800000u));
	return color + error * e;
}

#endif // __LUMEN_IMAGE_FORMATS_SH__
