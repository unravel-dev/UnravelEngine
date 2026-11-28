#pragma once

#include <engine/ecs/ecs.h>

#include <cstdint>

namespace unravel
{
class dynamic_renderer;
struct strip_style;

/// The slots the MaterialInstance of a Line and a Trail pass to the native calls: each has one instance, of its
/// material, and scripts name the two components by them. Mirror MaterialInstance.LineSlot and TrailSlot in
/// MaterialInstance.cs.
inline constexpr uint32_t LINE_MATERIAL_INSTANCE_SLOT = 0xFFFFFFFDu;
inline constexpr uint32_t TRAIL_MATERIAL_INSTANCE_SLOT = 0xFFFFFFFCu;

/// Whether @p slot names the one instance of a Line or a Trail.
inline auto is_dynamic_renderer_slot(uint32_t slot) -> bool
{
    return slot == TRAIL_MATERIAL_INSTANCE_SLOT || slot == LINE_MATERIAL_INSTANCE_SLOT;
}

/// The Line or the Trail of entity @p id that @p slot names; null when the entity has none.
auto find_dynamic_renderer(entt::entity id, uint32_t slot) -> dynamic_renderer*;

/// The strip of the Line or the Trail of entity @p id that @p slot names; null for an entity without the component.
auto find_dynamic_renderer_strip(entt::entity id, uint32_t slot) -> strip_style*;

} // namespace unravel
