#pragma once

/// Stopping a running deploy early.
namespace unravel::deployment
{

/// The error of a step that stopped, or never started, because its deploy was cancelled.
constexpr const char* CANCELLED_STEP_ERROR = "Cancelled.";

/// Asks the running deploy to stop: its steps that have not started are skipped, and its long ones stop
/// between two of their items. Any thread.
void request_deploy_cancel();

/// Forgets an earlier request; a deploy calls it as it starts.
void reset_deploy_cancel();

/// Whether the deploy was asked to stop. Stays true until the next deploy starts. Any thread.
auto is_deploy_cancelled() -> bool;

/// Throws CANCELLED_STEP_ERROR when the deploy was asked to stop.
void throw_if_deploy_cancelled();

} // namespace unravel::deployment
