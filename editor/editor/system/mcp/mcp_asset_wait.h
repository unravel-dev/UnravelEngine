#pragma once

#include <context/context.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace unravel::mcp
{

/// Whether the asset a key names can be used yet.
enum class asset_readiness
{
    ready,
    loading,
    failed,
    missing,
};

auto readiness_to_string(asset_readiness state) -> const char*;

/**
 * The assets that keys held before an import or reimport overwrote them, by key.
 *
 * A reimport reloads into the same handle, so until the reload is published the handle still
 * returns the old asset and reads as ready. A wait compares against the asset kept here
 * (the identity version() reports); holding it, rather than its address, keeps the reloaded
 * asset from reusing the address and reading as unchanged.
 */
using asset_snapshot = std::map<std::string, std::shared_ptr<void>>;

/// Loads and holds what each existing key of @p keys holds now. It is loaded, not just peeked: an
/// asset nothing uses is not resident, and loading the stale compiled file later would read as new.
auto take_asset_snapshot(rtti::context& ctx, const std::vector<std::string>& keys) -> asset_snapshot;

/**
 * @brief Waits until the assets of @p keys are compiled and loaded, under one deadline.
 *
 * Keys are polled in turn in short slices, so one slow asset does not hold up noticing the others;
 * a @p timeout of 0 polls each key once. Loads are started for keys nothing has asked for yet.
 * A key that @p previous holds is ready once it is loaded as something else (see asset_snapshot).
 * Call from a worker thread, never from the main thread: the loads it waits for finish there.
 *
 * @return One state per key, in the order of @p keys.
 */
auto wait_assets_ready(rtti::context& ctx,
                       const std::vector<std::string>& keys,
                       std::chrono::milliseconds timeout,
                       const asset_snapshot& previous = {}) -> std::vector<asset_readiness>;

/// What a batch tool reports after waiting for its assets.
struct asset_wait_report
{
    /// Per key, the ',"state":"..."' field its result carries; empty for a key not waited for.
    std::vector<std::string> state_fields;
    size_t ready_count{0};
    bool waited{false};
};

/**
 * @brief Waits for the keys of @p keys that @p is_waited marks (wait_assets_ready) and describes
 * each one's state for the tool's result.
 *
 * A @p timeout of 0 waits for nothing: a caller that asked not to wait gets no states.
 */
auto wait_for_assets(rtti::context& ctx,
                     const std::vector<std::string>& keys,
                     const std::vector<bool>& is_waited,
                     std::chrono::milliseconds timeout,
                     const asset_snapshot& previous = {}) -> asset_wait_report;

} // namespace unravel::mcp
