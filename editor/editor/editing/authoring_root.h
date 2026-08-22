#pragma once

#include <entt/entity/handle.hpp>

namespace unravel
{

/**
 * @brief Marks the root of the prefab being edited - prefab mode, or the content browser's
 *        prefab inspector.
 *
 * That root is loaded by instantiating the prefab, so it is an instance of the very file it
 * edits: prefab_component with source = that file, same as any instance in a scene. Which is
 * the model chosen on purpose - prefab mode is a scene holding one instance, and Save is Apply
 * All - with one structural difference the tag records: the root is *upstream* of its file.
 * Its live state is the file's next content, not something derived from the file.
 *
 * Two things follow, each enforced at a single funnel:
 * - It is never synced against its file (editing_manager::sync_prefab_entity): a replay can
 *   only restore what was last saved, which undoes any revert made since.
 * - Its own content is never recorded as overrides of its file
 *   (prefab_override_context::find_prefab_root_entity): the user is editing the file, not
 *   deviating from it.
 *
 * Editor-only and never serialized. Not in all_serializeable_components, so it costs nothing
 * per entity in any document.
 */
struct authoring_root_tag
{
};

inline auto is_authoring_root(entt::handle entity) -> bool
{
    return entity && entity.all_of<authoring_root_tag>();
}

} // namespace unravel
