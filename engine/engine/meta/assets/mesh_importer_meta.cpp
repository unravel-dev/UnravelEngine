#include "asset_importer_meta.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>

namespace unravel
{

REFLECT(mesh_importer_meta)
{
    rttr::registration::class_<mesh_importer_meta::model_meta>("model_meta")
        .property("import_meshes",
                  &mesh_importer_meta::model_meta::import_meshes)(rttr::metadata("pretty_name", "Import Meshes"))
        .property("weld_vertices", &mesh_importer_meta::model_meta::weld_vertices)(
            rttr::metadata("pretty_name", "Weld Vertices"),
            rttr::metadata("tooltip",
                           "Identifies and joins identical vertex data sets within all imported meshes.\n"
                           "After this step is run, each mesh contains unique vertices,\n"
                           "so a vertex may be used by multiple faces. You usually want\n"
                           "to use this post processing step. If your application deals with\n"
                           "indexed geometry, this step is compulsory or you'll just waste rendering\n"
                           "time."))
        .property("optimize_meshes", &mesh_importer_meta::model_meta::optimize_meshes)(
            rttr::metadata("pretty_name", "Optimize Meshes"),
            rttr::metadata("tooltip",
                           "A post-processing step to reduce the number of meshes.\n"
                           "This will, in fact, reduce the number of draw calls."))
        .property("split_large_meshes", &mesh_importer_meta::model_meta::split_large_meshes)(
            rttr::metadata("pretty_name", "Split Large Meshes"))
        .property("find_degenerates",
                  &mesh_importer_meta::model_meta::find_degenerates)(rttr::metadata("pretty_name", "Find Degenerates"))
        .property("find_invalid_data", &mesh_importer_meta::model_meta::find_invalid_data)(
            rttr::metadata("pretty_name", "Find Invalid Data"),
            rttr::metadata("tooltip",
                           "This step searches all meshes for invalid data, such as zeroed\n"
                           "normal vectors or invalid UV coords and removes/fixes them. This is\n"
                           "intended to get rid of some common exporter errors."));

    rttr::registration::class_<mesh_importer_meta::rig_meta>("rig_meta");

    rttr::registration::class_<mesh_importer_meta::animations_meta>("animations_meta")
        .property("import_animations", &mesh_importer_meta::animations_meta::import_animations)(
            rttr::metadata("pretty_name", "Import Animations"));

    rttr::registration::class_<mesh_importer_meta::materials_meta>("materials_meta")
        .property("import_materials", &mesh_importer_meta::materials_meta::import_materials)(
            rttr::metadata("pretty_name", "Import Materials"))
        .property("remove_redundant_materials", &mesh_importer_meta::materials_meta::remove_redundant_materials)(
            rttr::metadata("pretty_name", "Remove Redundant Materials"));

    rttr::registration::class_<mesh_importer_meta>("mesh_importer_meta")
        .property("model", &mesh_importer_meta::model)(rttr::metadata("pretty_name", "Model"))
        .property("rig", &mesh_importer_meta::rig)(rttr::metadata("pretty_name", "Rig"))
        .property("animations", &mesh_importer_meta::animations)(rttr::metadata("pretty_name", "Animations"))
        .property("materials", &mesh_importer_meta::materials)(rttr::metadata("pretty_name", "Materials"));
}

SAVE(mesh_importer_meta::model_meta)
{
    try_save(ar, ser20::make_nvp("weld_vertices", obj.weld_vertices));
    try_save(ar, ser20::make_nvp("optimize_meshes", obj.optimize_meshes));
    try_save(ar, ser20::make_nvp("split_large_meshes", obj.split_large_meshes));
    try_save(ar, ser20::make_nvp("find_degenerates", obj.find_degenerates));
    try_save(ar, ser20::make_nvp("find_invalid_data", obj.find_invalid_data));
}
SAVE_INSTANTIATE(mesh_importer_meta::model_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(mesh_importer_meta::model_meta, ser20::oarchive_binary_t);

LOAD(mesh_importer_meta::model_meta)
{
    try_load(ar, ser20::make_nvp("weld_vertices", obj.weld_vertices));
    try_load(ar, ser20::make_nvp("optimize_meshes", obj.optimize_meshes));
    try_load(ar, ser20::make_nvp("split_large_meshes", obj.split_large_meshes));
    try_load(ar, ser20::make_nvp("find_degenerates", obj.find_degenerates));
    try_load(ar, ser20::make_nvp("find_invalid_data", obj.find_invalid_data));
}
LOAD_INSTANTIATE(mesh_importer_meta::model_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_importer_meta::model_meta, ser20::iarchive_binary_t);

SAVE(mesh_importer_meta::rig_meta)
{
}
SAVE_INSTANTIATE(mesh_importer_meta::rig_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(mesh_importer_meta::rig_meta, ser20::oarchive_binary_t);

LOAD(mesh_importer_meta::rig_meta)
{
}
LOAD_INSTANTIATE(mesh_importer_meta::rig_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_importer_meta::rig_meta, ser20::iarchive_binary_t);

SAVE(mesh_importer_meta::animations_meta)
{
    try_save(ar, ser20::make_nvp("import_animations", obj.import_animations));
}
SAVE_INSTANTIATE(mesh_importer_meta::animations_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(mesh_importer_meta::animations_meta, ser20::oarchive_binary_t);

LOAD(mesh_importer_meta::animations_meta)
{
    try_load(ar, ser20::make_nvp("import_animations", obj.import_animations));
}
LOAD_INSTANTIATE(mesh_importer_meta::animations_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_importer_meta::animations_meta, ser20::iarchive_binary_t);

SAVE(mesh_importer_meta::materials_meta)
{
    try_save(ar, ser20::make_nvp("import_materials", obj.import_materials));
    try_save(ar, ser20::make_nvp("remove_redundant_materials", obj.remove_redundant_materials));
}
SAVE_INSTANTIATE(mesh_importer_meta::materials_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(mesh_importer_meta::materials_meta, ser20::oarchive_binary_t);

LOAD(mesh_importer_meta::materials_meta)
{
    try_load(ar, ser20::make_nvp("import_materials", obj.import_materials));
    try_load(ar, ser20::make_nvp("remove_redundant_materials", obj.remove_redundant_materials));
}
LOAD_INSTANTIATE(mesh_importer_meta::materials_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_importer_meta::materials_meta, ser20::iarchive_binary_t);

SAVE(mesh_importer_meta)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_save(ar, ser20::make_nvp("model", obj.model));
    try_save(ar, ser20::make_nvp("rig", obj.rig));
    try_save(ar, ser20::make_nvp("animations", obj.animations));
    try_save(ar, ser20::make_nvp("materials", obj.materials));
}
SAVE_INSTANTIATE(mesh_importer_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(mesh_importer_meta, ser20::oarchive_binary_t);

LOAD(mesh_importer_meta)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_load(ar, ser20::make_nvp("model", obj.model));
    try_load(ar, ser20::make_nvp("rig", obj.rig));
    try_load(ar, ser20::make_nvp("animations", obj.animations));
    try_load(ar, ser20::make_nvp("materials", obj.materials));
}
LOAD_INSTANTIATE(mesh_importer_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_importer_meta, ser20::iarchive_binary_t);




} // namespace unravel
