#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <math/math.h>

namespace unravel
{

enum class volume_mode : uint8_t
{
    local,
    global,
};

/**
 * @class volume_component
 * @brief Spatial volume that applies post-processing effects when the camera is inside.
 * Effect settings come from sibling components (bloom_component, tonemapping_component, etc.)
 * on the same entity.
 */
class volume_component : public component_crtp<volume_component>
{
public:
    /**
     * @brief Volume mode: local uses bounds, global affects camera everywhere.
     */
    volume_mode mode = volume_mode::local;
    /**
     * @brief Higher priority wins when multiple volumes overlap.
     */
    int priority = 0;
    /**
     * @brief Influence multiplier in range [0, 1].
     */
    float weight = 1.0f;
    /**
     * @brief Distance outside the volume over which blending ramps from 0 to 1 (local volumes only).
     * Contribution is 1 at the boundary and inside; ramps down to 0 over this distance when outside.
     */
    float blend_distance = 1.0f;
    /**
     * @brief Half-extents for local box bounds, centered at entity origin.
     */
    math::vec3 extents = {1.0f, 1.0f, 1.0f};
    /**
     * @brief Gets local bounding box (before transform).
     * @return Bounding box with min=-extents, max=extents.
     */
    auto get_local_bounds() const -> math::bbox;
};

} // namespace unravel
