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
    rttr::registration::class_<model>("model")
        .property("materials", &model::get_materials, &model::set_materials)(
            rttr::metadata("pretty_name", "Materials"),
            rttr::metadata("tooltip", "Materials for this model."))
        .property("material_instances", &model::get_material_instances, &model::set_material_instances)(
            rttr::metadata("pretty_name", "Material Instances"),
            rttr::metadata("tooltip", "Material instances for this model."))
        .property("lods", &model::get_lods, &model::set_lods)(rttr::metadata("pretty_name", "LOD"),
                                                              rttr::metadata("tooltip", "Levels of Detail."))
        .property("lod_limits", &model::get_lod_limits, &model::set_lod_limits)(
            rttr::metadata("pretty_name", "LOD Ranges"),
            rttr::metadata("tooltip", "LOD ranges in % of screen."),
            rttr::metadata("format", "%.2f%%"),
            rttr::metadata("min", 0),
            rttr::metadata("max", 100));

    // Register model with entt
    entt::meta_factory<model>{}
        .type("model"_hs)
        .data<&model::set_materials, &model::get_materials>("materials"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Materials"},
            entt::attribute{"tooltip", "Materials for this model."},
        })
        .data<&model::set_material_instances, &model::get_material_instances>("material_instances"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Material Instances"},
            entt::attribute{"tooltip", "Material instances for this model."},
        })
        .data<&model::set_lods, &model::get_lods>("lods"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "LOD"},
            entt::attribute{"tooltip", "Levels of Detail."},
        })
        .data<&model::set_lod_limits, &model::get_lod_limits>("lod_limits"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "LOD Ranges"},
            entt::attribute{"tooltip", "LOD ranges in % of screen."},
            entt::attribute{"format", "%.2f%%"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 100},
        });
}

SAVE(model)
{
    try_save(ar, ser20::make_nvp("lods", obj.mesh_lods_));
    try_save(ar, ser20::make_nvp("materials", obj.materials_));
    try_save(ar, ser20::make_nvp("material_instances", obj.material_instances_));
    try_save(ar, ser20::make_nvp("lod_limits", obj.lod_limits_));
}
SAVE_INSTANTIATE(model, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(model, ser20::oarchive_binary_t);

LOAD(model)
{
    try_load(ar, ser20::make_nvp("lods", obj.mesh_lods_));
    try_load(ar, ser20::make_nvp("materials", obj.materials_));
    try_load(ar, ser20::make_nvp("material_instances", obj.material_instances_));
    try_load(ar, ser20::make_nvp("lod_limits", obj.lod_limits_));
}
LOAD_INSTANTIATE(model, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(model, ser20::iarchive_binary_t);
} // namespace unravel
