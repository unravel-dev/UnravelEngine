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

/// Spatial Interleaved Gradient Noise (Jorge Jimenez 2014). Screen-space-stationary
/// blue-noise-like distribution; high-frequency, locally-uniform, no temporal axis.
/// Use this for spatial dithering. For temporal animation, COMBINE with a separate
/// per-frame Cranley-Patterson offset (e.g. `HashFrameOffset`) -- DO NOT animate the
/// pattern by shifting the UV in screen space (the historical IGN "FrameId * (47,17)"
/// trick), because the pattern then slides in that fixed diagonal each frame and any
/// temporal accumulator integrating across frames produces a visible image-wide
/// directional drift along the shift vector.
float InterleavedGradientNoise(vec2 uv)
{
    const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

/// Legacy IGN with screen-space-shifted temporal axis. Retained for callers that
/// already rely on its directional-drift behaviour. Prefer `InterleavedGradientNoise(uv)`
/// + `HashFrameOffset(frameId)` for any new temporal-accumulation use.
float InterleavedGradientNoise(vec2 uv, float FrameId)
{
    uv += FrameId * (vec2(47, 17) * 0.695);
    return InterleavedGradientNoise(uv);
}

/// Per-frame 2D random offset, uniformly distributed in [0,1)^2 with NO directional
/// preference (any axis is equally likely). Combine with a spatial blue-noise dither
/// via Cranley-Patterson rotation: `fract(spatial + HashFrameOffset(frame))`. The
/// resulting per-pixel temporal sequence stays decorrelated across frames AND has no
/// directional drift, so a temporal accumulator integrates to a stable image instead
/// of an image that "slides" along a fixed diagonal as IGN's screen-shift temporal
/// axis does.
vec2 HashFrameOffset(uint frameId)
{
    // PCG-style hash. The constants are PCG and xorshift mixers; sequence has uniform
    // 2D distribution and a period >> any plausible accumulation window.
    uint h = frameId * 1664525u + 1013904223u;
    h ^= h >> 16u;
    h *= 0x85ebca6bu;
    h ^= h >> 13u;
    h *= 0xc2b2ae35u;
    h ^= h >> 16u;
    return vec2(float(h & 0xFFFFu),
                float((h >> 16) & 0xFFFFu)) * (1.0 / 65536.0);
}

/// Two-channel screen-space-stationary blue-noise sample with a hash-driven temporal
/// axis. The spatial pattern (driven by `pix_coord` alone) is BLUE-NOISE distributed
/// thanks to IGN's high-frequency dot-product hash; the temporal axis (driven by
/// `frame_idx` alone via `HashFrameOffset`) adds a per-frame uniform 2D offset with
/// no directional bias. Together this gives:
///   - per-frame spatial: blue noise (denoiser-friendly)
///   - per-pixel temporal: hash-uniform (no perceptual drift)
/// Use this in place of the legacy "InterleavedGradientNoise(uv, FrameId)" pattern for
/// any pass that temporally accumulates the noisy output.
vec2 BlueNoise2D(vec2 pix_coord, float frame_idx)
{
    // Spatial IGN, two phase-shifted taps to decorrelate the two channels within a frame.
    float ign1 = InterleavedGradientNoise(pix_coord);
    float ign2 = InterleavedGradientNoise(pix_coord + vec2(11.13, 7.37));
    // Cranley-Patterson rotation by a hash-derived per-frame offset. fract() wraps the
    // sum back into [0,1), preserving the per-pixel spatial structure but cycling the
    // value uniformly across frames without any directional shift.
    vec2 t_offset = HashFrameOffset(uint(frame_idx));
    return fract(vec2(ign1, ign2) + t_offset);
}

/// Hammersley with screen-space-stationary blue-noise scramble (`BlueNoise2D`). Drop-in
/// for `Hammersley16` when per-pixel blue-noise distribution of the sample offsets is
/// wanted AND the consuming pass temporally accumulates. The pixel coord drives
/// spatial decorrelation; `frame_idx` drives temporal decorrelation via a hash, NOT
/// via a screen-space shift of the noise pattern -- avoiding the directional-drift
/// artefact of the legacy `InterleavedGradientNoise(uv, FrameId)` formulation.
vec2 Hammersley16_IGN(uint idx, uint num_samples, vec2 pix_coord, float frame_idx)
{
    vec2 rnd_f = BlueNoise2D(pix_coord, frame_idx);
    uvec2 rnd = uvec2(rnd_f * 65535.0);
    return Hammersley16(idx, num_samples, rnd);
}

#endif // __SAMPLING_SH__
