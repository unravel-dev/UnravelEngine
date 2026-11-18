#pragma once

#include <context/context.hpp>
#include <entt/entity/handle.hpp>
#include <hpp/string_view.hpp>

#include <memory>
#include <string>
#include <vector>

namespace unravel
{

struct editing_action_t;

/// Where pasted entities land.
enum class paste_placement
{
    /// Right after the anchor among its siblings, keeping their world pose. Without an anchor: right after the entity
    /// each was copied from when the scene has it, else last under its old parent when the scene has that, else last
    /// at the scene root.
    next_to,
    /// Last under the anchor, keeping their transform relative to their parent.
    child_keep_local,
    /// Last under the anchor, keeping their world pose.
    child_keep_world,
    /// Each right after the entity it was copied from, keeping its world pose: a duplicate.
    next_to_source,
    /// Last at the scene root, keeping their world pose.
    scene_root,
};

/// What a paste made, or why it made nothing.
struct paste_result
{
    /// The pasted roots, in the order they were copied.
    std::vector<entt::handle> roots;
    /// Why nothing was pasted, as a sentence; empty when the paste ran.
    std::string problem;
};

/**
 * @brief Copy and paste of entities through a text document: the OS clipboard's, or one a caller keeps.
 *
 * A copy holds the top-level entities given and everything under them as save_clone_to_stream writes them (uuids,
 * so it pastes into any scene, prefab or project, at any later time), with where each came from. A paste is one undo
 * step that makes new entities with fresh ids: references inside the copy land on the copy, references out of it on
 * the entity of the scene with that id or on nothing.
 */
namespace entity_clipboard
{

/// The copy document of the top-level entities among @p entities, in hierarchy order; empty when there are none.
auto copy(const std::vector<entt::handle>& entities) -> std::string;

/// Whether @p text is a copy of entities, by its head; it may still be refused when pasted.
auto holds_entities(hpp::string_view text) -> bool;

/**
 * @brief The undo step that pastes @p text into the scene being edited where @p placement says and selects what it
 *        pasted. Null, with the reason as a sentence in @p result, when @p text is no copy this editor reads, no
 *        scene is open, or the copy would put a prefab inside itself.
 * @param anchor The entity a paste lands next to or under; empty for none.
 * @param result Filled when the step first runs.
 */
auto make_paste_action(rtti::context& ctx,
                       std::string text,
                       paste_placement placement,
                       entt::handle anchor,
                       const std::shared_ptr<paste_result>& result) -> std::shared_ptr<editing_action_t>;

/// The undo step that copies @p entities right after themselves and selects the copies; null when there are none.
auto make_duplicate_action(rtti::context& ctx,
                           const std::vector<entt::handle>& entities,
                           const std::shared_ptr<paste_result>& result) -> std::shared_ptr<editing_action_t>;

/// Whether the OS clipboard holds a copy of entities.
auto clipboard_holds_entities() -> bool;

/// Puts a copy of @p entities on the OS clipboard.
void copy_to_clipboard(const std::vector<entt::handle>& entities);

/// Puts a copy of @p entities on the OS clipboard and deletes them, as one undo step queued for this frame's actions.
void cut_to_clipboard(rtti::context& ctx, const std::vector<entt::handle>& entities);

/// Queues the paste of the OS clipboard, next to or under @p anchor. Logs why when it cannot.
void paste_from_clipboard(rtti::context& ctx, paste_placement placement, entt::handle anchor);

/// Queues the duplicate of @p entities.
void duplicate(rtti::context& ctx, const std::vector<entt::handle>& entities);

} // namespace entity_clipboard
} // namespace unravel
