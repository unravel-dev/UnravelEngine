#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <engine/frame_times.h>
#include <hpp/span.hpp>
#include <math/math.h>

#include <vector>

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

    /// The frustums of the scene's active cameras this frame, copied: they resolve lazily, so tasks read the copies.
    static auto get_camera_frustums(scene& scn) -> std::vector<math::frustum>;
    /// Whether @p bounds is in any of @p view_frustums: a model that no view used last frame is entering one.
    static auto is_in_any_view(const std::vector<math::frustum>& view_frustums, const math::bbox& bounds) -> bool;

private:
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
