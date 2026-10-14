#pragma once

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{
namespace ps_soa
{

/// Voxels per side of the turbulence noise volume (the GPU texture's size).
constexpr uint32_t particle_noise_size = 32;
/// Noise features (lattice cells of its potential) per side of one tile of the volume.
constexpr uint32_t particle_noise_cells = 8;

/**
 * @brief A tileable, divergence-free vector field (the curl of a smooth random potential) with lengths up to 1,
 * which turbulence displaces particles by. The GPU samples the same values from a 3D texture
 * (particle_vertex.sh, s_particle_noise); the CPU mirror samples them here.
 */
class particle_noise
{
public:
    particle_noise();

    /// The field at @p uvw (one unit = one tile, wrapping), trilinear between voxel centres like the GPU's
    /// filtered fetch.
    auto sample(const math::vec3& uvw) const -> math::vec3;

    /// Voxels in x-fastest order.
    auto get_voxels() const -> const std::vector<math::vec3>&;

private:
    auto voxel(int32_t x, int32_t y, int32_t z) const -> const math::vec3&;

    std::vector<math::vec3> voxels_;
};

/// The one noise volume every emitter shares (built on first use).
auto get_particle_noise() -> const particle_noise&;

} // namespace ps_soa
} // namespace unravel
