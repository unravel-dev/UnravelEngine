#pragma once
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <hpp/span.hpp>

namespace unravel
{
class transform_system
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;
    void on_frame_update(scene& scn, delta_t dt);
    void on_play_begin(hpp::span<const entt::handle> entities, delta_t dt);

private:
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
