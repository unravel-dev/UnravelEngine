#pragma once

#include "editing_action.h"
#include "composite_action.h"
#include "entt/meta/meta.hpp"
#include <engine/ecs/components/transform_component.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector.h>
#include <math/math.h>

namespace unravel
{

// Individual transform component actions
struct transform_move_action_t : crtp_meta_type<transform_move_action_t, editing_action_t>
{
    entt::handle entity;
    math::vec3 old_position;
    math::vec3 new_position;
    
    transform_move_action_t(entt::handle ent, const math::vec3& old_pos, const math::vec3& new_pos);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct transform_move_global_action_t : crtp_meta_type<transform_move_global_action_t, editing_action_t>
{
    entt::handle entity;
    math::vec3 old_position;
    math::vec3 new_position;
    
    transform_move_global_action_t(entt::handle ent, const math::vec3& old_pos, const math::vec3& new_pos);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct transform_rotate_action_t : crtp_meta_type<transform_rotate_action_t, editing_action_t>
{
    entt::handle entity;
    math::quat old_rotation;
    math::quat new_rotation;
    
    transform_rotate_action_t(entt::handle ent, const math::quat& old_rot, const math::quat& new_rot);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct transform_scale_action_t : crtp_meta_type<transform_scale_action_t, editing_action_t>
{
    entt::handle entity;
    math::vec3 old_scale;
    math::vec3 new_scale;
    
    transform_scale_action_t(entt::handle ent, const math::vec3& old_sc, const math::vec3& new_sc);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

struct transform_skew_action_t : crtp_meta_type<transform_skew_action_t, editing_action_t>
{
    entt::handle entity;
    math::vec3 old_skew;
    math::vec3 new_skew;
    
    transform_skew_action_t(entt::handle ent, const math::vec3& old_sk, const math::vec3& new_sk);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};


} // namespace unravel
