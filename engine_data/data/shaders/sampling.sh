/*
 * Shared low-discrepancy sampling and RNG utilities.
 * Used by SSR, SSIL, and other screen-space passes.
 */

#ifndef __SAMPLING_SH__
#define __SAMPLING_SH__

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10;
}

uint RadicalInverse(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return bits;
}

/// 3D PCG random number generator.
/// Returns three elements with 16 random bits each (0-0xffff).
uvec3 Rand3DPCG16(ivec3 p)
{
    uvec3 v = uvec3(p);
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v >> 16u;
}

vec2 Hammersley(int i, int N)
{
    return vec2(float(i) / float(N), RadicalInverse_VdC(uint(i)));
}

/// Temporally-jittered Hammersley sequence with 16-bit precision.
vec2 Hammersley16(uint idx, uint num_samples, uvec2 rnd)
{
    float E1 = fract(float(idx) / float(num_samples) + float(rnd.x) * (1.0 / 65536.0));
    float E2 = float((RadicalInverse(idx) >> 16) ^ rnd.y) * (1.0 / 65536.0);
    return vec2(E1, E2);
}

float InterleavedGradientNoise(vec2 uv, float FrameId)
{
    uv += FrameId * (vec2(47, 17) * 0.695);
    const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

#endif // __SAMPLING_SH__
