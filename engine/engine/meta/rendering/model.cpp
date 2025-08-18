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
        .data<&model::set_lod_limits, &model::get_lod_limits>("lod_limits"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_limits"},
            entt::attribute{"pretty_name", "LOD Ranges"},
            entt::attribute{"tooltip", "LOD ranges in % of screen."},
            entt::attribute{"format", "%.2f%%"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 100},
        });

    auto t = entt::resolve<model>();
    auto data = t.data("materials"_hs);
    auto type = data.type();
    auto is_sequence_container = type.is_sequence_container();
    auto is_associative_container = type.is_associative_container();

    std::cout << "is_sequence_container: " << is_sequence_container << std::endl;
    std::cout << "is_associative_container: " << is_associative_container << std::endl;


    model m;
    entt::meta_any obj = m;
    auto md = entt::resolve<model>().data("materials"_hs);

    // This gives you a meta_any referencing the vector
    entt::meta_any vec = md.get(obj);

    if(auto seq = vec.as_sequence_container()) {
        std::cout << "size = " << seq.size() << "\n";
        for(auto elem : seq) {
            std::cout << elem.cast<asset_handle<material>>() << " ";  // prints 1 2 3
        }
        std::cout << "\n";
    }
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
