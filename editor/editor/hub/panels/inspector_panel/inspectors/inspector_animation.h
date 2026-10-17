#pragma once
#include "inspector.h"

#include <editor/editing/animation_retarget_check.h>
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
    /// The model's humanoid roles, each settable by hand (see set_model_humanoid_override).
    void draw_humanoid_section(rtti::context& ctx, animation_component& data);
    /// The role row's menu, opening down and to the left of `anchor` (its button's bottom right
    /// corner): back to the finder, no bone, or a bone of the model.
    void draw_role_menu(rtti::context& ctx,
                        const std::string& model_key,
                        humanoid_bone role,
                        const animation_target_skeleton& skeleton,
                        const ImVec2& anchor);
    /// How a clip lands on this model's skeleton (see check_animation_retarget).
    void draw_retarget_section(rtti::context& ctx, animation_component& data);
    void update_traces(const animation_player& player);
    void draw_trace(size_t layer_index) const;

    /// Identity of the player the traces belong to; reset on change.
    const animation_player* traced_player_{};
    std::vector<blend_trace> traces_;
    double last_trace_time_{};

    /// What a retarget check was made for: it is made again only when one of these changes.
    struct retarget_key
    {
        const animation_component* component{};
        hpp::uuid clip_uid{};
        uintptr_t clip_version{};
        uint64_t skeleton_version{};
        animation_retargeting_mode retargeting_mode{};
        animation_pose_retargeting pose_retargeting{};

        auto operator==(const retarget_key& other) const -> bool = default;
    };
    /// The clip the Retarget Check looks at; empty follows the clip playing.
    asset_handle<animation_clip> retarget_clip_;
    animation_retarget_check retarget_check_;
    retarget_key retarget_key_;
    bool shows_unmatched_only_{};
    /// Filters the bones a role menu lists.
    ImGuiTextFilter role_bone_filter_;
};

REFLECT_INSPECTOR_INLINE(inspector_animation_component, animation_component)

} // namespace unravel
