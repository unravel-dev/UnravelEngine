#include "cloud_noise.h"
#include <graphics/graphics.h>
#include <logging/logging.h>

#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

namespace unravel
{

namespace
{

// ---- Integer hashing (Wang hash) — excellent distribution for small integers ----

auto wang_hash(uint32_t seed) -> uint32_t
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

auto hash_int3(int x, int y, int z) -> float
{
    uint32_t h = wang_hash(
        static_cast<uint32_t>(x) * 73856093u ^
        static_cast<uint32_t>(y) * 19349663u ^
        static_cast<uint32_t>(z) * 83492791u);
    return static_cast<float>(h) / static_cast<float>(0xFFFFFFFFu);
}

struct f3
{
    float x, y, z;
};

auto hash_int3_vec3(int x, int y, int z) -> f3
{
    float hx = hash_int3(x, y, z);
    float hy = hash_int3(x + 47, y + 17, z + 31);
    float hz = hash_int3(x + 89, y + 53, z + 71);
    return {hx, hy, hz};
}

// ---- Tiling helpers ----

const int period = cloud_noise_textures::tile_period;

auto wrap(int v) -> int
{
    return ((v % period) + period) % period;
}

// ---- Math helpers ----

auto lerp(float a, float b, float t) -> float { return a + (b - a) * t; }

auto smoothstep_f(float t) -> float
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---- Tileable value noise (Perlin-like) ----

auto value_noise_3d(float px, float py, float pz) -> float
{
    int ix = static_cast<int>(std::floor(px));
    int iy = static_cast<int>(std::floor(py));
    int iz = static_cast<int>(std::floor(pz));

    float fx = px - std::floor(px);
    float fy = py - std::floor(py);
    float fz = pz - std::floor(pz);

    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);
    float uz = fz * fz * (3.0f - 2.0f * fz);

    int i0x = wrap(ix),     i0y = wrap(iy),     i0z = wrap(iz);
    int i1x = wrap(ix + 1), i1y = wrap(iy + 1), i1z = wrap(iz + 1);

    float n000 = hash_int3(i0x, i0y, i0z);
    float n100 = hash_int3(i1x, i0y, i0z);
    float n010 = hash_int3(i0x, i1y, i0z);
    float n110 = hash_int3(i1x, i1y, i0z);
    float n001 = hash_int3(i0x, i0y, i1z);
    float n101 = hash_int3(i1x, i0y, i1z);
    float n011 = hash_int3(i0x, i1y, i1z);
    float n111 = hash_int3(i1x, i1y, i1z);

    float nx00 = lerp(n000, n100, ux);
    float nx10 = lerp(n010, n110, ux);
    float nx01 = lerp(n001, n101, ux);
    float nx11 = lerp(n011, n111, ux);

    float nxy0 = lerp(nx00, nx10, uy);
    float nxy1 = lerp(nx01, nx11, uy);

    return lerp(nxy0, nxy1, uz);
}

// ---- Tileable Worley noise ----

auto worley_noise_3d(float px, float py, float pz) -> float
{
    int ix = static_cast<int>(std::floor(px));
    int iy = static_cast<int>(std::floor(py));
    int iz = static_cast<int>(std::floor(pz));

    float fx = px - std::floor(px);
    float fy = py - std::floor(py);
    float fz = pz - std::floor(pz);

    float min_dist = 1.0f;

    for(int dx = -1; dx <= 1; dx++)
    {
        for(int dy = -1; dy <= 1; dy++)
        {
            for(int dz = -1; dz <= 1; dz++)
            {
                int cx = wrap(ix + dx);
                int cy = wrap(iy + dy);
                int cz = wrap(iz + dz);

                f3 pt = hash_int3_vec3(cx, cy, cz);

                float diff_x = float(dx) + pt.x - fx;
                float diff_y = float(dy) + pt.y - fy;
                float diff_z = float(dz) + pt.z - fz;

                float dist = diff_x * diff_x + diff_y * diff_y + diff_z * diff_z;
                min_dist = std::min(min_dist, dist);
            }
        }
    }
    return std::sqrt(min_dist);
}

// ---- FBM wrappers ----

auto fbm_value(float px, float py, float pz, int octaves) -> float
{
    float value = 0.0f;
    float amplitude = 0.5f;
    float freq = 1.0f;
    for(int i = 0; i < octaves; i++)
    {
        value += amplitude * value_noise_3d(px * freq, py * freq, pz * freq);
        freq *= 2.0f;
        amplitude *= 0.5f;
    }
    return value;
}

auto worley_at(float px, float py, float pz, float freq) -> float
{
    return worley_noise_3d(px * freq, py * freq, pz * freq);
}

// ---- Remap utility (Schneider / HZD) ----

auto remap(float value, float lo, float hi, float new_lo, float new_hi) -> float
{
    return new_lo + (value - lo) / (hi - lo) * (new_hi - new_lo);
}

// ---- Float-to-half conversion ----

auto float_to_half(float value) -> uint16_t
{
    uint32_t f = 0;
    std::memcpy(&f, &value, 4);

    uint32_t sign = (f >> 16u) & 0x8000u;
    auto exponent = static_cast<int32_t>(((f >> 23u) & 0xFFu)) - 127 + 15;
    uint32_t mantissa = f & 0x007FFFFFu;

    if(exponent <= 0)
    {
        return static_cast<uint16_t>(sign);
    }
    if(exponent >= 31)
    {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }

    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10u) | (mantissa >> 13u));
}

} // namespace

void cloud_noise_textures::generate()
{
    generate_3d();
    generate_flat();
}

void cloud_noise_textures::clear()
{
    base_noise.reset();
    flat_noise.reset();
}

void cloud_noise_textures::generate_3d()
{
    constexpr uint16_t res = resolution;
    constexpr size_t total = size_t(res) * res * res;
    constexpr size_t num_channels = 4;
    constexpr size_t num_halfs = total * num_channels;
    constexpr size_t bytes = num_halfs * sizeof(uint16_t);

    APPLOG_TRACE("[CloudNoise] Generating {}x{}x{} 3D noise texture (RGBA16F, period={})...",
                res, res, res, tile_period);

    std::vector<uint16_t> data(num_halfs);

    const float inv_res = float(tile_period) / float(res);

    for(uint16_t z = 0; z < res; z++)
    {
        for(uint16_t y = 0; y < res; y++)
        {
            for(uint16_t x = 0; x < res; x++)
            {
                float px = float(x) * inv_res;
                float py = float(y) * inv_res;
                float pz = float(z) * inv_res;

                // Perlin FBM (4 octaves) — soft large-scale shape
                float perlin4 = fbm_value(px, py, pz, 4);

                // Inverted Worley at 1x — puffy cell centers
                float inv_worley = 1.0f - worley_at(px, py, pz, 1.0f);

                // R: Linear Perlin-Worley blend (matches original procedural shader)
                // Full [0,1] range — coverage=0.5 gives ~50% sky coverage
                float perlin_worley = perlin4 * 0.65f + inv_worley * 0.35f;
                perlin_worley = std::clamp(perlin_worley, 0.0f, 1.0f);

                // G/B/A: Worley at increasing frequencies for multi-octave erosion
                float worley_1x = worley_at(px, py, pz, 1.0f);
                float worley_2x = worley_at(px, py, pz, 2.0f);
                float worley_4x = worley_at(px, py, pz, 4.0f);

                size_t idx = (size_t(z) * res * res + size_t(y) * res + size_t(x)) * num_channels;
                data[idx + 0] = float_to_half(perlin_worley);
                data[idx + 1] = float_to_half(worley_1x);
                data[idx + 2] = float_to_half(worley_2x);
                data[idx + 3] = float_to_half(worley_4x);
            }
        }
    }

    APPLOG_TRACE("[CloudNoise] Noise computed, uploading to GPU...");

    auto* mem = gfx::copy(data.data(), static_cast<uint32_t>(bytes));
    base_noise = std::make_unique<gfx::texture>(
        res, res, res,
        false,
        gfx::texture_format::RGBA16F,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
        mem);

    APPLOG_TRACE("[CloudNoise] 3D noise texture ready ({} MB).", bytes / size_t(1024 * 1024));
}

void cloud_noise_textures::generate_flat()
{
    constexpr uint16_t res = flat_resolution;
    constexpr size_t total = size_t(res) * res;
    constexpr size_t num_channels = 4;
    constexpr size_t num_halfs = total * num_channels;
    constexpr size_t bytes = num_halfs * sizeof(uint16_t);

    APPLOG_TRACE("[CloudNoise] Generating {}x{} 2D flat noise texture (RGBA16F, period={})...",
                res, res, tile_period);

    std::vector<uint16_t> data(num_halfs);

    const float inv_res = float(tile_period) / float(res);
    const float fixed_z = float(tile_period) * 0.5f;

    for(uint16_t y = 0; y < res; y++)
    {
        for(uint16_t x = 0; x < res; x++)
        {
            float px = float(x) * inv_res;
            float py = float(y) * inv_res;

            float perlin4 = fbm_value(px, fixed_z, py, 4);
            float inv_worley = 1.0f - worley_at(px, fixed_z, py, 1.0f);

            float perlin_worley = perlin4 * 0.65f + inv_worley * 0.35f;
            perlin_worley = std::clamp(perlin_worley, 0.0f, 1.0f);

            float worley_1x = worley_at(px, fixed_z, py, 1.0f);
            float worley_2x = worley_at(px, fixed_z, py, 2.0f);
            float worley_4x = worley_at(px, fixed_z, py, 4.0f);

            size_t idx = (size_t(y) * res + size_t(x)) * num_channels;
            data[idx + 0] = float_to_half(perlin_worley);
            data[idx + 1] = float_to_half(worley_1x);
            data[idx + 2] = float_to_half(worley_2x);
            data[idx + 3] = float_to_half(worley_4x);
        }
    }

    APPLOG_TRACE("[CloudNoise] 2D flat noise computed, uploading to GPU...");

    auto* mem = gfx::copy(data.data(), static_cast<uint32_t>(bytes));
    flat_noise = std::make_unique<gfx::texture>(
        res, res,
        false,
        1,
        gfx::texture_format::RGBA16F,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
        mem);

    APPLOG_TRACE("[CloudNoise] 2D flat noise texture ready ({} KB).", bytes / size_t(1024));
}

} // namespace unravel
