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

    /// Stable unique id for this project. Generated once on creation and kept
    /// forever - useful as a cache/bucket key for launchers, CI artifacts etc.
    std::string project_guid;
};

/// Returns true if `on_disk` is from an engine strictly older than `running`,
/// comparing all four components (major, minor, patch, commit_count).
///
/// This models the common upgrade flow: a project was authored against one
/// engine build and is later opened on a newer build, where on-disk formats
/// may have evolved in the intervening commits. The inverse direction
/// (running an older engine against a project saved by a newer one) is rare
/// enough that we don't warn about it here.
inline auto is_from_older_engine(const version::engine_version& on_disk,
                                 const version::engine_version& running) -> bool
{
    return on_disk < running;
}

} // namespace unravel
