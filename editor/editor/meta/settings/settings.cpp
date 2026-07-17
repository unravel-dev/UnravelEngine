#include "settings.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include "logging/logging.h"
#include <serialization/types/array.hpp>
#include <serialization/types/vector.hpp>

namespace unravel
{

SAVE_INLINE(editor_settings::projects_settings)
{
    std::vector<std::string> recent_projects;
    recent_projects.reserve(obj.recent_projects.size());
    for(const auto& prj : obj.recent_projects)
    {
        recent_projects.emplace_back(prj.generic_string());
    }
    try_save(ar, ser20::make_nvp("recent_projects", recent_projects));
}

LOAD_INLINE(editor_settings::projects_settings)
{
    std::vector<std::string> recent_projects;

    try_load(ar, ser20::make_nvp("recent_projects", recent_projects));

    obj.recent_projects.reserve(recent_projects.size());
    for(const auto& prj : recent_projects)
    {
        obj.recent_projects.emplace_back(fs::path(prj));
    }
}

REFLECT_INLINE(editor_settings::external_tools_settings)
{
    entt::meta_factory<editor_settings::external_tools_settings>{}
        .type("external_tools_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "external_tools_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "External Tools"},
        })
        .data<&editor_settings::external_tools_settings::vscode_executable>("vscode_executable"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vscode_executable"},
            entt::attribute{"pretty_name", "Visual Studio Code"},
            entt::attribute{"tooltip", "Full path to executable."},
            entt::attribute{"type", "file"},
        });
}

SAVE_INLINE(editor_settings::external_tools_settings)
{
    try_save(ar, ser20::make_nvp("vscode_executable", obj.vscode_executable.generic_string()));
}

LOAD_INLINE(editor_settings::external_tools_settings)
{
    std::string vscode_executable;
    if(try_load(ar, ser20::make_nvp("vscode_executable", vscode_executable)))
    {
        obj.vscode_executable = vscode_executable;
    }
}

REFLECT_INLINE(editor_settings::debugger_settings)
{
    entt::meta_factory<editor_settings::debugger_settings>{}
        .type("debugger_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "debugger_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Debugger"},
        })
        .data<&editor_settings::debugger_settings::ip>("ip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ip"},
            entt::attribute{"pretty_name", "Ip Address"},
            entt::attribute{"tooltip", "Ip address to await connections. Default(127.0.0.1)"},
        })
        .data<&editor_settings::debugger_settings::port>("port"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "port"},
            entt::attribute{"pretty_name", "Port"},
            entt::attribute{"tooltip", "Port to await connections. Default (55555)"},
        })
        .data<&editor_settings::debugger_settings::loglevel>("loglevel"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "loglevel"},
            entt::attribute{"pretty_name", "Log Level"},
        });
}

SAVE_INLINE(editor_settings::debugger_settings)
{
    try_save(ar, ser20::make_nvp("ip", obj.ip));
    try_save(ar, ser20::make_nvp("port", obj.port));
    try_save(ar, ser20::make_nvp("loglevel", obj.loglevel));
}

LOAD_INLINE(editor_settings::debugger_settings)
{
    try_load(ar, ser20::make_nvp("ip", obj.ip));
    try_load(ar, ser20::make_nvp("port", obj.port));
    try_load(ar, ser20::make_nvp("loglevel", obj.loglevel));
}

REFLECT_INLINE(editor_settings::scripting_settings)
{
    auto always_readonly = entt::property_predicate<bool>(
        [](const entt::meta_any&)
        {
            return true;
        });

    entt::meta_factory<editor_settings::scripting_settings>{}
        .type("scripting_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scripting_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Scripting"},
        })
        .data<&editor_settings::scripting_settings::reload_app_domain>("reload_app_domain"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reload_app_domain"},
            entt::attribute{"pretty_name", "Reload App Domain"},
            entt::attribute{"tooltip", "Always reloads the app domain on play-mode changes and script recompile."},
            entt::attribute{"readonly_predicate", always_readonly},
        })
        .data<&editor_settings::scripting_settings::reload_engine_domain>("reload_engine_domain"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reload_engine_domain"},
            entt::attribute{"pretty_name", "Reload Engine Domain"},
            entt::attribute{"tooltip",
                            "Also reload the engine domain on play-mode changes and script recompile. "
                            "Useful when iterating on engine C# scripts."},
        });
}

SAVE_INLINE(editor_settings::scripting_settings)
{
    try_save(ar, ser20::make_nvp("reload_app_domain", obj.reload_app_domain));
    try_save(ar, ser20::make_nvp("reload_engine_domain", obj.reload_engine_domain));
}

LOAD_INLINE(editor_settings::scripting_settings)
{
    try_load(ar, ser20::make_nvp("reload_app_domain", obj.reload_app_domain));
    try_load(ar, ser20::make_nvp("reload_engine_domain", obj.reload_engine_domain));
    obj.reload_app_domain = true;
}

REFLECT(editor_settings)
{
    entt::meta_factory<editor_settings>{}
        .type("editor_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "editor_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Editor Settings"},
        })
        .data<&editor_settings::debugger>("debugger"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "debugger"},
            entt::attribute{"pretty_name", "Debugger"},
            entt::attribute{"tooltip", "Missing..."},
        })
        .data<&editor_settings::external_tools>("external_tools"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "external_tools"},
            entt::attribute{"pretty_name", "External Tools"},
            entt::attribute{"tooltip", "Missing..."},
        })
        .data<&editor_settings::scripting>("scripting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scripting"},
            entt::attribute{"pretty_name", "Scripting"},
            entt::attribute{"tooltip", "Script domain reload behaviour."},
        });
}

SAVE(editor_settings)
{
    try_save(ar, ser20::make_nvp("debugger", obj.debugger));
    try_save(ar, ser20::make_nvp("external_tools", obj.external_tools));
    try_save(ar, ser20::make_nvp("scripting", obj.scripting));
    try_save(ar, ser20::make_nvp("projects", obj.projects));
}
SAVE_INSTANTIATE(editor_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(editor_settings, ser20::oarchive_binary_t);

LOAD(editor_settings)
{
    try_load(ar, ser20::make_nvp("debugger", obj.debugger));
    try_load(ar, ser20::make_nvp("external_tools", obj.external_tools));
    try_load(ar, ser20::make_nvp("scripting", obj.scripting));
    try_load(ar, ser20::make_nvp("projects", obj.projects));
}
LOAD_INSTANTIATE(editor_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(editor_settings, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const editor_settings& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        try
        {
            auto ar = ser20::create_oarchive_associative(stream);
            try_save(ar, ser20::make_nvp("settings", obj));
        }
        catch(const std::exception& e)
        {
            APPLOG_WARNING("Failed to load config file {}", absolute_path);
        }
    }
}

void save_to_file_bin(const std::string& absolute_path, const editor_settings& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        try
        {
            ser20::oarchive_binary_t ar(stream);
            try_save(ar, ser20::make_nvp("settings", obj));
        }
        catch(const std::exception& e)
        {
            APPLOG_WARNING("Failed to load config file {}", absolute_path);
        }
    }
}

auto load_from_file(const std::string& absolute_path, editor_settings& obj) -> bool
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        try
        {
            auto ar = ser20::create_iarchive_associative(stream);
            return try_load(ar, ser20::make_nvp("settings", obj));
        }
        catch(const std::exception& e)
        {
            APPLOG_WARNING("Failed to load config file {}", absolute_path);
        }
    }

    return false;
}

auto load_from_file_bin(const std::string& absolute_path, editor_settings& obj) -> bool
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        try
        {
            ser20::iarchive_binary_t ar(stream);
            return try_load(ar, ser20::make_nvp("settings", obj));
        }
        catch(const std::exception& e)
        {
            APPLOG_WARNING("Failed to load config file {}", absolute_path);
        }
    }

    return false;
}
} // namespace unravel
