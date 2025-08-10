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
    rttr::registration::class_<editor_settings::external_tools_settings>("external_tools_settings")(
        rttr::metadata("pretty_name", "External Tools"))
        .constructor<>()()
        .property("vscode_executable", &editor_settings::external_tools_settings::vscode_executable)(
            rttr::metadata("pretty_name", "Visual Studio Code"),
            rttr::metadata("type", "file"),
            rttr::metadata("tooltip", "Full path to executable."));
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
    rttr::registration::class_<editor_settings::debugger_settings>("debugger_settings")(
        rttr::metadata("pretty_name", "Standalone"))
        .constructor<>()()
        .property("ip", &editor_settings::debugger_settings::ip)(
            rttr::metadata("pretty_name", "Ip Address"),
            rttr::metadata("tooltip", "Ip address to await connections. Default(127.0.0.1)"))
        .property("port", &editor_settings::debugger_settings::port)(
            rttr::metadata("pretty_name", "Port"),
            rttr::metadata("tooltip", "Port to await connections. Default (55555)"))
        .property("loglevel",
                  &editor_settings::debugger_settings::loglevel)(rttr::metadata("pretty_name", "Log Level"));
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

REFLECT(editor_settings)
{
    rttr::registration::class_<editor_settings>("settings")(rttr::metadata("pretty_name", "Settings"))
        .constructor<>()()
        .property("debugger", &editor_settings::debugger)(rttr::metadata("pretty_name", "Debugger"),
                                                          rttr::metadata("tooltip", "Missing..."))
        .property("external_tools", &editor_settings::external_tools)(rttr::metadata("pretty_name", "External Tools"),
                                                                      rttr::metadata("tooltip", "Missing..."));
}

SAVE(editor_settings)
{
    try_save(ar, ser20::make_nvp("debugger", obj.debugger));
    try_save(ar, ser20::make_nvp("external_tools", obj.external_tools));
    try_save(ar, ser20::make_nvp("projects", obj.projects));
}
SAVE_INSTANTIATE(editor_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(editor_settings, ser20::oarchive_binary_t);

LOAD(editor_settings)
{
    try_load(ar, ser20::make_nvp("debugger", obj.debugger));
    try_load(ar, ser20::make_nvp("external_tools", obj.external_tools));
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
        catch(const ser20::Exception& e)
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
        catch(const ser20::Exception& e)
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
        catch(const ser20::Exception& e)
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
        catch(const ser20::Exception& e)
        {
            APPLOG_WARNING("Failed to load config file {}", absolute_path);
        }
    }

    return false;
}
} // namespace unravel
