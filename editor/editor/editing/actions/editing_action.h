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
    uint64_t merge_key{0};
    bool undoable{false};
    std::string name{};
    uint64_t execution_count{0};

    virtual auto get_name() const -> const std::string& { return name; }
    virtual void do_action() = 0;
    virtual auto get_execution_count() const -> uint64_t { return execution_count; }
    virtual void undo_action() = 0;
    virtual auto is_undoable() const -> bool { return undoable; } // Default: actions are undoable
    virtual auto is_mergeable(const editing_action_t& previous) const -> bool { return false; } // Default: actions are not mergeable
    virtual void merge_with(const editing_action_t& previous) {} // Default: no merge implementation
    virtual auto is_valid() const -> bool { return true; } // Default: actions are valid
    virtual void draw_in_inspector(rtti::context& ctx) {} // Default: no inspector drawing
    /// Called after first execution to release scope-bound references.
    /// Actions holding proxies should switch to resolver-based access.
    virtual void detach() {}
    /// Whether this action mutates scene content (i.e. something that could invalidate reflection bakes,
    /// auto-save state, etc.). Overridden to false by selection/focus-only actions.
    virtual auto modifies_scene_content() const -> bool { return true; }
    
    // Note: Common merge checks (type equality, operation_id validation) are handled by undo_redo_stack.
    // Individual is_mergeable() implementations only need to check action-specific criteria.

protected:
    void draw_in_inspector_impl(rtti::context& ctx, const entt::meta_any& old_value, const entt::meta_any& new_value, const entt::meta_custom& custom);
};

} // namespace unravel
