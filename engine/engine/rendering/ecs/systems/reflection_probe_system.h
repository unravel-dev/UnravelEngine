#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <hpp/span.hpp>

namespace unravel
{
class reflection_probe_system
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void on_frame_update(scene& scn, delta_t dt);
    void on_play_begin(hpp::span<const entt::handle> entities, delta_t dt);

    /**
     * @brief Flags every reflection probe in the scene for rebuild.
     *
     * Used by editor tooling and scripted bakes. When force_full_first_frame is true,
     * each probe bakes all six faces in a single frame (no time slicing) -
     * pick true for explicit "Bake now" actions and false for background rebuilds
     * where smooth amortization is preferred.
     */
    static auto mark_all_dirty(scene& scn, bool force_full_first_frame = false) -> size_t;

private:
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
