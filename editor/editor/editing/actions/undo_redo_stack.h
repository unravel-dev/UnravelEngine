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

    void push_if_undoable(std::shared_ptr<editing_action_t> action);

    auto can_undo() const -> bool;
    auto can_redo() const -> bool;
    
    void undo();
    void redo();
    void clear();
};

} // namespace unravel
