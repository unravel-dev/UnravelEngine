#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <engine/frame_times.h>
#include <hpp/span.hpp>

namespace unravel
{

class model_system
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;
    void on_play_begin(rtti::context& ctx);
    void on_play_begin(hpp::span<const entt::handle> entities, delta_t dt);
    void on_frame_update(scene& scn, const frame_times& times);
    void on_frame_before_render(scene& scn, const frame_times& times);

private:
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
