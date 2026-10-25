#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/fog_pass.h>

namespace unravel
{

/**
 * @brief A box or ellipsoid of fog placed in the world (fog_pass::volume_settings), oriented and stretched by its
 * entity's transform.
 *
 * Unlike a post-process volume, which blends settings at the camera's position, a fog volume is participating
 * medium where it stands: the volumetric fog lights it with the sun's shadows, the sky and the local lights; past the
 * volumetric fog, or without it, the analytic fog draws it unshadowed. It renders with a view's Fog component: a scene
 * without height fog can give that component a long attenuation distance to show the volumes alone.
 */
class fog_volume_component : public component_crtp<fog_volume_component>
{
public:
    bool enabled = true;
    fog_pass::volume_settings settings{};
};

} // namespace unravel
