#pragma once
#include <engine/engine_export.h>

#include <math/math.h>

#include <cstdint>

namespace unravel
{
/**
 * @brief Enum class representing the type of reflection probe.
 */
enum class probe_type : std::uint8_t
{
    /// Box type reflection probe.
    box = 0,
    /// Sphere type reflection probe.
    sphere = 1
};

/**
 * @brief Enum class representing the reflection method.
 */
enum class reflect_method : std::uint8_t
{
    /// Environment reflection method.
    environment = 0,
    /// Static-only reflection method.
    static_only = 1
};

/**
 * @brief Enum class describing when a reflection probe refreshes its cubemap.
 *
 * Every probe bakes once when it is created or loaded, because the capture is never saved; the
 * mode decides the bakes after that. Static probes refresh on request (on_demand) or after an
 * edit (once); realtime probes refresh continuously, time-sliced across frames.
 */
enum class probe_update_mode : std::uint8_t
{
    /// After the initial bake, only refreshes when explicitly requested via reflection_probe_component::mark_dirty().
    /// Recommended runtime default for probes covering static geometry.
    on_demand = 0,
    /// Bakes once on load (or when edited in the editor), then stops refreshing until marked dirty.
    once = 1,
    /// Continuously refreshes, time-sliced according to faces_per_frame / update_interval.
    /// Use for probes that need to reflect dynamic lighting (e.g. day/night cycles).
    realtime = 2
};

/**
 * @brief Fixed cubemap face resolutions supported by reflection probes.
 */
enum class probe_resolution : std::uint8_t
{
    res_16 = 0,
    res_32,
    res_64,
    res_128,
    res_256,
    res_512,
    res_1024,

    count
};

/**
 * @brief Converts a probe_resolution enum value to its pixel size.
 */
inline auto probe_resolution_to_size(probe_resolution resolution) -> std::uint16_t
{
    switch(resolution)
    {
    case probe_resolution::res_16:
        return 16;
    case probe_resolution::res_32:
        return 32;
    case probe_resolution::res_64:
        return 64;
    case probe_resolution::res_128:
        return 128;
    case probe_resolution::res_256:
        return 256;
    case probe_resolution::res_512:
        return 512;
    case probe_resolution::res_1024:
        return 1024;
    default:
        return 256;
    }
}

/**
 * @brief Structure representing a reflection probe.
 */
struct reflection_probe
{
    /**
     * @brief Structure representing box projection data.
     */
    struct box
    {
        /// Extents of the box projection.
        math::vec3 extents = {5.0, 5.0f, 5.0f};
        /// Transition distance for the box projection.
        float transition_distance = 1.0f;
    };

    /**
     * @brief Structure representing sphere projection data.
     */
    struct sphere
    {
        /// Range of the sphere projection.
        float range = 5.0f;
    };

    /**
     * @brief How far the view of a capture face reaches (its far clip), for the probe at @p transform.
     *
     * Faces look along the world axes, +X, -X, +Y, -Y, +Z, -Z (camera::get_face_camera). A box reaches along a
     * face's axis as far as its farthest corner, with the probe's rotation and scale; a sphere reaches its range
     * times the largest scale in every direction.
     */
    auto get_face_extents(uint32_t face, const math::transform& transform) const -> float
    {
        if(type == probe_type::sphere)
        {
            return get_sphere_radius(transform.get_scale());
        }

        if(type == probe_type::box)
        {
            // The box's half axes in the world are the transform's columns times the extents; a face's reach is
            // the sum of their lengths along its world axis.
            const auto& matrix = transform.get_matrix();
            const int axis = int(face / 2);
            float reach = 0.0f;
            for(int column = 0; column < 3; ++column)
            {
                reach += math::abs(matrix[column][axis]) * box_data.extents[column];
            }
            return reach;
        }

        return {};
    }

    /**
     * @brief The sphere's radius in the world: its range times the largest of the transform's @p scale, a mirrored
     * (negative) one too.
     */
    auto get_sphere_radius(const math::vec3& scale) const -> float
    {
        const math::vec3 size = math::abs(scale);
        return sphere_data.range * math::max(size.x, math::max(size.y, size.z));
    }

    /**
     * @brief How far the box's influence reaches from its centre at the transform's @p scale, a mirrored (negative)
     * scale too: its half sizes and its transition distance, which the shader measures in the box's own units
     * (box_probe.sh), both scaled.
     */
    auto get_box_influence_radius(const math::vec3& scale) const -> float
    {
        return math::length(math::abs(scale) * (box_data.extents + box_data.transition_distance));
    }

    /**
     * @brief How far the probe's influence reaches from its centre at the transform's @p scale.
     */
    auto get_influence_radius(const math::vec3& scale) const -> float
    {
        if(type == probe_type::sphere)
        {
            return get_sphere_radius(scale);
        }
        if(type == probe_type::box)
        {
            return get_box_influence_radius(scale);
        }
        return 0.0f;
    }

    /// Type of the reflection probe.
    probe_type type = probe_type::box;
    /// Reflection method.
    reflect_method method = reflect_method::environment;
    /// Intensity
    float intensity = 1.0f;
    /// Data describing box projection.
    box box_data;
    /// Data describing sphere projection.
    sphere sphere_data;
};

/**
 * @brief Equality operator for reflection_probe.
 *
 * @param pr1 First reflection_probe to compare.
 * @param pr2 Second reflection_probe to compare.
 * @return true if the reflection probes are equal, false otherwise.
 */
inline auto operator==(const reflection_probe& pr1, const reflection_probe& pr2) -> bool
{
    return pr1.type == pr2.type && pr1.method == pr2.method &&
           math::equal(pr1.intensity, pr2.intensity, math::epsilon<float>()) &&
           math::all(math::equal(pr1.box_data.extents, pr2.box_data.extents, math::epsilon<float>())) &&
           math::equal(pr1.box_data.transition_distance, pr2.box_data.transition_distance, math::epsilon<float>()) &&
           math::equal(pr1.sphere_data.range, pr2.sphere_data.range, math::epsilon<float>());
}

/**
 * @brief Inequality operator for reflection_probe.
 *
 * @param pr1 First reflection_probe to compare.
 * @param pr2 Second reflection_probe to compare.
 * @return true if the reflection probes are not equal, false otherwise.
 */
inline auto operator!=(const reflection_probe& pr1, const reflection_probe& pr2) -> bool
{
    return !(pr1 == pr2);
}

} // namespace unravel
