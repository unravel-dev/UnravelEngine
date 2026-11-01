#pragma once

#include <editor/editing/gizmo_settings.h>
#include <engine/assets/asset_handle.h>
#include <engine/ecs/prefab.h>

#include <string>

namespace unravel
{

/// Per-project editor state persisted in app:/editor/.
/// Unlike deploy_settings (deployment config) or settings (runtime config),
/// this captures editor-only working state such as the last opened scene.
struct project_editor_settings
{
    struct scene_setup
    {
        asset_handle<scene_prefab> opened_scene;
    } scene;

    /// The settings profiles this user works with: the editor's choices, not the project's.
    struct profile_setup
    {
        /// The profile the Deploy window builds.
        std::string deploy_profile;
        /// The profile whose settings the editor runs with; empty: the project settings.
        std::string preview_profile;
    } profiles;

    /// The icons and gizmos of the Scene view as the user last set them on its toolbar, given to the editing manager
    /// when the project opens. What a tool changes for a capture never gets here.
    gizmo_setup gizmos;
};

} // namespace unravel
