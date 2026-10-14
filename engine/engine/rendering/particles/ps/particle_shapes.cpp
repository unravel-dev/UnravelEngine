#include "particle_shapes.h"

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace ps_soa
{
namespace
{

/// Below this squared length a point is the origin: it has no direction.
constexpr float k_min_point_length_sq = 1.0e-12f;

auto random_in_unit_ball(bx::RngMwc& rng) -> math::vec3
{
    math::vec3 p;
    do
    {
        p = math::vec3(frand_range(rng, -1.0f, 1.0f), frand_range(rng, -1.0f, 1.0f), frand_range(rng, -1.0f, 1.0f));
    } while(math::dot(p, p) > 1.0f);
    return p;
}

auto random_in_unit_disk(bx::RngMwc& rng) -> math::vec2
{
    math::vec2 p;
    do
    {
        p = math::vec2(frand_range(rng, -1.0f, 1.0f), frand_range(rng, -1.0f, 1.0f));
    } while(math::dot(p, p) > 1.0f);
    return p;
}

auto random_on_unit_circle(bx::RngMwc& rng) -> math::vec2
{
    const float a = frand_range(rng, 0.0f, math::two_pi<float>());
    return math::vec2(std::cos(a), std::sin(a));
}

auto sample_classic_surface(emitter_shape shape, bx::RngMwc& rng) -> math::vec3
{
    switch(shape)
    {
        default:
        case emitter_shape::sphere:
            return random_unit_vector(rng);
        case emitter_shape::hemisphere:
        {
            math::vec3 p = random_unit_vector(rng);
            p.y = math::abs(p.y);
            return p;
        }
        case emitter_shape::circle:
        {
            const math::vec2 c = random_on_unit_circle(rng);
            return math::vec3(c.x, 0.0f, c.y);
        }
        case emitter_shape::box:
        {
            const int face = int(frand01(rng) * 6.0f);
            const float u = frand_range(rng, -1.0f, 1.0f);
            const float v = frand_range(rng, -1.0f, 1.0f);
            switch(face)
            {
                case 0: return math::vec3(1.0f, u, v);
                case 1: return math::vec3(-1.0f, u, v);
                case 2: return math::vec3(u, 1.0f, v);
                case 3: return math::vec3(u, -1.0f, v);
                case 4: return math::vec3(u, v, 1.0f);
                default: return math::vec3(u, v, -1.0f);
            }
        }
        case emitter_shape::rect:
        {
            const int edge = int(frand01(rng) * 4.0f);
            const float t = frand_range(rng, -1.0f, 1.0f);
            switch(edge)
            {
                case 0: return math::vec3(t, 0.0f, 1.0f);
                case 1: return math::vec3(1.0f, 0.0f, t);
                case 2: return math::vec3(t, 0.0f, -1.0f);
                default: return math::vec3(-1.0f, 0.0f, t);
            }
        }
        case emitter_shape::line:
            return math::vec3(frand01(rng) < 0.5f ? -1.0f : 1.0f, 0.0f, 0.0f);
    }
}

auto sample_classic_inside(emitter_shape shape, bx::RngMwc& rng) -> math::vec3
{
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    switch(shape)
    {
        default:
        case emitter_shape::sphere:
            return random_in_unit_ball(rng);
        case emitter_shape::hemisphere:
        {
            math::vec3 p = random_in_unit_ball(rng);
            return math::dot(p, up) < 0.0f ? -p : p;
        }
        case emitter_shape::circle:
        {
            const math::vec2 d = random_in_unit_disk(rng);
            return math::vec3(d.x, 0.0f, d.y);
        }
        case emitter_shape::box:
            return math::vec3(frand_range(rng, -1.0f, 1.0f), frand_range(rng, -1.0f, 1.0f), frand_range(rng, -1.0f, 1.0f));
        case emitter_shape::rect:
            return math::vec3(frand_range(rng, -1.0f, 1.0f), 0.0f, frand_range(rng, -1.0f, 1.0f));
        case emitter_shape::line:
            return math::vec3(frand_range(rng, -1.0f, 1.0f), 0.0f, 0.0f);
    }
}

/// A point on the cone's base disc (its rim for surface) and the slant through it: straight up at the centre,
/// the cone angle at the rim.
auto sample_cone(const emitter_emission_desc& emission, bx::RngMwc& rng) -> shape_sample
{
    const math::vec2 disc = emission.spawn_location == spawn_location::surface ? random_on_unit_circle(rng)
                                                                               : random_in_unit_disk(rng);
    const float radius = math::length(disc);
    const float angle = radius * math::radians(math::clamp(emission.cone_angle, 0.0f, 90.0f));
    const math::vec3 radial = radius > 0.0f ? math::vec3(disc.x, 0.0f, disc.y) / radius : math::vec3(0.0f);
    shape_sample sample;
    sample.point = math::vec3(disc.x, 0.0f, disc.y);
    sample.normal = math::vec3(0.0f, std::cos(angle), 0.0f) + radial * std::sin(angle);
    return sample;
}

/// A point of the torus's tube (on it for surface) and the tube's outward direction there.
auto sample_torus(const emitter_emission_desc& emission, bx::RngMwc& rng) -> shape_sample
{
    const float around = frand_range(rng, 0.0f, math::two_pi<float>());
    const float across = frand_range(rng, 0.0f, math::two_pi<float>());
    const float tube = math::max(emission.torus_thickness, 0.0f);
    const float radius = emission.spawn_location == spawn_location::surface ? tube : tube * std::sqrt(frand01(rng));
    const math::vec3 ring(std::cos(around), 0.0f, std::sin(around));
    shape_sample sample;
    sample.normal = ring * std::cos(across) + math::vec3(0.0f, std::sin(across), 0.0f);
    sample.point = ring + sample.normal * radius;
    return sample;
}

/// A point of the mesh: anywhere on its surface by area, or one of its vertices; the interpolated vertex
/// normal there.
auto sample_mesh(const emitter_emission_desc& emission, bx::RngMwc& rng) -> shape_sample
{
    shape_sample sample;
    const emission_mesh* mesh = emission.mesh.get();
    if(mesh == nullptr || mesh->positions.empty())
    {
        return sample;
    }
    if(emission.mesh_mode == mesh_spawn_mode::vertices || mesh->cumulative_area.empty())
    {
        const size_t vertex = math::min(size_t(frand01(rng) * float(mesh->positions.size())), mesh->positions.size() - 1);
        sample.point = mesh->positions[vertex];
        sample.normal = mesh->normals[vertex];
        return sample;
    }
    const float pick = frand01(rng) * mesh->cumulative_area.back();
    const auto found = std::upper_bound(mesh->cumulative_area.begin(), mesh->cumulative_area.end(), pick);
    const size_t triangle = math::min(size_t(found - mesh->cumulative_area.begin()), mesh->cumulative_area.size() - 1);
    const uint32_t a = mesh->indices[triangle * 3];
    const uint32_t b = mesh->indices[triangle * 3 + 1];
    const uint32_t c = mesh->indices[triangle * 3 + 2];
    // Uniform over the triangle: the square root keeps the density even toward its corners.
    const float s = std::sqrt(frand01(rng));
    const float t = frand01(rng);
    const float wa = 1.0f - s;
    const float wb = s * (1.0f - t);
    const float wc = s * t;
    sample.point = mesh->positions[a] * wa + mesh->positions[b] * wb + mesh->positions[c] * wc;
    sample.normal = direction_from_center(mesh->normals[a] * wa + mesh->normals[b] * wb + mesh->normals[c] * wc);
    return sample;
}

} // namespace

auto frand01(bx::RngMwc& rng) -> float
{
    return bx::frnd(&rng);
}

auto frand_range(bx::RngMwc& rng, float lo, float hi) -> float
{
    return math::mix(lo, hi, frand01(rng));
}

auto random_unit_vector(bx::RngMwc& rng) -> math::vec3
{
    // Marsaglia method for uniform direction on sphere surface.
    float x = 0.0f;
    float y = 0.0f;
    float s = 0.0f;
    do
    {
        x = frand_range(rng, -1.0f, 1.0f);
        y = frand_range(rng, -1.0f, 1.0f);
        s = x * x + y * y;
    } while(s >= 1.0f || s <= 0.0f);
    const float f = 2.0f * std::sqrt(1.0f - s);
    return math::vec3(x * f, y * f, 1.0f - 2.0f * s);
}

auto random_in_cone(bx::RngMwc& rng, const math::vec3& axis, float half_angle) -> math::vec3
{
    if(half_angle <= 0.0f)
    {
        return axis;
    }
    const float cos_max = std::cos(math::min(half_angle, math::pi<float>()));
    const float cos_theta = math::mix(1.0f, cos_max, frand01(rng));
    const float sin_theta = std::sqrt(math::max(0.0f, 1.0f - cos_theta * cos_theta));
    const float phi = frand_range(rng, 0.0f, math::two_pi<float>());
    const math::vec3 helper = math::abs(axis.y) < 0.99f ? math::vec3(0.0f, 1.0f, 0.0f) : math::vec3(1.0f, 0.0f, 0.0f);
    const math::vec3 tangent = math::normalize(math::cross(helper, axis));
    const math::vec3 bitangent = math::cross(axis, tangent);
    return math::normalize(axis * cos_theta + (tangent * std::cos(phi) + bitangent * std::sin(phi)) * sin_theta);
}

auto get_unit_spawn_region(const emitter_emission_desc& emission) -> math::bbox
{
    switch(emission.shape)
    {
        case emitter_shape::hemisphere:
            return {math::vec3(-1.0f, 0.0f, -1.0f), math::vec3(1.0f, 1.0f, 1.0f)};
        case emitter_shape::circle:
        case emitter_shape::rect:
        case emitter_shape::cone:
            return {math::vec3(-1.0f, 0.0f, -1.0f), math::vec3(1.0f, 0.0f, 1.0f)};
        case emitter_shape::line:
            return {math::vec3(-1.0f, 0.0f, 0.0f), math::vec3(1.0f, 0.0f, 0.0f)};
        case emitter_shape::torus:
        {
            const float tube = math::max(emission.torus_thickness, 0.0f);
            return {math::vec3(-1.0f - tube, -tube, -1.0f - tube), math::vec3(1.0f + tube, tube, 1.0f + tube)};
        }
        case emitter_shape::mesh:
            return emission.mesh ? emission.mesh->bounds : math::bbox(math::vec3(0.0f), math::vec3(0.0f));
        default:
            return {math::vec3(-1.0f), math::vec3(1.0f)};
    }
}

auto direction_from_center(const math::vec3& point) -> math::vec3
{
    const float length_sq = math::dot(point, point);
    return length_sq > k_min_point_length_sq ? point / std::sqrt(length_sq) : math::vec3(0.0f, 1.0f, 0.0f);
}

auto has_shape_normal(emitter_shape shape) -> bool
{
    return shape == emitter_shape::cone || shape == emitter_shape::torus || shape == emitter_shape::mesh;
}

auto sample_shape(const emitter_emission_desc& emission, bx::RngMwc& rng) -> shape_sample
{
    switch(emission.shape)
    {
        case emitter_shape::cone:
            return sample_cone(emission, rng);
        case emitter_shape::torus:
            return sample_torus(emission, rng);
        case emitter_shape::mesh:
            return sample_mesh(emission, rng);
        default:
        {
            shape_sample sample;
            sample.point = emission.spawn_location == spawn_location::surface ? sample_classic_surface(emission.shape, rng)
                                                                              : sample_classic_inside(emission.shape, rng);
            return sample;
        }
    }
}

auto shape_direction(emitter_direction direction, emitter_shape shape, const shape_sample& sample) -> math::vec3
{
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    switch(direction)
    {
        case emitter_direction::outward:
            return sample.normal;
        case emitter_direction::inward:
            return -sample.normal;
        default:
            return shape == emitter_shape::cone ? sample.normal : up;
    }
}

} // namespace ps_soa
} // namespace unravel
