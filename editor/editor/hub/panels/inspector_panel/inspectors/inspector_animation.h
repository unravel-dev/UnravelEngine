#pragma once
#include "inspector.h"

#include <engine/animation/ecs/components/animation_component.h>

#include <array>
#include <vector>

namespace unravel
{

/**
 * @brief Custom inspector for animation_component.
 *
 * Draws the reflected properties like any other component and adds a live
 * "Player" section: transport controls, per-layer playback timelines with
 * seeking, a detailed crossfade visualization driven by the player's actual
 * easing function, blend-space weights and a recent blend-activity trace.
 */
struct inspector_animation_component : public crtp_meta_type<inspector_animation_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;

private:
    /// Ring buffer of recent per-layer crossfade weights (target weight, 0..1).
    struct blend_trace
    {
        static constexpr size_t capacity = 180;
        std::array<float, capacity> samples{};
        size_t head{};
        size_t count{};
    };

    void draw_player_section(rtti::context& ctx, animation_component& data);
    void update_traces(const animation_player& player);
    void draw_trace(size_t layer_index) const;

    /// Identity of the player the traces belong to; reset on change.
    const animation_player* traced_player_{};
    std::vector<blend_trace> traces_;
    double last_trace_time_{};
};

REFLECT_INSPECTOR_INLINE(inspector_animation_component, animation_component)

} // namespace unravel
