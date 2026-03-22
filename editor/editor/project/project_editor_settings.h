#pragma once

#include <engine/assets/asset_handle.h>
#include <engine/ecs/prefab.h>

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
};

} // namespace unravel
