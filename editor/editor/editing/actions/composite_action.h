#pragma once

#include "editing_action.h"
#include <memory>
#include <vector>

namespace unravel
{

// Composite action base class - contains multiple sub-actions
struct composite_action_t : crtp_meta_type<composite_action_t, editing_action_t>
{
    std::vector<std::shared_ptr<editing_action_t>> sub_actions;
    
    void add_sub_action(std::shared_ptr<editing_action_t> action);
    void draw_in_inspector(rtti::context& ctx) override;
    void do_action() override;
    void undo_action() override;
    auto is_valid() const -> bool override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    void merge_with(const editing_action_t& previous) override;


};

} // namespace unravel
