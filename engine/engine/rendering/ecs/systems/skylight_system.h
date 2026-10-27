#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <hpp/span.hpp>

namespace unravel
{
/**
 * @brief Finds the one skylight that lights a scene.
 *
 * With several scenes loaded the main scene's sky wins, as the active scene's does in other engines: a skylight
 * from a scene loaded next to it, or from a persistent root, counts only while the main scene has no active
 * skylight. Among those that count the strongest irradiance wins. Every consumer reads this one skylight, so the
 * drawn sky, the ambient light and the GI agree.
 * @param registry The scene's registry.
 * @return The skylight's entity, or an invalid handle when no skylight is active.
 */
auto find_skylight_in_effect(entt::registry& registry) -> entt::handle;

class skylight_system
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void on_frame_update(scene& scn, delta_t dt);
    void on_play_begin(hpp::span<const entt::handle> entities, delta_t dt);

private:
    /// Sets each light's atmosphere transmittance for this frame: the sky's atmosphere sun gets it, every other light 1.
    void update_atmosphere_sun_transmittance(scene& scn);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
