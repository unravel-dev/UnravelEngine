#ifndef BLOOM_COMMON_SH_HEADER_GUARD
#define BLOOM_COMMON_SH_HEADER_GUARD

/*
 * Range guard shared by the bloom passes. A NaN that enters the mip chain spreads over every
 * texel each downsample and upsample reaches and floods the frame, so every pass writes only
 * finite values in the range of its RGBA16F target.
 *
 * The guard tests the exponent bits instead of relying on min / max: GLSL and SPIR-V leave a
 * min / max with a NaN operand undefined (D3D returns the other operand), and a compiler may
 * fold an isnan() away.
 */

/// The largest finite half float.
#define BLOOM_MAX_HDR 65504.0

/// @p value clamped to [0, BLOOM_MAX_HDR]; NaN reads 0 and +Inf (a half-float overflow) the maximum.
float bloom_safe_hdr_channel(float value)
{
    uint bits = floatBitsToUint(value);
    // An all-ones exponent is +-Inf (zero mantissa) or NaN.
    bool is_finite = (bits & 0x7F800000u) != 0x7F800000u;
    float finite_value = is_finite ? value : (bits == 0x7F800000u ? BLOOM_MAX_HDR : 0.0);
    return clamp(finite_value, 0.0, BLOOM_MAX_HDR);
}

/// @p color with every channel clamped by bloom_safe_hdr_channel.
vec3 bloom_safe_hdr(vec3 color)
{
    return vec3(bloom_safe_hdr_channel(color.x), bloom_safe_hdr_channel(color.y), bloom_safe_hdr_channel(color.z));
}

#endif // BLOOM_COMMON_SH_HEADER_GUARD
