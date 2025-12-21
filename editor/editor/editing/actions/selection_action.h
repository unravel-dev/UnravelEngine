#pragma once

#include "editing_action.h"
#include <entt/meta/meta.hpp>
#include <vector>

namespace unravel
{

struct editing_manager;

// Action for tracking selection changes
struct selection_action_t : crtp_meta_type<selection_action_t, editing_action_t>
{
    editing_manager* manager{nullptr};
    std::vector<entt::meta_any> old_selection{};
    std::vector<entt::meta_any> new_selection{};
    bool is_select{false};
    selection_action_t(editing_manager* mgr, 
                       const std::vector<entt::meta_any>& old_sel,
                       const std::vector<entt::meta_any>& new_sel, bool is_select);

    void do_action() override;
    void undo_action() override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;
    auto is_valid() const -> bool override;
};

} // namespace unravel

