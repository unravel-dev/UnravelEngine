#include "project_editor_settings.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <engine/meta/assets/asset_handle.hpp>

#include <logging/logging.h>

namespace unravel
{

REFLECT_INLINE(project_editor_settings::scene_setup)
{
    entt::meta_factory<project_editor_settings::scene_setup>{}
        .type("scene_setup"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scene_setup"},
            entt::attribute{"pretty_name", "Scene Setup"},
        })
        .data<&project_editor_settings::scene_setup::opened_scene>("opened_scene"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "opened_scene"},
            entt::attribute{"pretty_name", "Opened Scene"},
            entt::attribute{"tooltip", "The scene that was last opened in the editor."},
        });
}

SAVE_INLINE(project_editor_settings::scene_setup)
{
    try_save(ar, ser20::make_nvp("opened_scene", obj.opened_scene));
}

LOAD_INLINE(project_editor_settings::scene_setup)
{
    try_load(ar, ser20::make_nvp("opened_scene", obj.opened_scene));
}

REFLECT(project_editor_settings)
{
    entt::meta_factory<project_editor_settings>{}
        .type("project_editor_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "project_editor_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Project Editor Settings"},
        })
        .data<&project_editor_settings::scene>("scene"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scene"},
            entt::attribute{"pretty_name", "Scene"},
        });
}

SAVE(project_editor_settings)
{
    try_save(ar, ser20::make_nvp("scene", obj.scene));
}
SAVE_INSTANTIATE(project_editor_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(project_editor_settings, ser20::oarchive_binary_t);

LOAD(project_editor_settings)
{
    try_load(ar, ser20::make_nvp("scene", obj.scene));
}
LOAD_INSTANTIATE(project_editor_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(project_editor_settings, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const project_editor_settings& obj)
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
            APPLOG_WARNING("Failed to save project editor settings {}", absolute_path);
        }
    }
}

auto load_from_file(const std::string& absolute_path, project_editor_settings& obj) -> bool
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
            APPLOG_WARNING("Failed to load project editor settings {}", absolute_path);
        }
    }

    return false;
}

} // namespace unravel
