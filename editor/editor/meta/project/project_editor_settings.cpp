#include "project_editor_settings.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>

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

SAVE_INLINE(project_editor_settings::profile_setup)
{
    try_save(ar, ser20::make_nvp("deploy_profile", obj.deploy_profile));
    try_save(ar, ser20::make_nvp("preview_profile", obj.preview_profile));
}

LOAD_INLINE(project_editor_settings::profile_setup)
{
    try_load(ar, ser20::make_nvp("deploy_profile", obj.deploy_profile));
    try_load(ar, ser20::make_nvp("preview_profile", obj.preview_profile));
}

SERIALIZE_INLINE(billboard_gizmos)
{
    try_serialize(ar, ser20::make_nvp("opacity", obj.opacity));
    try_serialize(ar, ser20::make_nvp("size", obj.size));
    try_serialize(ar, ser20::make_nvp("depth_aware", obj.depth_aware));
    try_serialize(ar, ser20::make_nvp("show_camera", obj.show_camera));
    try_serialize(ar, ser20::make_nvp("show_light", obj.show_light));
    try_serialize(ar, ser20::make_nvp("show_reflection_probe", obj.show_reflection_probe));
    try_serialize(ar, ser20::make_nvp("show_audio_source", obj.show_audio_source));
    try_serialize(ar, ser20::make_nvp("show_particle_emitter", obj.show_particle_emitter));
}

SERIALIZE_INLINE(gizmos_data)
{
    try_serialize(ar, ser20::make_nvp("show_selection_outline", obj.show_selection_outline));
    try_serialize(ar, ser20::make_nvp("show_selection_wireframe", obj.show_selection_wireframe));
    try_serialize(ar, ser20::make_nvp("selection_wireframe_color", obj.selection_wireframe_color));
    try_serialize(ar, ser20::make_nvp("selection_wireframe_thickness", obj.selection_wireframe_thickness));
    try_serialize(ar, ser20::make_nvp("show_camera", obj.show_camera));
    try_serialize(ar, ser20::make_nvp("show_light", obj.show_light));
    try_serialize(ar, ser20::make_nvp("show_reflection_probe", obj.show_reflection_probe));
    try_serialize(ar, ser20::make_nvp("show_volume", obj.show_volume));
    try_serialize(ar, ser20::make_nvp("show_model_bounds", obj.show_model_bounds));
    try_serialize(ar, ser20::make_nvp("show_model_submesh_bounds", obj.show_model_submesh_bounds));
    try_serialize(ar, ser20::make_nvp("show_text", obj.show_text));
    try_serialize(ar, ser20::make_nvp("show_particle_emitter", obj.show_particle_emitter));
    try_serialize(ar, ser20::make_nvp("show_component_gizmos", obj.show_component_gizmos));
    try_serialize(ar, ser20::make_nvp("show_particle_emitter_bounds", obj.show_particle_emitter_bounds));
    try_serialize(ar, ser20::make_nvp("show_particle_emitter_shape", obj.show_particle_emitter_shape));
    try_serialize(ar, ser20::make_nvp("show_particle_emitter_direction", obj.show_particle_emitter_direction));
}

SERIALIZE_INLINE(gizmo_setup)
{
    try_serialize(ar, ser20::make_nvp("show_gizmos", obj.show_gizmos));
    try_serialize(ar, ser20::make_nvp("billboards", obj.billboards));
    try_serialize(ar, ser20::make_nvp("selection", obj.selection));
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
    try_save(ar, ser20::make_nvp("profiles", obj.profiles));
    try_save(ar, ser20::make_nvp("gizmos", obj.gizmos));
}
SAVE_INSTANTIATE(project_editor_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(project_editor_settings, ser20::oarchive_binary_t);

LOAD(project_editor_settings)
{
    try_load(ar, ser20::make_nvp("scene", obj.scene));
    try_load(ar, ser20::make_nvp("profiles", obj.profiles));
    try_load(ar, ser20::make_nvp("gizmos", obj.gizmos));
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
