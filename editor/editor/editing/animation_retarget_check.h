#pragma once

#include <engine/animation/animation_player.h>

#include <context/context.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace unravel
{
class animation_component;
class model_component;

/// Kinds of animation_binding_report::match_kind, for per-kind counts.
constexpr size_t RETARGET_MATCH_KIND_COUNT = 5;

/**
 * @brief A clip checked against a character the way it plays there: the engine's binding report,
 * the skeleton naming the character's bones, counts per kind of match, and what looks wrong, in
 * words. The Animation inspector's Retarget Check and the MCP tool animation_get_retarget show the
 * same check.
 */
struct animation_retarget_check
{
    animation_binding_report report;
    std::shared_ptr<const animation_target_skeleton> skeleton;
    /// Channels per animation_binding_report::match_kind.
    std::array<size_t, RETARGET_MATCH_KIND_COUNT> match_counts{};
    std::vector<std::string> warnings;
    /// Both skeletons are straightened to a T-pose first (Retarget Pose T-Pose).
    bool aligns_to_t_pose{};
    /// Turns the character's retarget pose adds to its bones.
    size_t pose_offset_count{};
};

/// Checks how `clip` binds onto `model`'s skeleton with `animation`'s retargeting settings.
auto check_animation_retarget(animation_component& animation,
                              const model_component& model,
                              const std::shared_ptr<animation_clip>& clip) -> animation_retarget_check;

/// The clip a check looks at by default: the one the base layer plays, else the one its controller
/// starts in, else the component's clip. A controller still loading is skipped unless
/// `waits_for_controller`.
auto get_default_retarget_clip(animation_component& animation, bool waits_for_controller = false)
    -> asset_handle<animation_clip>;

/// Display name of a kind of match ("name", "role"...).
auto get_match_kind_name(animation_binding_report::match_kind kind) -> const char*;

/// The name of `bone` in the check's skeleton, or "-" for none.
auto get_retarget_bone_name(const animation_retarget_check& check, int bone) -> std::string;

/// Channels that reached a bone, whatever the match.
auto count_matched_channels(const animation_retarget_check& check) -> size_t;

/// The key of the project model file `model` shows, whose import settings can be edited; empty
/// for engine models and models without a mesh.
auto get_editable_model_key(const model_component& model) -> std::string;

/// The humanoid roles the model's import settings set by hand.
auto get_model_humanoid_overrides(rtti::context& ctx, const std::string& model_key) -> std::vector<humanoid_bone_override>;

/**
 * @brief Sets one humanoid role in the model's import settings: `bone` names the bone that plays it
 * (empty: none), nullopt gives it back to the finder. The model and its clips are imported again.
 * @return False when the settings could not be written.
 */
auto set_model_humanoid_override(rtti::context& ctx,
                                 const std::string& model_key,
                                 humanoid_bone role,
                                 const std::optional<std::string>& bone) -> bool;

} // namespace unravel
