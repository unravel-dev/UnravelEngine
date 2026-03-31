#pragma once

#include "bbox.h"
#include "bsphere.h"
#include "frustum.h"
#include "math_types.h"
#include "plane.h"
#include "transform.hpp"
#include "color.h"
#include "gradient.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace math
{

static inline std::vector<float> log_space(std::size_t start, std::size_t end, std::size_t count)
{
    std::vector<float> result;
    result.reserve(count);
    for(std::size_t i = 0; i <= count; ++i)
    {
        float f = start * glm::pow(float(end) / float(start), float(i) / float(count));
        result.push_back(f);
    }

    return result;
}

// inline bool is_negative_float(const float& A)
// {
//     return ((*(const std::uint32_t*)&A) >= std::uint32_t(0x80000000)); // Detects sign bit.
// }

// template<typename T>
// inline T square(const T& t)
// {
//     return t * t;
// }

/**
 * Compute the screen bounds of a point light along one axis.
 * Based on http://www.gamasutra.com/features/20021011/lengyel_06.htm
 * and http://sourceforge.net/mailarchive/message.php?msg_id=10501105
 */
// inline bool compute_projected_sphere_shaft(float light_x,
//                                            float light_z,
//                                            float radius,
//                                            const transform& proj,
//                                            const vec3& axis,
//                                            float axis_sign,
//                                            std::int32_t& in_out_min_x,
//                                            std::int32_t& in_out_max_x)
// {
//     auto view_x = float(in_out_min_x);
//     auto view_size_x = float(in_out_max_x - in_out_min_x);

//     // Vertical planes: T = <Nx, 0, Nz, 0>
//     float discriminant = (square(light_x) - square(radius) + square(light_z)) * square(light_z);
//     if(discriminant >= 0)
//     {
//         float sqrt_discriminant = glm::sqrt(discriminant);
//         float inv_light_square = 1.0f / (square(light_x) + square(light_z));

//         float Nxa = (radius * light_x - sqrt_discriminant) * inv_light_square;
//         float Nxb = (radius * light_x + sqrt_discriminant) * inv_light_square;
//         float Nza = (radius - Nxa * light_x) / light_z;
//         float Nzb = (radius - Nxb * light_x) / light_z;
//         float Pza = light_z - radius * Nza;
//         float Pzb = light_z - radius * Nzb;

//         // Tangent a
//         if(Pza > 0)
//         {
//             float Pxa = -Pza * Nza / Nxa;
//             vec4 P = proj * vec4(axis.x * Pxa, axis.y * Pxa, Pza, 1);
//             float X = (dot(vec3(P), axis) / P.w + 1.0f * axis_sign) / 2.0f * axis_sign;
//             if(is_negative_float(Nxa) ^ is_negative_float(axis_sign))
//             {
//                 in_out_max_x = glm::min<std::int32_t>(std::int32_t(glm::ceil(view_size_x * X + view_x)), in_out_max_x);
//             }
//             else
//             {
//                 in_out_min_x = glm::max<std::int32_t>(std::int32_t(glm::floor(view_size_x * X + view_x)), in_out_min_x);
//             }
//         }

//         // Tangent b
//         if(Pzb > 0)
//         {
//             float Pxb = -Pzb * Nzb / Nxb;
//             vec4 P = proj * vec4(axis.x * Pxb, axis.y * Pxb, Pzb, 1);
//             float X = (dot(vec3(P), axis) / P.w + 1.0f * axis_sign) / 2.0f * axis_sign;
//             if(is_negative_float(Nxb) ^ is_negative_float(axis_sign))
//             {
//                 in_out_max_x = glm::min<std::int32_t>(std::int32_t(glm::ceil(view_size_x * X + view_x)), in_out_max_x);
//             }
//             else
//             {
//                 in_out_min_x = glm::max<std::int32_t>(std::int32_t(glm::floor(view_size_x * X + view_x)), in_out_min_x);
//             }
//         }
//     }

//     return in_out_min_x <= in_out_max_x;
// }

// //@return 0: not visible, 1:use scissor rect, 2: no scissor rect needed
// inline std::uint32_t compute_projected_sphere_rect(std::int32_t& left,
//                                                    std::int32_t& right,
//                                                    std::int32_t& top,
//                                                    std::int32_t& bottom,
//                                                    const vec3& sphere_center,
//                                                    float radius,
//                                                    const math::vec3& view_origin,
//                                                    const transform& view,
//                                                    const transform& proj)
// {
//     // Calculate a screen rectangle for the sphere's radius.
//     if(math::length2(sphere_center - view_origin) > math::square(radius))
//     {
//         math::vec3 lv = view.transform_coord(sphere_center);

//         if(!compute_projected_sphere_shaft(lv.x, lv.z, radius, proj, vec3(1.0f, 0.0f, 0.0f), 1.0f, left, right))
//         {
//             return 0;
//         }

//         if(!compute_projected_sphere_shaft(lv.y, lv.z, radius, proj, vec3(0.0f, 1.0f, 0.0f), -1.0f, top, bottom))
//         {
//             return 0;
//         }

//         return 1;
//     }
//     else
//     {
//         return 2;
//     }
// }

inline bool is_negative_float(const float& A)
{
    union {
        float f;
        std::uint32_t i;
    } u;
    u.f = A;
    return (u.i & 0x80000000) != 0; // Detects sign bit.
}

template<typename T>
inline T square(const T& t)
{
    return t * t;
}

/**
 * Compute the screen bounds of a point light along one axis.
 * Based on http://www.gamasutra.com/features/20021011/lengyel_06.htm
 * and http://sourceforge.net/mailarchive/message.php?msg_id=10501105
 */
inline bool compute_projected_sphere_shaft(float light_x,
                                           float light_z,
                                           float radius,
                                           const glm::mat4& proj,
                                           const glm::vec3& axis,
                                           float axis_sign,
                                           std::int32_t& in_out_min_x,
                                           std::int32_t& in_out_max_x)
{
    auto view_x = float(in_out_min_x);
    auto view_size_x = float(in_out_max_x - in_out_min_x);

           // Vertical planes: T = <Nx, 0, Nz, 0>
    float discriminant = (square(light_x) - square(radius) + square(light_z)) * square(light_z);
    if(discriminant >= 0)
    {
        float sqrt_discriminant = glm::sqrt(discriminant);
        float inv_light_square = 1.0f / (square(light_x) + square(light_z));

        float Nxa = (radius * light_x - sqrt_discriminant) * inv_light_square;
        float Nxb = (radius * light_x + sqrt_discriminant) * inv_light_square;
        float Nza = (radius - Nxa * light_x) / light_z;
        float Nzb = (radius - Nxb * light_x) / light_z;
        float Pza = light_z - radius * Nza;
        float Pzb = light_z - radius * Nzb;

               // Tangent a
        if(Pza > 0)
        {
            float Pxa = -Pza * Nza / Nxa;
            glm::vec4 P = proj * glm::vec4(axis.x * Pxa, axis.y * Pxa, Pza, 1);
            float X = (glm::dot(glm::vec3(P), axis) / P.w + 1.0f * axis_sign) / 2.0f * axis_sign;
            if(is_negative_float(Nxa) ^ is_negative_float(axis_sign))
            {
                in_out_max_x = std::min<std::int32_t>(std::int32_t(glm::ceil(view_size_x * X + view_x)), in_out_max_x);
            }
            else
            {
                in_out_min_x = std::max<std::int32_t>(std::int32_t(glm::floor(view_size_x * X + view_x)), in_out_min_x);
            }
        }

               // Tangent b
        if(Pzb > 0)
        {
            float Pxb = -Pzb * Nzb / Nxb;
            glm::vec4 P = proj * glm::vec4(axis.x * Pxb, axis.y * Pxb, Pzb, 1);
            float X = (glm::dot(glm::vec3(P), axis) / P.w + 1.0f * axis_sign) / 2.0f * axis_sign;
            if(is_negative_float(Nxb) ^ is_negative_float(axis_sign))
            {
                in_out_max_x = std::min<std::int32_t>(std::int32_t(glm::ceil(view_size_x * X + view_x)), in_out_max_x);
            }
            else
            {
                in_out_min_x = std::max<std::int32_t>(std::int32_t(glm::floor(view_size_x * X + view_x)), in_out_min_x);
            }
        }
    }

    return in_out_min_x <= in_out_max_x;
}

//@return 0: not visible, 1:use scissor rect, 2: no scissor rect needed
inline std::uint32_t compute_projected_sphere_rect(std::int32_t& left,
                                                   std::int32_t& right,
                                                   std::int32_t& top,
                                                   std::int32_t& bottom,
                                                   const glm::vec3& sphere_center,
                                                   float radius,
                                                   const glm::vec3& view_origin,
                                                   const glm::mat4& view,
                                                   const glm::mat4& proj)
{
    // Calculate a screen rectangle for the sphere's radius.
    if(glm::length2(sphere_center - view_origin) > square(radius))
    {
        glm::vec3 lv = glm::vec3(view * glm::vec4(sphere_center, 1.0f));

        if(!compute_projected_sphere_shaft(lv.x, lv.z, radius, proj, glm::vec3(1.0f, 0.0f, 0.0f), 1.0f, left, right))
        {
            return 0;
        }

        if(!compute_projected_sphere_shaft(lv.y, lv.z, radius, proj, glm::vec3(0.0f, 1.0f, 0.0f), -1.0f, top, bottom))
        {
            return 0;
        }

        return 1;
    }
    else
    {
        return 2;
    }
}

inline float halton(std::uint32_t Index, std::uint32_t Base)
{
    float Result = 0.0f;
    float InvBase = 1.0f / Base;
    float Fraction = InvBase;
    while(Index > 0)
    {
        Result += (Index % Base) * Fraction;
        Index /= Base;
        Fraction *= InvBase;
    }
    return Result;
}

/**
 * @brief 2D subpixel jitter in [-0.5, 0.5] for temporal AA (Kronecker / golden-ratio sequence).
 *
 * Successive integer @p frame values advance by incommensurable steps on the unit torus, so
 * offsets fill the pixel progressively without the large consecutive jumps of Halton(i,2)/(i,3)
 * and without a hard period-N reset from @c frame % N (which causes a periodic reprojection spike).
 *
 * @param temporal_phase_scale Multiplies the frame index inside the sequence (default 1). Values below 1
 *        (e.g. 0.35–0.5) walk the torus more slowly so consecutive frames sit closer on the pixel,
 *        which helps TAA history track and reduces visible whole-frame shake; coverage still fills over time.
 */
inline void taa_subpixel_offset_progressive(std::uint32_t frame,
                                            float& offset_x,
                                            float& offset_y,
                                            float temporal_phase_scale = 1.0f)
{
    const float s =
        temporal_phase_scale < 0.03f ? 0.03f : (temporal_phase_scale > 4.0f ? 4.0f : temporal_phase_scale);
    constexpr float inv_phi = 0.61803398874989484820459f;
    const float f = static_cast<float>(frame) * s;
    float u = std::fmod(0.5f + f * inv_phi, 1.0f);
    float v = std::fmod(0.5f + f * (inv_phi * inv_phi), 1.0f);
    if(u < 0.0f)
    {
        u += 1.0f;
    }
    if(v < 0.0f)
    {
        v += 1.0f;
    }
    offset_x = u - 0.5f;
    offset_y = v - 0.5f;
}

/** Halton(2), Halton(3) mapped to [-0.5, 0.5]. Uses @p frame + 1 so the first frame is non-degenerate. */
inline void taa_subpixel_offset_halton(std::uint32_t frame,
                                       float& offset_x,
                                       float& offset_y,
                                       float temporal_phase_scale = 1.0f)
{
    const float s =
        temporal_phase_scale < 0.03f ? 0.03f : (temporal_phase_scale > 4.0f ? 4.0f : temporal_phase_scale);
    // Larger effective index steps when s<1 so Halton visits similar to "slower phase" golden/R2.
    const float inv_s = 1.0f / s;
    const std::uint32_t i = std::max(1u, static_cast<std::uint32_t>(static_cast<float>(frame) * inv_s) + 1u);
    offset_x = halton(i, 2u) - 0.5f;
    offset_y = halton(i, 3u) - 0.5f;
}

/** R2 / recurrence-based low-discrepancy pair in [-0.5, 0.5]. */
inline void taa_subpixel_offset_r2(std::uint32_t frame,
                                   float& offset_x,
                                   float& offset_y,
                                   float temporal_phase_scale = 1.0f)
{
    const float s =
        temporal_phase_scale < 0.03f ? 0.03f : (temporal_phase_scale > 4.0f ? 4.0f : temporal_phase_scale);
    constexpr float a1 = 0.75487766624669276f;
    constexpr float a2 = 0.56984029099805327f;
    const float f = static_cast<float>(frame) * s;
    float u = std::fmod(0.5f + f * a1, 1.0f);
    float v = std::fmod(0.5f + f * a2, 1.0f);
    if(u < 0.0f)
    {
        u += 1.0f;
    }
    if(v < 0.0f)
    {
        v += 1.0f;
    }
    offset_x = u - 0.5f;
    offset_y = v - 0.5f;
}

inline std::uint32_t power_of_n_round_down(std::uint32_t val, std::uint32_t n)
{
    std::uint32_t currentVal = n;
    std::uint32_t iter = 1;
    while(currentVal < val)
    {
        currentVal *= n;
        ++iter;
    }

    return iter;
}

} // namespace math
