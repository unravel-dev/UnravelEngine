#pragma once

#include "emitter_desc.h"

#include <bx/rng.h>
#include <math/bbox.h>
#include <math/math.h>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief A spawn point of a shape (before shape_position and shape_scale) and the unit direction pointing out of the
 * shape there (the classic shapes point away from the emitter's origin through the placed point instead, see
 * has_shape_normal).
 */
struct shape_sample
{
    math::vec3 point{0.0f};
    math::vec3 normal{0.0f, 1.0f, 0.0f};
};

/// A random number in [0, 1).
auto frand01(bx::RngMwc& rng) -> float;
/// A random number between @p lo and @p hi.
auto frand_range(bx::RngMwc& rng, float lo, float hi) -> float;
/// A direction uniformly spread over the sphere.
auto random_unit_vector(bx::RngMwc& rng) -> math::vec3;
/// A direction uniformly spread over the spherical cap of half angle @p half_angle (radians) around @p axis.
auto random_in_cone(bx::RngMwc& rng, const math::vec3& axis, float half_angle) -> math::vec3;
/// The unit region a shape spawns in, before shape_position and shape_scale.
auto get_unit_spawn_region(const emitter_emission_desc& emission) -> math::bbox;
/// The unit direction from the origin through @p point (up at the origin).
auto direction_from_center(const math::vec3& point) -> math::vec3;
/// Cone, torus and mesh launch along their own normals; the classic shapes outward from the emitter's origin.
auto has_shape_normal(emitter_shape shape) -> bool;
/// A spawn point of the emission's shape, inside it or on its surface.
auto sample_shape(const emitter_emission_desc& emission, bx::RngMwc& rng) -> shape_sample;
/// The launch direction (before spread) of a particle spawned at @p sample. A cone always launches along its slant
/// (inward reverses it).
auto shape_direction(emitter_direction direction, emitter_shape shape, const shape_sample& sample) -> math::vec3;

} // namespace ps_soa
} // namespace unravel
