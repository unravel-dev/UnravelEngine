#include "project_info.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>

#include <logging/logging.h>

namespace unravel
{

namespace
{

// `version::engine_version` is intentionally serialized as its canonical
// "M.m.p[-commit[-sha]]" string. This keeps the on-disk format human-readable,
// stable across engine refactors of the struct, and hand-editable by users.
auto to_nvp_string(const version::engine_version& v) -> std::string
{
    return v.to_string();
}

void from_nvp_string(const std::string& s, version::engine_version& out)
{
    if(auto parsed = version::parse(s))
    {
        out = *parsed;
    }
    else
    {
        APPLOG_WARNING("Could not parse engine version '{}' from project file. "
                       "Using zero-initialized version.",
                       s);
        out = {};
    }
}

} // namespace

REFLECT(project_info)
{
    entt::meta_factory<project_info>{}
        .type("project_info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "project_info"},
            entt::attribute{"category", "PROJECT"},
            entt::attribute{"pretty_name", "Project Info"},
        })
        .data<&project_info::project_guid>("project_guid"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "project_guid"},
            entt::attribute{"pretty_name", "Project GUID"},
            entt::attribute{"tooltip", "Stable unique id generated when this project was created."},
            entt::attribute{"readonly", true},
        });
}

SAVE(project_info)
{
    try_save(ar, ser20::make_nvp("project_guid", obj.project_guid));

    // Serialized as strings: see rationale at the top of this file.
    const std::string created = to_nvp_string(obj.engine_version_created);
    const std::string opened = to_nvp_string(obj.engine_version_opened);
    try_save(ar, ser20::make_nvp("engine_version_created", created));
    try_save(ar, ser20::make_nvp("engine_version_opened", opened));
}
SAVE_INSTANTIATE(project_info, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(project_info, ser20::oarchive_binary_t);

LOAD(project_info)
{
    try_load(ar, ser20::make_nvp("project_guid", obj.project_guid));

    std::string created;
    if(try_load(ar, ser20::make_nvp("engine_version_created", created)))
    {
        from_nvp_string(created, obj.engine_version_created);
    }
    std::string opened;
    if(try_load(ar, ser20::make_nvp("engine_version_opened", opened)))
    {
        from_nvp_string(opened, obj.engine_version_opened);
    }
}
LOAD_INSTANTIATE(project_info, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(project_info, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const project_info& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        try
        {
            auto ar = ser20::create_oarchive_associative(stream);
            try_save(ar, ser20::make_nvp("project", obj));
        }
        catch(const std::exception& e)
        {
            APPLOG_WARNING("Failed to save project info {}: {}", absolute_path, e.what());
        }
    }
}

auto load_from_file(const std::string& absolute_path, project_info& obj) -> bool
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        try
        {
            auto ar = ser20::create_iarchive_associative(stream);
            return try_load(ar, ser20::make_nvp("project", obj));
        }
        catch(const std::exception& e)
        {
            APPLOG_WARNING("Failed to load project info {}: {}", absolute_path, e.what());
        }
    }

    return false;
}

} // namespace unravel
