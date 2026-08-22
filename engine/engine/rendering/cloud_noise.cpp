#include "cloud_noise.h"
#include <concurrency/parallel.h>
#include <graphics/graphics.h>
#include <logging/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

namespace unravel
{

namespace
{

// Baked octaves. Perlin FBM: 4 octaves at 1x..8x. Worley: 1x / 2x / 4x in G / B / A.
constexpr int perlin_octaves = 4;
constexpr int worley_levels = 3;
// Base-shape blend (R channel): Perlin FBM dilated by inverted Worley.
constexpr float perlin_weight = 0.55f;
constexpr float worley_weight = 0.45f;
constexpr int period = cloud_noise_textures::tile_period;

struct f3
{
    float x, y, z;
};

// ---- Integer hashing (Wang hash) ----

auto wang_hash(uint32_t seed) -> uint32_t
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

auto hash_to_unit(uint32_t h) -> float
{
    return static_cast<float>(h) / static_cast<float>(0xFFFFFFFFu);
}

auto hash_cell(int x, int y, int z, uint32_t salt) -> uint32_t
{
    return wang_hash(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^
                     static_cast<uint32_t>(z) * 83492791u ^ salt * 2654435761u);
}

auto wrap(int v, int n) -> int
{
    return ((v % n) + n) % n;
}

// ---- Lattice tables: one gradient / feature point per cell, per frequency. The tables make
// the per-voxel evaluation hash-free and tileable by construction (cells wrap at n = period *
// frequency).

struct lattice
{
    int n{};
    std::vector<f3> points;

    auto at(int x, int y, int z) const -> const f3&
    {
        return points[(size_t(wrap(z, n)) * n + size_t(wrap(y, n))) * n + size_t(wrap(x, n))];
    }
};

auto make_gradient_lattice(int frequency, uint32_t salt) -> lattice
{
    lattice lat;
    lat.n = period * frequency;
    lat.points.resize(size_t(lat.n) * lat.n * lat.n);
    for(int z = 0; z < lat.n; z++)
    {
        for(int y = 0; y < lat.n; y++)
        {
            for(int x = 0; x < lat.n; x++)
            {
                // Uniform direction on the sphere from two hashes.
                const float u = hash_to_unit(hash_cell(x, y, z, salt));
                const float v = hash_to_unit(hash_cell(x, y, z, salt + 1u));
                const float cos_theta = u * 2.0f - 1.0f;
                const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
                const float phi = v * 6.28318530718f;
                lat.points[(size_t(z) * lat.n + y) * lat.n + x] = {sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
            }
        }
    }
    return lat;
}

auto make_feature_lattice(int frequency, uint32_t salt) -> lattice
{
    lattice lat;
    lat.n = period * frequency;
    lat.points.resize(size_t(lat.n) * lat.n * lat.n);
    for(int z = 0; z < lat.n; z++)
    {
        for(int y = 0; y < lat.n; y++)
        {
            for(int x = 0; x < lat.n; x++)
            {
                lat.points[(size_t(z) * lat.n + y) * lat.n + x] = {hash_to_unit(hash_cell(x, y, z, salt)),
                                                                   hash_to_unit(hash_cell(x, y, z, salt + 1u)),
                                                                   hash_to_unit(hash_cell(x, y, z, salt + 2u))};
            }
        }
    }
    return lat;
}

struct noise_tables
{
    lattice gradients[perlin_octaves];
    lattice features[worley_levels];

    noise_tables()
    {
        for(int i = 0; i < perlin_octaves; i++)
        {
            gradients[i] = make_gradient_lattice(1 << i, 100u + uint32_t(i) * 7u);
        }
        for(int i = 0; i < worley_levels; i++)
        {
            features[i] = make_feature_lattice(1 << i, 500u + uint32_t(i) * 11u);
        }
    }
};

// ---- Math helpers ----

auto lerp(float a, float b, float t) -> float
{
    return a + (b - a) * t;
}

auto fade(float t) -> float
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// ---- Tileable gradient (Perlin) noise, output roughly [-1, 1] ----

auto perlin_3d(const lattice& lat, float px, float py, float pz) -> float
{
    const float fx0 = std::floor(px);
    const float fy0 = std::floor(py);
    const float fz0 = std::floor(pz);
    const int ix = static_cast<int>(fx0);
    const int iy = static_cast<int>(fy0);
    const int iz = static_cast<int>(fz0);
    const float fx = px - fx0;
    const float fy = py - fy0;
    const float fz = pz - fz0;
    const float ux = fade(fx);
    const float uy = fade(fy);
    const float uz = fade(fz);

    auto dot_grad = [&](int cx, int cy, int cz, float dx, float dy, float dz) -> float
    {
        const f3& g = lat.at(ix + cx, iy + cy, iz + cz);
        return g.x * dx + g.y * dy + g.z * dz;
    };

    const float n000 = dot_grad(0, 0, 0, fx, fy, fz);
    const float n100 = dot_grad(1, 0, 0, fx - 1.0f, fy, fz);
    const float n010 = dot_grad(0, 1, 0, fx, fy - 1.0f, fz);
    const float n110 = dot_grad(1, 1, 0, fx - 1.0f, fy - 1.0f, fz);
    const float n001 = dot_grad(0, 0, 1, fx, fy, fz - 1.0f);
    const float n101 = dot_grad(1, 0, 1, fx - 1.0f, fy, fz - 1.0f);
    const float n011 = dot_grad(0, 1, 1, fx, fy - 1.0f, fz - 1.0f);
    const float n111 = dot_grad(1, 1, 1, fx - 1.0f, fy - 1.0f, fz - 1.0f);

    const float nx00 = lerp(n000, n100, ux);
    const float nx10 = lerp(n010, n110, ux);
    const float nx01 = lerp(n001, n101, ux);
    const float nx11 = lerp(n011, n111, ux);
    const float nxy0 = lerp(nx00, nx10, uy);
    const float nxy1 = lerp(nx01, nx11, uy);
    return lerp(nxy0, nxy1, uz);
}

auto perlin_fbm(const noise_tables& tables, float px, float py, float pz) -> float
{
    float value = 0.0f;
    float amplitude = 0.5f;
    float freq = 1.0f;
    for(int i = 0; i < perlin_octaves; i++)
    {
        value += amplitude * perlin_3d(tables.gradients[i], px * freq, py * freq, pz * freq);
        freq *= 2.0f;
        amplitude *= 0.5f;
    }
    return value;
}

// ---- Tileable Worley noise: distance to the nearest feature point, in [0, 1] ----

auto worley_3d(const lattice& lat, float px, float py, float pz) -> float
{
    const float fx0 = std::floor(px);
    const float fy0 = std::floor(py);
    const float fz0 = std::floor(pz);
    const int ix = static_cast<int>(fx0);
    const int iy = static_cast<int>(fy0);
    const int iz = static_cast<int>(fz0);
    const float fx = px - fx0;
    const float fy = py - fy0;
    const float fz = pz - fz0;

    float min_dist = 1.0f;
    for(int dz = -1; dz <= 1; dz++)
    {
        for(int dy = -1; dy <= 1; dy++)
        {
            for(int dx = -1; dx <= 1; dx++)
            {
                const f3& pt = lat.at(ix + dx, iy + dy, iz + dz);
                const float diff_x = float(dx) + pt.x - fx;
                const float diff_y = float(dy) + pt.y - fy;
                const float diff_z = float(dz) + pt.z - fz;
                const float dist = diff_x * diff_x + diff_y * diff_y + diff_z * diff_z;
                min_dist = std::min(min_dist, dist);
            }
        }
    }
    return std::sqrt(min_dist);
}

auto worley_at(const noise_tables& tables, int level, float px, float py, float pz) -> float
{
    const float freq = float(1 << level);
    return worley_3d(tables.features[level], px * freq, py * freq, pz * freq);
}

auto to_unorm8(float value) -> uint8_t
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Raw channels of one sample: Perlin FBM (unnormalized) and the three Worley levels.
struct sample_channels
{
    float perlin{};
    float worley[worley_levels]{};
};

auto evaluate(const noise_tables& tables, float px, float py, float pz) -> sample_channels
{
    sample_channels s;
    s.perlin = perlin_fbm(tables, px, py, pz);
    for(int i = 0; i < worley_levels; i++)
    {
        s.worley[i] = worley_at(tables, i, px, py, pz);
    }
    return s;
}

// Packs the samples: Perlin is remapped from its measured range to [0, 1] (gradient noise
// uses only part of [-1, 1]) and blended with the inverted 1x Worley into R.
void pack(const std::vector<sample_channels>& samples, std::vector<uint8_t>& out)
{
    float perlin_min = std::numeric_limits<float>::max();
    float perlin_max = std::numeric_limits<float>::lowest();
    for(const auto& s : samples)
    {
        perlin_min = std::min(perlin_min, s.perlin);
        perlin_max = std::max(perlin_max, s.perlin);
    }
    const float perlin_range = std::max(perlin_max - perlin_min, 1e-6f);
    out.resize(samples.size() * 4);
    for(size_t i = 0; i < samples.size(); i++)
    {
        const auto& s = samples[i];
        const float perlin01 = (s.perlin - perlin_min) / perlin_range;
        const float inv_worley = 1.0f - s.worley[0];
        out[i * 4 + 0] = to_unorm8(perlin01 * perlin_weight + inv_worley * worley_weight);
        out[i * 4 + 1] = to_unorm8(s.worley[0]);
        out[i * 4 + 2] = to_unorm8(s.worley[1]);
        out[i * 4 + 3] = to_unorm8(s.worley[2]);
    }
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

    const auto start = std::chrono::steady_clock::now();
    APPLOG_TRACE("[CloudNoise] Generating {}x{}x{} 3D noise texture (RGBA8, period={})...", res, res, res, tile_period);

    const noise_tables tables;
    std::vector<sample_channels> samples(total);
    const float inv_res = float(tile_period) / float(res);

    std::vector<int> slices(res);
    std::iota(slices.begin(), slices.end(), 0);
    poolstl::for_each_par_if(true,
                             slices.begin(),
                             slices.end(),
                             [&](int z)
                             {
                                 const float pz = float(z) * inv_res;
                                 for(int y = 0; y < res; y++)
                                 {
                                     const float py = float(y) * inv_res;
                                     for(int x = 0; x < res; x++)
                                     {
                                         const float px = float(x) * inv_res;
                                         samples[(size_t(z) * res + size_t(y)) * res + size_t(x)] = evaluate(tables, px, py, pz);
                                     }
                                 }
                             });

    std::vector<uint8_t> data;
    pack(samples, data);

    auto* mem = gfx::copy(data.data(), static_cast<uint32_t>(data.size()));
    base_noise = std::make_unique<gfx::texture>(res,
                                                res,
                                                res,
                                                false,
                                                gfx::texture_format::RGBA8,
                                                BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
                                                mem);

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    APPLOG_TRACE("[CloudNoise] 3D noise texture ready ({} MB, {} ms).", data.size() / size_t(1024 * 1024), ms);
}

void cloud_noise_textures::generate_flat()
{
    constexpr uint16_t res = flat_resolution;
    constexpr size_t total = size_t(res) * res;

    APPLOG_TRACE("[CloudNoise] Generating {}x{} 2D flat noise texture (RGBA8, period={})...", res, res, tile_period);

    const noise_tables tables;
    std::vector<sample_channels> samples(total);
    const float inv_res = float(tile_period) / float(res);
    const float fixed_z = float(tile_period) * 0.5f;

    for(int y = 0; y < res; y++)
    {
        const float py = float(y) * inv_res;
        for(int x = 0; x < res; x++)
        {
            const float px = float(x) * inv_res;
            // The flat path reads (u, v) as the horizontal plane: sample the field at a fixed height.
            samples[size_t(y) * res + size_t(x)] = evaluate(tables, px, fixed_z, py);
        }
    }

    std::vector<uint8_t> data;
    pack(samples, data);

    auto* mem = gfx::copy(data.data(), static_cast<uint32_t>(data.size()));
    flat_noise = std::make_unique<gfx::texture>(res,
                                                res,
                                                false,
                                                1,
                                                gfx::texture_format::RGBA8,
                                                BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
                                                mem);

    APPLOG_TRACE("[CloudNoise] 2D flat noise texture ready ({} KB).", data.size() / size_t(1024));
}

} // namespace unravel
