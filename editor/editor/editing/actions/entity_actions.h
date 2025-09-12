#pragma once

#include "editing_action.h"
#include "entt/meta/meta.hpp"
#include <engine/assets/asset_handle.h>
#include <base/basetypes.hpp>
#include <math/math.h>
#include <sstream>

namespace unravel
{

class material;

// Individual entity component actions
struct entity_add_component_action_t : crtp_meta_type<entity_add_component_action_t, editing_action_t>
{
    entt::handle entity{};
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
    entt::handle entity{};
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
    entt::handle entity{};
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
    entt::handle entity{};
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
    entt::handle entity{};
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
    entt::handle entity{};
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
    entt::handle entity{};
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

// Script component specific actions
struct entity_add_script_component_action_t : crtp_meta_type<entity_add_script_component_action_t, editing_action_t>
{
    entt::handle entity{};
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
    entt::handle entity{};
    std::string script_type_name{};

    bool do_was_successful{false};
    // Store the script object data for restoration
    std::stringstream removed_script_object_data{};

    entity_remove_script_component_action_t(entt::handle ent, const std::string& type_name);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

} // namespace unravel
