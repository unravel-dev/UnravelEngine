#pragma once

#include "editing_action.h"
#include <memory>
#include <vector>

namespace unravel
{

struct undo_redo_stack
{
    std::vector<std::shared_ptr<editing_action_t>> actions;
    size_t current_index{0}; // Points to the last executed action (0 = no actions executed)
    std::string last_action_name;
    delta_t last_action_elapsed_time{};


    void push_if_undoable(std::shared_ptr<editing_action_t> action);

    auto can_undo() const -> bool;
    auto can_redo() const -> bool;
    
    auto undo() -> std::shared_ptr<editing_action_t>;
    auto redo() -> std::shared_ptr<editing_action_t>;
    void clear();
};

} // namespace unravel
