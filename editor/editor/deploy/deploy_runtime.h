#pragma once

#include <context/context.hpp>
#include <deploy/assembly_references.h>
#include <deploy/deploy_settings.h>
#include <filesystem/filesystem.h>

#include <string>
#include <vector>

/// The managed runtime a deployed game carries beside it.
namespace unravel::deployment
{

/// What the runtime step copies, every path resolved on the main thread.
struct runtime_plan
{
    /// The step's name in the deploy's list.
    std::string step_name;
    /// The managed bridge payload (CoreCLR) or the runtime's assemblies (Mono), and where they go.
    fs::path managed_source;
    fs::path managed_destination;
    /// CoreCLR: the hostfxr and shared framework version folders to bundle. The framework is empty when none
    /// was found (the game then needs an installed runtime); the host alone is empty when no hostfxr was
    /// found (the game then starts the bundled framework through the coreclr host). Mono: the folder holding
    /// the native libraries.
    fs::path host_source;
    fs::path host_destination;
    fs::path framework_source;
    fs::path framework_destination;
    /// Mono only: native libraries copied by name from host_source into host_destination.
    std::vector<std::string> library_names;
    /// Mono only: the runtime's configuration.
    fs::path config_source;
    fs::path config_destination;
    bool with_debug_symbols{};
};

/// The runtime step of a deploy with these settings; main thread.
auto make_runtime_plan(rtti::context& ctx, const deploy_settings& params) -> runtime_plan;

/// The libraries of the plan's managed payload that ship: what the game loads from it, which a framework trim
/// starts from. None on Mono, whose class libraries ship whole.
auto list_runtime_libraries(const runtime_plan& plan) -> std::vector<fs::path>;

/**
 * @brief Brings the deployed runtime up to date: copies what changed and removes an older version beside the
 * current one. Throws on failure.
 * @param trim The framework assemblies the build leaves out (CoreCLR's shared framework); what an earlier build
 * shipped of them is removed. It reads the build's script libraries, so they are built by then.
 */
void deploy_runtime(const runtime_plan& plan, const framework_trim& trim);

/// Why the editor's runtime cannot ship, if it cannot: a deployed game carries the runtime the editor runs
/// on, which must be of the preferred .NET major. Empty when it can.
auto get_runtime_problem() -> std::string;

} // namespace unravel::deployment
