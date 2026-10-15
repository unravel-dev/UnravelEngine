#pragma once

#include <engine/ecs/components/prefab_component.h>
#include <engine/meta/ecs/entity.hpp>

#include <entt/entity/handle.hpp>
#include <uuid/uuid.h>

#include <string>
#include <vector>

namespace unravel
{

/// Whether an instance root sits inside another instance.
auto is_nested_instance(entt::handle entity) -> bool;

/// A removal recorded on one instance: an entity of its prefab (by prefab uid) or a nested
/// instance (by its slot id), stated here or by a document containing the instance.
struct prefab_removal_entry
{
    hpp::uuid id;
    bool is_instance{};
    bool inherited{};
};

/// An entity under an instance that no prefab file of that instance contains.
struct prefab_addition_entry
{
    entt::handle handle;
    bool is_instance{};
    /// Added by the prefab that contains this instance rather than here.
    bool inherited{};
};

/**
 * @brief Everything recorded on one prefab instance: what is stated about its content and by
 *        whom, what was removed from it and what was added under it.
 */
struct prefab_instance_changes
{
    entt::handle root;
    std::string asset_name;
    /// The instance the collection started from, rather than one nested in it.
    bool is_self{};
    statements_about_instance about;
    std::vector<prefab_removal_entry> removals;
    std::vector<prefab_addition_entry> additions;
    size_t local_count{};
    size_t inherited_count{};
    size_t added_count{};
};

/**
 * @brief The root (when it is an instance) plus every instance nested below it, each with its
 *        changes, in hierarchy order.
 *
 * The first entry is always the root's own. An authoring root - the content of the prefab being
 * edited - is not an instance: its entry only collects direct additions.
 */
auto collect_prefab_changes(entt::handle root) -> std::vector<prefab_instance_changes>;

/**
 * @brief Drops everything stated here on one instance, so its next sync puts it back to exactly
 *        what the prefabs say. What a containing document states stays; added entities stay.
 */
void revert_local_changes(prefab_component& prefab_comp);

/**
 * @brief Writes an instance into its prefab file and re-homes what it carried: what was stated
 *        here about the nested content is the document's statement now, what was stated about
 *        this instance's own content is its content.
 *
 * One override survives - a nested root's own placement, which is the containing document's to
 * state; dropping it would let the container's next replay snap the instance back to where it
 * was before the move. Does not sync; the caller does (editing_manager::sync_after_override_change).
 */
void apply_instance_to_prefab(entt::handle root);

/**
 * @brief Unlinks an entity from its prefab: it stays, with every value it has now, as plain
 *        entities. Removing an instance root's prefab_component strips the prefab ids down its
 *        subtree (the component's destroy hook).
 */
void unlink_from_prefab(entt::handle entity);

} // namespace unravel
