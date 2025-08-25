#pragma once

#include <engine/meta/ecs/entity.hpp>
#include <reflection/registration.h>
#include <string>

namespace unravel
{

// Base class for all editing actions
struct editing_action_t : crtp_meta_type<editing_action_t>
{
    virtual ~editing_action_t() = default;
    std::string name{};
    uint64_t merge_key{0};
    bool undoable{false};

    virtual void do_action() = 0;
    virtual void undo_action() = 0;
    virtual auto is_undoable() const -> bool { return undoable; } // Default: actions are undoable
    virtual auto is_mergeable(const editing_action_t& previous) const -> bool { return false; } // Default: actions are not mergeable
    virtual void merge_with(const editing_action_t& previous) {} // Default: no merge implementation
    
    // Note: Common merge checks (type equality, operation_id validation) are handled by undo_redo_stack.
    // Individual is_mergeable() implementations only need to check action-specific criteria.
};

} // namespace unravel
