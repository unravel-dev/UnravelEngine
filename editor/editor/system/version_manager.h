#pragma once
#include <context/context.hpp>

#include <update_check/update_schedule.h>

#include <atomic>
#include <functional>
#include <memory>

namespace unravel
{
/**
 * @brief The editor's update check.
 *
 * At start, when Editor Settings allow it and the last attempt is old enough (update_check::is_check_due), and on
 * Help > Check for Updates..., it reads the latest release's latest.json on a pool thread and names a newer build of
 * this platform in a toast. The automatic check fails silently; the frame never waits for it.
 */
struct version_manager
{
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /// Help > Check for Updates...: always asks GitHub and always answers with a toast.
    void check_for_update_async();

private:
    void start_check(bool is_manual);
    void apply_outcome(const update_check::check_outcome& outcome);
    void announce_update(const update_check::release_build& build);
    void show_manual_answer(const update_check::check_outcome& outcome);
    void dismiss_update(const std::string& version);
    /// Applies `change` to the state as the file holds it now and saves it.
    void change_state(const std::function<void(update_check::update_state&)>& change);

    fs::path state_path_;
    update_check::update_state state_;
    hpp::optional<version::engine_version> own_version_;
    /// Shared with the check in flight and the toasts, which can outlive deinit.
    std::shared_ptr<std::atomic<bool>> is_stopping_ = std::make_shared<std::atomic<bool>>(false);
    bool is_checking_ = false;
    /// Whether the check in flight answers with a toast in any case: the user asked for it.
    bool is_manual_check_ = false;
};
} // namespace unravel
