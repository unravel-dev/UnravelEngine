#pragma once

#include <editor/editing/prefab_removal_record.h>

#include "editing_action.h"
#include "entt/meta/meta.hpp"
#include <engine/assets/asset_handle.h>
#include <engine/ecs/scene.h>
#include <base/basetypes.hpp>
#include <math/math.h>
#include <functional>
#include <sstream>
#include <uuid/uuid.h>
#include <vector>

namespace unravel
{

class material;

/**
 * @brief Undoable create for entity subtrees: invokes a user-supplied factory on first execution,
 * snapshots the newly-created roots, and restores them with identical UUIDs on redo.
 *
 * This action is intentionally narrow: it only owns the creation/destruction lifecycle. Selection
 * changes and any parent re-wiring that happens outside the factory are expected to be driven by
 * separate actions. Any parent assigned by the factory itself (e.g. scene::create_entity(name, parent))
 * is captured as part of the snapshot and re-applied on redo.
 */
struct create_entities_action_t : crtp_meta_type<create_entities_action_t, editing_action_t>
{
    /// Factory invoked on the first execution. Returns the newly-created root entities (may be empty).
    /// Cleared after the first execution; subsequent redos use the captured snapshot.
    std::function<std::vector<entt::handle>()> factory{};

    /// Serialized bytes of each root, captured after the first factory execution.
    std::vector<std::string> serialized_roots{};
    /// Handles of each created root; the registry pointer persists even after the entity is destroyed.
    std::vector<entt::uhandle> root_entities{};
    /// Parent of each root (may hold a nil uuid for root-level entities). Refreshed on every undo.
    std::vector<entt::uhandle> parent_entities{};
    /// Preorder UUIDs per root subtree (same ordering as save_to_stream / flatten_hierarchy).
    std::vector<std::vector<hpp::uuid>> subtree_uuids{};
    /// What destroying each root recorded on the prefab instance that contained it. Restoring
    /// the entity does not restore this - it lives on the container, not on the entity.
    std::vector<prefab_removal_record> removal_records{};

    /// True once the initial factory-driven creation has run and the snapshot is populated.
    bool captured{false};

    /// Multi-root constructor. The factory returns the set of newly-created roots.
    create_entities_action_t(std::function<std::vector<entt::handle>()> factory);

    /// Single-root convenience overload.
    create_entities_action_t(std::function<entt::handle()> factory);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    auto is_valid() const -> bool override;
};

/**
 * @brief Undoable delete for entity subtrees: snapshots serialized roots at construction, deletes on do, restores on undo.
 */
struct delete_entities_action_t : crtp_meta_type<delete_entities_action_t, editing_action_t>
{
    std::vector<std::string> serialized_roots{};
    std::vector<entt::uhandle> root_entities{};
    std::vector<entt::uhandle> parent_entities{};
    /// Preorder UUIDs per root subtree (same order as save_to_stream / flatten_hierarchy).
    std::vector<std::vector<hpp::uuid>> subtree_uuids{};
    /// What the deletion recorded on the prefab instance that contained each root. Restoring
    /// the entity does not restore this - it lives on the container, not on the entity.
    std::vector<prefab_removal_record> removal_records{};

    explicit delete_entities_action_t(std::vector<entt::handle> entities);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    auto is_valid() const -> bool override;
};

// Individual entity component actions
struct entity_add_component_action_t : crtp_meta_type<entity_add_component_action_t, editing_action_t>
{
    entt::uhandle entity{};
    entt::meta_type component_type{};

    bool do_was_successful{false};

    entity_add_component_action_t(entt::handle ent, const entt::meta_type& ctype);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct entity_remove_component_action_t : crtp_meta_type<entity_remove_component_action_t, editing_action_t>
{
    entt::uhandle entity{};
    entt::meta_type component_type{};

    bool do_was_successful{false};
    std::stringstream stream{};

    entity_remove_component_action_t(entt::handle ent, const entt::meta_type& ctype);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

// Individual entity component actions
struct entity_set_active_action_t : crtp_meta_type<entity_set_active_action_t, editing_action_t>
{
    entt::uhandle entity{};
    bool old_active{};
    bool new_active{};

    entity_set_active_action_t(entt::handle ent, bool old_active, bool new_active);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct entity_set_name_action_t : crtp_meta_type<entity_set_name_action_t, editing_action_t>
{
    entt::uhandle entity{};
    std::string old_name{};
    std::string new_name{};

    entity_set_name_action_t(entt::handle ent, const std::string& old_name, const std::string& new_name);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct entity_set_tag_action_t : crtp_meta_type<entity_set_tag_action_t, editing_action_t>
{
    entt::uhandle entity{};
    std::string old_tag{};
    std::string new_tag{};

    entity_set_tag_action_t(entt::handle ent, const std::string& old_tag, const std::string& new_tag);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct entity_set_materials_action_t : crtp_meta_type<entity_set_materials_action_t, editing_action_t>
{
    entt::uhandle entity{};
    std::vector<asset_handle<material>> old_materials{};
    std::vector<asset_handle<material>> new_materials{};

    entity_set_materials_action_t(entt::handle ent,
                                  const std::vector<asset_handle<material>>& old_materials,
                                  const asset_handle<material>& new_material);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct entity_set_text_bounds_action_t : crtp_meta_type<entity_set_text_bounds_action_t, editing_action_t>
{
    entt::uhandle entity{};
    fsize_t old_area{};
    fsize_t new_area{};

    entity_set_text_bounds_action_t(entt::handle ent, const fsize_t& old_area, const fsize_t& new_area);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct entity_set_ui_document_component_bounds_action_t : crtp_meta_type<entity_set_ui_document_component_bounds_action_t, editing_action_t>
{
    entt::uhandle entity{};
    usize32_t old_size{};
    usize32_t new_size{};

    entity_set_ui_document_component_bounds_action_t(entt::handle ent, const usize32_t& old_size, const usize32_t& new_size);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

// Script component specific actions
struct entity_add_script_component_action_t : crtp_meta_type<entity_add_script_component_action_t, editing_action_t>
{
    entt::uhandle entity{};
    std::string script_type_name{};

    bool do_was_successful{false};

    entity_add_script_component_action_t(entt::handle ent, const std::string& type_name);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct entity_remove_script_component_action_t : crtp_meta_type<entity_remove_script_component_action_t, editing_action_t>
{
    entt::uhandle entity{};
    std::string script_type_name{};
    int script_index{-1}; // Index of the script component to remove

    bool do_was_successful{false};
    // Store the script object data for restoration
    std::stringstream removed_script_object_data{};

    entity_remove_script_component_action_t(entt::handle ent, const std::string& type_name, int index = -1);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

} // namespace unravel
