#pragma once

#include <cstdint>

namespace unravel
{
class material;

/**
 * @class shadow_geometry_source
 * @brief Geometry built for one frame that casts a shadow without a model or a mesh: the Lines and Trails one
 * material draws.
 *
 * The shadow generator culls it by the world box of its caster entry and draws it once into each shadow view it
 * reaches, with the material's own shadow program when the material clips or moves its shadow.
 */
class shadow_geometry_source
{
public:
    virtual ~shadow_geometry_source() = default;

    /// Sets the geometry's buffers and world transform on the next draw. False, setting nothing, when it has
    /// nothing to draw this frame.
    [[nodiscard]] virtual auto bind_shadow_geometry() const -> bool = 0;

    /// The material whose mask clips the shadow. The geometry casts from both of its sides.
    [[nodiscard]] virtual auto get_shadow_material() const -> const material& = 0;

    /// What the shadow draws, as a key: a cached shadow map is reused while it does not change.
    [[nodiscard]] virtual auto get_shadow_key() const -> uint64_t = 0;
};

} // namespace unravel
