#pragma once

#include "editing_action.h"
#include "entt/meta/meta.hpp"

namespace unravel
{

 // Individual entity component actions
struct entity_add_component_action_t : crtp_meta_type<entity_add_component_action_t, editing_action_t>
{
    entt::handle entity{};
    entt::meta_type component_type{};

    bool do_was_successful{false};
    
    entity_add_component_action_t(entt::handle ent, entt::meta_type component_type);

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

    entity_remove_component_action_t(entt::handle ent, entt::meta_type component_type);

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


} // namespace unravel
