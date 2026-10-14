#include "particle_noise.h"

#include <bx/rng.h>

#include <array>
#include <cmath>

namespace unravel
{
namespace ps_soa
{
namespace
{

/// Fixed seed: every run, and the CPU and the GPU, see the same field.
constexpr uint32_t k_noise_seed = 0x5eed1234u;
constexpr int32_t k_cells = int32_t(particle_noise_cells);
constexpr int32_t k_size = int32_t(particle_noise_size);

auto wrap(int32_t value, int32_t period) -> int32_t
{
    const int32_t remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
}

/// Quintic fade: the potential and its first two derivatives stay continuous across cells.
auto fade(float t) -> float
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

class potential_field
{
public:
    potential_field()
    {
        bx::RngMwc rng(k_noise_seed, k_noise_seed ^ 0x9e3779b9u);
        for(auto& value : lattice_)
        {
            value = math::vec3(bx::frndh(&rng), bx::frndh(&rng), bx::frndh(&rng));
        }
    }

    /// The potential at @p p in lattice cells, periodic over k_cells.
    auto at(const math::vec3& p) const -> math::vec3
    {
        const math::vec3 base = math::floor(p);
        const math::vec3 f = p - base;
        const math::vec3 w(fade(f.x), fade(f.y), fade(f.z));
        const int32_t x0 = int32_t(base.x);
        const int32_t y0 = int32_t(base.y);
        const int32_t z0 = int32_t(base.z);
        math::vec3 result(0.0f);
        for(int32_t corner = 0; corner < 8; ++corner)
        {
            const int32_t dx = corner & 1;
            const int32_t dy = (corner >> 1) & 1;
            const int32_t dz = (corner >> 2) & 1;
            const float weight = (dx ? w.x : 1.0f - w.x) * (dy ? w.y : 1.0f - w.y) * (dz ? w.z : 1.0f - w.z);
            result += lattice_[index(x0 + dx, y0 + dy, z0 + dz)] * weight;
        }
        return result;
    }

private:
    static auto index(int32_t x, int32_t y, int32_t z) -> size_t
    {
        return size_t(wrap(x, k_cells) + k_cells * (wrap(y, k_cells) + k_cells * wrap(z, k_cells)));
    }

    std::array<math::vec3, size_t(k_cells * k_cells * k_cells)> lattice_{};
};

} // namespace

particle_noise::particle_noise()
{
    const potential_field potential;
    // The curl of the potential by central differences, sampled at the voxel centres.
    const float to_cells = float(k_cells) / float(k_size);
    const float step = 0.5f * to_cells;
    voxels_.resize(size_t(k_size * k_size * k_size));
    float longest = 0.0f;
    for(int32_t z = 0; z < k_size; ++z)
    {
        for(int32_t y = 0; y < k_size; ++y)
        {
            for(int32_t x = 0; x < k_size; ++x)
            {
                const math::vec3 p = (math::vec3(float(x), float(y), float(z)) + 0.5f) * to_cells;
                const math::vec3 dx = (potential.at(p + math::vec3(step, 0.0f, 0.0f)) - potential.at(p - math::vec3(step, 0.0f, 0.0f))) / (2.0f * step);
                const math::vec3 dy = (potential.at(p + math::vec3(0.0f, step, 0.0f)) - potential.at(p - math::vec3(0.0f, step, 0.0f))) / (2.0f * step);
                const math::vec3 dz = (potential.at(p + math::vec3(0.0f, 0.0f, step)) - potential.at(p - math::vec3(0.0f, 0.0f, step))) / (2.0f * step);
                const math::vec3 curl(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x);
                voxels_[size_t(x + k_size * (y + k_size * z))] = curl;
                longest = math::max(longest, math::length(curl));
            }
        }
    }
    const float scale = longest > 0.0f ? 1.0f / longest : 0.0f;
    for(auto& value : voxels_)
    {
        value *= scale;
    }
}

auto particle_noise::voxel(int32_t x, int32_t y, int32_t z) const -> const math::vec3&
{
    return voxels_[size_t(wrap(x, k_size) + k_size * (wrap(y, k_size) + k_size * wrap(z, k_size)))];
}

auto particle_noise::sample(const math::vec3& uvw) const -> math::vec3
{
    const math::vec3 position = uvw * float(k_size) - 0.5f;
    const math::vec3 base = math::floor(position);
    const math::vec3 f = position - base;
    const int32_t x0 = int32_t(base.x);
    const int32_t y0 = int32_t(base.y);
    const int32_t z0 = int32_t(base.z);
    math::vec3 result(0.0f);
    for(int32_t corner = 0; corner < 8; ++corner)
    {
        const int32_t dx = corner & 1;
        const int32_t dy = (corner >> 1) & 1;
        const int32_t dz = (corner >> 2) & 1;
        const float weight = (dx ? f.x : 1.0f - f.x) * (dy ? f.y : 1.0f - f.y) * (dz ? f.z : 1.0f - f.z);
        result += voxel(x0 + dx, y0 + dy, z0 + dz) * weight;
    }
    return result;
}

auto particle_noise::get_voxels() const -> const std::vector<math::vec3>&
{
    return voxels_;
}

auto get_particle_noise() -> const particle_noise&
{
    static const particle_noise noise;
    return noise;
}

} // namespace ps_soa
} // namespace unravel
