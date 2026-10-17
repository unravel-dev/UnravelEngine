#pragma once

#include <engine/animation/animation_player.h>

#include <array>
#include <memory>
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

} // namespace unravel
