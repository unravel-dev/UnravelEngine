#pragma once

#include <version/version.h>

#include <string>

namespace unravel
{

/// Per-project metadata persisted in `app:/project.cfg`.
///
/// Unlike `settings` (runtime game config), `deploy_settings` (deployment config)
/// or `project_editor_settings` (editor working state), this is the *project
/// signature*: a minimal, version-gated record written once at create-time and
/// updated on every open.
///
/// Rationale for being a dedicated file instead of a field inside
/// `settings.cfg`:
///   * It must be readable *independently* of every other project file so the
///     engine can migrate / warn about format incompatibilities before
///     attempting full project deserialization (which itself may have changed
///     schema).
///   * Third-party tooling (launchers, CI, migration scripts, "open recent")
///     can cheaply fingerprint a project without loading the whole settings
///     pipeline.
struct project_info
{
    /// Engine version that first created this project. Immutable after the
    /// initial write; never overwritten on subsequent opens.
    version::engine_version engine_version_created;

    /// Engine version that most recently opened and saved this project.
    /// Updated on every successful open.
    version::engine_version engine_version_opened;

    /// The newest engine version that has opened this project. Raised on every open and never lowered, so an
    /// older engine that opens the project still knows that files in newer formats may be on disk. A project file
    /// without it reads engine_version_opened here.
    version::engine_version engine_version_newest;

    /// Stable unique id for this project. Generated once on creation and kept
    /// forever - useful as a cache/bucket key for launchers, CI artifacts etc.
    std::string project_guid;
};

/// Returns true if `on_disk` is from an engine strictly newer than `running`,
/// comparing all four components (major, minor, patch, commit_count): files
/// that engine saved may be in formats the running one cannot read.
inline auto is_from_newer_engine(const version::engine_version& on_disk,
                                 const version::engine_version& running) -> bool
{
    return running < on_disk;
}

} // namespace unravel
