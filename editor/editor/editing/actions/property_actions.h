#pragma once

#include "editing_action.h"
#include "composite_action.h"
#include "entt/meta/meta.hpp"
#include <engine/ecs/components/transform_component.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector.h>
#include <math/math.h>

namespace unravel
{


struct property_action_t : crtp_meta_type<property_action_t, editing_action_t>
{
    meta_any_proxy instance;
    entt::meta_any old_value;
    entt::meta_any new_value;
    entt::meta_custom custom;
    std::function<void()> on_success;
    /// Run after the value is put back on undo; on_success runs on do and redo.
    std::function<void()> on_undo;
    property_action_t(meta_any_proxy inst, const entt::meta_any& old_val, const entt::meta_any& new_val, const entt::meta_custom& custom = {}, const std::function<void()>& on_success = {}, const std::function<void()>& on_undo = {});

    void do_action() override;
    void undo_action() override;
    void detach() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

} // namespace unravel
