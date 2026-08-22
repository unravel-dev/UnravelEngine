#pragma once

#include "basic_component.h"
#include "id_component.h"
#include <engine/assets/asset_handle.h>
#include <unordered_map>
#include <map>
#include <string>
#include <string_view>
#include <set>
#include <vector>
#include <uuid/uuid.h>

namespace unravel
{

/**
 * @struct prefab_property_override_data
 * @brief One overridden property: which entity, which property - and, for a statement kept on
 *        an instance root about content nested below it, the chain of slots leading to the
 *        instance that entity belongs to.
 *
 * instance_path is empty for the root's own direct content. It is what lets a document keep its
 * statements about nested content on its own root rather than on the nested instance, which
 * is what keeps every list single-authored (see prefab_statements).
 */
struct prefab_property_override_data
{
    std::vector<hpp::uuid> instance_path;
    hpp::uuid entity_uuid;              // Entity UUID for stable identification
    std::string component_path;         // Component type + property path (e.g., "transform_component/position/x")
    std::string pretty_component_path;  // Human-readable component path (e.g., "Transform/Position/X")

    prefab_property_override_data() = default;
    prefab_property_override_data(const hpp::uuid& uuid, const std::string& path);
    prefab_property_override_data(const hpp::uuid& uuid, const std::string& path, const std::string& pretty_path);
    prefab_property_override_data(std::vector<hpp::uuid> path,
                                  const hpp::uuid& uuid,
                                  const std::string& component,
                                  const std::string& pretty_path);

    auto operator==(const prefab_property_override_data& other) const -> bool;
    auto operator<(const prefab_property_override_data& other) const -> bool;
};

/**
 * @brief A removed entity (by prefab uid) or a removed nested instance (by slot id), addressed
 *        the same way an override is: the chain of slots from the list's root, then the id.
 */
struct prefab_statement_target
{
    std::vector<hpp::uuid> instance_path;
    hpp::uuid id;

    auto operator==(const prefab_statement_target& other) const -> bool;
    auto operator<(const prefab_statement_target& other) const -> bool;
};

/**
 * @brief What one author states about the content under an instance root.
 *
 * A statement is an override, a removed entity or a removed nested instance. Each names its
 * target relative to the root the list sits on: the chain of slots down to the instance the
 * target belongs to (empty for the root's direct content), then the target.
 *
 * One author per list, and the list lives with the author: a document's statements sit on the
 * root of its own instances (prefab_component::from_document), what was stated in a scene sits
 * on the nearest instance root (prefab_component::local). Nothing merges two authors into one
 * set, so nothing has to be attributed afterwards - a replay replaces its document's list
 * wholesale and leaves every other list alone.
 */
struct prefab_statements
{
    std::set<prefab_property_override_data> overrides;
    std::set<prefab_statement_target> removed_entities;
    std::set<prefab_statement_target> removed_instances;

    auto empty() const -> bool;
    void clear();
    void merge(const prefab_statements& other);

    /**
     * @brief Adds an override, collapsing along the property path: an existing override on an
     *        ancestor path of the new one is replaced, and a new override on an ancestor path
     *        of an existing one is not added.
     */
    void add_override(const std::vector<hpp::uuid>& instance_path,
                      const hpp::uuid& entity_uuid,
                      const std::string& component_path,
                      const std::string& pretty_component_path);
    void remove_override(const std::vector<hpp::uuid>& instance_path,
                         const hpp::uuid& entity_uuid,
                         const std::string& component_path);
    auto has_override(const std::vector<hpp::uuid>& instance_path,
                      const hpp::uuid& entity_uuid,
                      const std::string& component_path) const -> bool;

    /**
     * @brief Whether an override lies on, above, or below a point in an entity's properties.
     *
     * Overrides name a point in a tree of properties, and reading one is a walk down that
     * tree - so both directions count. "tag_component" has to be let through to reach an
     * override on "tag_component/name", and "transform/position/x" has to be let through
     * because of an override on "transform/position".
     */
    auto has_override_touching(const std::vector<hpp::uuid>& instance_path,
                               const hpp::uuid& entity_uuid,
                               std::string_view component_path) const -> bool;

    /**
     * @brief Whether an override lies on a point in an entity's properties or above it - but
     *        not below.
     *
     * What protects a value from a replay. A replay asks per level - "transform" as a whole,
     * then each field inside it - so an override on a field must not shield its siblings: the
     * level above passes, and each field decides for itself. An override on the level itself,
     * or on any level above, does shield everything under it.
     */
    auto has_override_on_or_above(const std::vector<hpp::uuid>& instance_path,
                                  const hpp::uuid& entity_uuid,
                                  std::string_view component_path) const -> bool;

    /// Records the removal and drops every override on that entity: nothing is left to
    /// override, and a revert must not bring the overrides back without the entity.
    void remove_entity(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& entity_uuid);
    void restore_entity(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& entity_uuid);
    auto is_entity_removed(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& entity_uuid) const -> bool;

    void remove_instance(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& instance_id);
    void restore_instance(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& instance_id);
    auto is_instance_removed(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& instance_id) const -> bool;

    /// The entries about the content at `prefix` and everything below it, with `prefix`
    /// stripped - the list as seen from that instance.
    auto rebased(const std::vector<hpp::uuid>& prefix) const -> prefab_statements;
    /// The entries about exactly the content at `prefix`, stripped to direct-content entries.
    auto at(const std::vector<hpp::uuid>& prefix) const -> prefab_statements;
    /// The entries about the root's own direct content (empty path).
    auto direct() const -> prefab_statements;
    /// The entries about nested content (non-empty path).
    auto nested_only() const -> prefab_statements;
    /// This list with `prefix` prepended to every path - as seen from `prefix` levels up.
    auto prefixed(const std::vector<hpp::uuid>& prefix) const -> prefab_statements;
};

/**
 * @brief Who placed a nested prefab instance where it stands - for instances that have no
 *        slot yet.
 *
 * A named slot says whose it is (instance_document). An *unnamed* instance does not, and two
 * kinds are unnamed: one placed by hand, cloned, or added by an outer document - which the
 * containing asset must never claim or name - and one the containing asset supplied from a
 * file written before slots existed - which that asset alone may name and claim. This tells
 * them apart until every asset has been re-saved with slots; nothing else reads it.
 *
 * `unknown` is what a file written before this existed says, and reads as "may be claimed".
 */
enum class instance_placement : uint8_t
{
    unknown = 0,
    container = 1,
    other = 2,
};

/**
 * @struct prefab_component
 * @brief Marks an entity as the root of a prefab instance: the link to the asset, the slot the
 *        containing document gave it, and the two statement lists that live on it.
 */
struct prefab_component : public component_crtp<prefab_component, owned_component>
{
    static constexpr bool in_place_delete = false;

    /// Who placed this instance, while it has no slot. See instance_placement.
    instance_placement placed_by{instance_placement::unknown};

    /**
     * @brief Handle to the prefab asset.
     */
    asset_handle<prefab> source;

    /**
     * @brief Which nested instance of the document that produced this one it is.
     *
     * The only thing that can say. Prefab ids inside a nested instance come from the nested
     * asset, so two instances of one prefab - or an instance and a copy of it - are identical
     * by every other measure. Scoped to the containing document, unlike id_component (global)
     * and prefab_id_component (scoped to the asset the entity came from).
     *
     * Allocated once, when a subtree is written as a prefab file, and preserved from then on:
     * every instance of that file carries the same id for the same slot, which is what lets
     * the file name a slot and be understood by all of them. Regenerated on clone, because a
     * copy is a different slot.
     *
     * Nil means this instance was added where it stands rather than coming from a file - or
     * that the file predates instance ids. Both read the same way, and it is the safe
     * reading: a document only removes instances it can name.
     */
    hpp::uuid instance_id;

    /**
     * @brief The asset uid of the document that placed this instance - whose slot it is.
     *
     * Issued together with instance_id, at that document's save, and never changed by any
     * other document's save. What lets an asset's sync remove a slot it dropped and leave
     * alone one an outer document added inside it: the two are told apart by whose name the
     * slot carries, the same way an entity's prefab id names the document that introduced it.
     */
    hpp::uuid instance_document;

    /**
     * @brief What this instance's own document states about the content nested inside it.
     *
     * Loaded with the document and replaced wholesale by every replay of it; never written to
     * by editing here. Paths are non-empty: a document never states anything about its own
     * direct content - that is content. What an *outer* document states about this instance
     * sits on that document's root, not here.
     */
    prefab_statements from_document;

    /**
     * @brief What was stated here, in this scene, about content whose nearest instance root
     *        this is - overrides, removed entities, removed nested instances.
     *
     * Never touched by any replay. Paths are empty, with one exception: the root of a document
     * being edited (prefab mode) adopts its document's list here, so the author's own
     * statements about nested content read as local - editable, revertable - while editing.
     * A save folds them, and every nested root's local list, back into the document's list.
     */
    prefab_statements local;

    bool changed = false;

    /**
     * @brief Clears both lists. For an instance made from scratch, or re-linked to a prefab.
     */
    void clear_overrides();

    /**
     * @brief Add a property override on this instance's own content (local list)
     * @param entity_uuid The UUID of the entity being overridden
     * @param component_path The component type + property path
     */
    void add_override(const hpp::uuid& entity_uuid, const std::string& component_path);

    /**
     * @brief Add a property override with pretty path (local list), collapsing along the path
     */
    void add_override(const hpp::uuid& entity_uuid, const std::string& component_path, const std::string& pretty_component_path);

    /**
     * @brief Whether this property is overridden here (local list, direct content).
     */
    auto has_override(const hpp::uuid& entity_uuid, const std::string& component_path) const -> bool;

    /**
     * @brief Remove a property override from the local list.
     */
    void remove_override(const hpp::uuid& entity_uuid, const std::string& component_path);

    /**
     * @brief Record the removal of one of this instance's entities here (local list).
     */
    void remove_entity(const hpp::uuid& entity_uuid);

    /**
     * @brief Record that a nested instance was removed from this one here (local list)
     * @param instance_id The instance id of the nested instance, not its prefab uid
     */
    void remove_instance(const hpp::uuid& instance_id);

    /**
     * @brief Whether a nested instance was removed from this one here (local list)
     */
    auto is_instance_removed(const hpp::uuid& instance_id) const -> bool;

    /**
     * @brief has_override_touching on the local list, for this instance's direct content.
     */
    auto has_override_touching(const hpp::uuid& entity_uuid, std::string_view component_path) const -> bool;

    /**
     * @brief Check if a serialization path has a local override
     * @param serialization_path The path in format "entities/uuid/components/component/path"
     */
    auto has_serialization_override(const std::string& serialization_path) const -> bool;
};

/**
 * @struct prefab_id_component
 * @brief An entity's identity within the prefab document that introduced it.
 *
 * The id is unique within that document; the document says which one. Both are issued
 * together when the document is saved, for an entity that has none, and the pair is never
 * changed by another document's save - so content an outer document added inside a nested
 * instance keeps the outer document's name wherever it sits, and the nested asset's sync can
 * tell it from its own content without being told.
 */
 struct prefab_id_component : public id_component_base<prefab_id_component>
 {
    /// The asset uid of the document that introduced this entity. Nil for an id issued before
    /// documents were named; attributed on load (qualify_legacy_prefab_ids).
    hpp::uuid document;
 };


} // namespace unravel
