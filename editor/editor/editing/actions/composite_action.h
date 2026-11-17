#pragma once

#include "editing_action.h"
#include <functional>
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

/**
 * @brief Single-undo container for a chain of DEPENDENT actions.
 *
 * Where composite_action_t requires every sub-action to be fully constructed up-front,
 * sequence_action_t accepts "step factories" so later steps can reference values
 * produced by earlier ones (e.g. an entity handle that only exists after the preceding
 * create step has run).
 *
 * Lifecycle:
 *  - First do_action: each factory is invoked in order; the returned sub-action is
 *    executed immediately before moving to the next factory, then retained for redos.
 *  - Subsequent do_action (redo): replays retained sub-actions in order.
 *  - undo_action: walks retained sub-actions in reverse and undoes each, matching
 *    composite_action_t semantics.
 *  - is_valid: the first sub-action's; each later one is checked when its turn comes, since
 *    it may act on an entity an earlier step creates (gone while the chain is undone).
 *
 * Factories returning nullptr are silently skipped, allowing conditional steps. A sequence whose
 * factories all returned nullptr changed nothing: it is no undo step and leaves the scene saved.
 */
struct sequence_action_t : crtp_meta_type<sequence_action_t, editing_action_t>
{
    /// Factories evaluated lazily on the first execution. Cleared once consumed.
    std::vector<std::function<std::shared_ptr<editing_action_t>()>> step_factories;
    /// Captured sub-actions after first execution, replayed on redo and reversed on undo.
    std::vector<std::shared_ptr<editing_action_t>> sub_actions;

    bool first_run_done{false};

    /// Append a step to the sequence. Factory is invoked only on the first execution.
    void add_step(std::function<std::shared_ptr<editing_action_t>()> factory);

    void do_action() override;
    void undo_action() override;
    auto is_valid() const -> bool override;
    auto is_mergeable(const editing_action_t& previous) const -> bool override;
    auto is_undoable() const -> bool override;
    auto modifies_scene_content() const -> bool override;
    void draw_in_inspector(rtti::context& ctx) override;
};

} // namespace unravel
