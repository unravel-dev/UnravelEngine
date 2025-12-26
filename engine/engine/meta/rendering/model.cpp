#include "model.hpp"
#include "material.hpp"
#include "mesh.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

namespace unravel
{
REFLECT(model)
{
    auto is_lod_override_enabled_predicate = entt::property_predicate<bool>(
        [](const entt::meta_any& i)    
        {
            return i.try_cast<model>()->get_lod_override_enabled();
        });


    entt::meta_factory<model>{}
        .type("model"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model"},
            entt::attribute{"pretty_name", "Model"},
        })
        .data<&model::set_materials, &model::get_materials>("materials"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "materials"},
            entt::attribute{"pretty_name", "Materials"},
            entt::attribute{"tooltip", "Materials for this model."},
        })
        .data<&model::set_material_instances, &model::get_material_instances>("material_instances"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material_instances"},
            entt::attribute{"pretty_name", "Material Instances"},
            entt::attribute{"tooltip", "Material instances for this model."},
        })
        .data<&model::set_lods, &model::get_lods>("lods"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lods"},
            entt::attribute{"pretty_name", "LOD"},
            entt::attribute{"tooltip", "Levels of Detail."},
        })
        .data<&model::set_lod_override_enabled, &model::get_lod_override_enabled>("lod_override_enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_override_enabled"},
            entt::attribute{"pretty_name", "LOD Override"},
            entt::attribute{"tooltip", "Enable to force a specific LOD level regardless of distance."},
        })
        .data<&model::set_lod_override_level, &model::get_lod_override_level>("lod_override_level"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_override_level"},
            entt::attribute{"pretty_name", "Override Level"},
            entt::attribute{"tooltip", "The LOD level to use when override is enabled."},
            entt::attribute{"predicate", is_lod_override_enabled_predicate},
            entt::attribute{"min", 0},
            entt::attribute{"max", mesh::get_max_generated_lod_count()},
        })
        .data<&model::set_lod_selection_bias, &model::get_lod_selection_bias>("lod_selection_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_selection_bias"},
            entt::attribute{"pretty_name", "LOD Selection Bias"},
            entt::attribute{"tooltip", "The value added to the calculated LOD index. Increasing this value results in selecting less detailed LODs, reducing the value - in more detailed LODs."},
            entt::attribute{"readonly_predicate", is_lod_override_enabled_predicate},
            entt::attribute{"min", -float(mesh::get_max_generated_lod_count())},
            entt::attribute{"max", float(mesh::get_max_generated_lod_count())},
        });
}

SAVE(model)
{
    try_save(ar, ser20::make_nvp("lods", obj.mesh_lods_));
    try_save(ar, ser20::make_nvp("materials", obj.materials_));
    try_save(ar, ser20::make_nvp("material_instances", obj.material_instances_));
    try_save(ar, ser20::make_nvp("lod_override_enabled", obj.lod_override_enabled_));
    try_save(ar, ser20::make_nvp("lod_override_level", obj.lod_override_level_));
    try_save(ar, ser20::make_nvp("lod_selection_bias", obj.lod_selection_bias_));
}
SAVE_INSTANTIATE(model, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(model, ser20::oarchive_binary_t);

LOAD(model)
{
    std::vector<asset_handle<mesh>> lods;
    try_load(ar, ser20::make_nvp("lods", lods));
    obj.set_lods(lods);
    try_load(ar, ser20::make_nvp("materials", obj.materials_));
    try_load(ar, ser20::make_nvp("material_instances", obj.material_instances_));
    try_load(ar, ser20::make_nvp("lod_override_enabled", obj.lod_override_enabled_));
    try_load(ar, ser20::make_nvp("lod_override_level", obj.lod_override_level_));
    try_load(ar, ser20::make_nvp("lod_selection_bias", obj.lod_selection_bias_));
}
LOAD_INSTANTIATE(model, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(model, ser20::iarchive_binary_t);
} // namespace unravel
