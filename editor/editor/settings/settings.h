#pragma once

#include <base/basetypes.hpp>
#include <filesystem/filesystem.h>
#include <string>

namespace unravel
{

struct editor_settings
{
    struct debugger_settings
    {
        std::string ip = "127.0.0.1";
        uint32_t port = 55555;
        uint32_t loglevel = 0;
    } debugger;

    struct external_tools_settings
    {
        fs::path vscode_executable;
    } external_tools;

    struct scripting_settings
    {
        /// Always enabled. Shown in the UI as a readonly checkbox.
        bool reload_app_domain = true;
        /// When true, also unload/load the engine domain on play-mode changes
        /// and script recompile (alongside the app domain).
        bool reload_engine_domain = false;
    } scripting;

    struct projects_settings
    {
        std::vector<fs::path> recent_projects;
    } projects;
};
} // namespace unravel
