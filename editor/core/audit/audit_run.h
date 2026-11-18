#pragma once

#include "audit_config.h"
#include "audit_facts.h"
#include "audit_input.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace unravel::audit
{

/// The stages of a run, in the order they run. The editor takes the snapshot and loads the documents on its main
/// thread; run_audit does the rest.
enum class run_stage : std::uint8_t
{
    /// Reading the project: its assets, settings and profiles.
    snapshot,
    /// Loading scenes, prefabs, materials and controllers.
    documents,
    /// Following the references between assets.
    references,
    /// Finding what each profile's deploy ships.
    content,
    /// Reading the compiled files of textures, clips and meshes.
    assets,
    /// Analyzing the compiled scripts.
    scripts,
    /// Checking the rules and writing the report.
    rules,
};

constexpr std::size_t RUN_STAGE_COUNT = 7;

/// What a stage does, as the run's progress lists it: "Loading scenes and prefabs".
auto get_run_stage_title(run_stage stage) -> const char*;

/// Where a stage is: how many of its items are done, of how many, and the one it is on.
struct stage_report
{
    run_stage stage{run_stage::snapshot};
    std::size_t done{};
    /// 0 when the stage cannot count its work.
    std::size_t total{};
    /// The item the stage is on ("app:/data/Scenes/Main.spfb", "Android"); empty when it has none.
    std::string item;
};

/// How a run reports progress and learns it should stop.
struct run_options
{
    /// Asked between steps and between assets; true ends the run early with what it has.
    std::function<bool()> should_stop;
    /// Gets where the run is each time a stage starts or moves on. Called on the run's thread.
    std::function<void(const stage_report& report)> on_progress;
    /// Facts read in earlier runs; may be null.
    facts_cache* cache{nullptr};
};

/**
 * @brief Runs every enabled rule over the snapshot and returns the report. Works out what the snapshot leaves to the
 * run first: the references between assets, each profile's content, the facts of the compiled files and the code
 * analysis. Issues with the same id merge, profiles joined. Any thread; touches no editor state.
 */
auto run_audit(input& in, const config& settings, const run_options& options) -> report;

} // namespace unravel::audit
