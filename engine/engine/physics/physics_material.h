#pragma once
#include <engine/engine_export.h>

#include <memory>

#include <type_traits>

namespace unravel
{

//------------------------------------------------------------------------------
// 1) Enum for per-body combine modes.
//    We assign explicit integer values so they map neatly into userIndex2.
//------------------------------------------------------------------------------
enum class combine_mode : int
{
    average = 0,  // PhysX default: (eA + eB)/2
    minimum = 1,  // min(eA, eB)
    multiply = 2, // Bullet default: eA * eB
    maximum = 3,  // max(eA, eB)
    count = 4
};

/**
 * @struct physics_material
 * @brief Represents the physical properties of a material.
 */
struct physics_material
{
    using sptr = std::shared_ptr<physics_material>; ///< Shared pointer to a physics material.
    using wptr = std::weak_ptr<physics_material>;   ///< Weak pointer to a physics material.
    using uptr = std::unique_ptr<physics_material>; ///< Unique pointer to a physics material.

    float restitution{0}; ///< Coefficient of restitution. Range: [0.0, 1.0].
    /// Tooltip:
    /// Restitution represents the bounciness of the material. A value of 0.0 means no bounce (perfectly
    /// inelastic collision), while 1.0 means perfect bounce (perfectly elastic collision).

    float friction{0.5}; ///< Coefficient of friction. Range: [0.0, 1.0] (sometimes slightly above 1.0).
    /// Tooltip:
    /// Friction represents the resistance to sliding motion. A value of 0.0 means no friction (perfectly
    /// slippery), while values around 1.0 represent typical real-world friction. Values slightly above 1.0
    /// can simulate very high friction surfaces but should be used cautiously.

    float stiffness{0.5}; ///< Normalized stiffness value. Range: [0.0, 1.0].
    /// Tooltip: Normalized stiffness value. Represents the elasticity of the material. Higher values indicate stiffer
    /// materials.

    float damping{0.1f}; ///< Coefficient of damping. Range: [0.0, 1.0].
    /// Tooltip: Coefficient of damping. Represents the material's resistance to motion. Higher values result in more
    /// energy loss.

    combine_mode restitution_combine{combine_mode::average}; ///< How to combine restitution values.

    combine_mode friction_combine{combine_mode::average}; ///< How to combine friction values.

    /**
     * @brief Converts normalized stiffness to actual stiffness.
     * @return The actual stiffness value.
     */
    auto get_stiffness() const -> float
    {
        const float min_stiffness = 1e3f; ///< Minimum actual stiffness.
        const float max_stiffness = 1e5f; ///< Maximum actual stiffness.
        return min_stiffness + stiffness * (max_stiffness - min_stiffness);
    }
};

} // namespace unravel
