#include "asset_importer_meta.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>

namespace unravel
{

REFLECT(mesh_importer_meta)
{
    entt::meta_factory<mesh_importer_meta>{}
        .type("mesh_importer_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh_importer_meta"},
            entt::attribute{"pretty_name", "Mesh Importer Meta"},
        })
        .func<&mesh_importer_meta::get_meta_type>("get_meta_type"_hs)
        .func<&mesh_importer_meta::get_static_meta_type>("get_static_meta_type"_hs)
        .func<&mesh_importer_meta::as_derived>("as_derived"_hs);

    // Register mesh_importer_meta::model_meta with entt
    entt::meta_factory<mesh_importer_meta::model_meta>{}
        .type("model_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model_meta"},
            entt::attribute{"pretty_name", "Model Meta"},
        })

        .data<&mesh_importer_meta::model_meta::import_meshes>("import_meshes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "import_meshes"},
            entt::attribute{"pretty_name", "Import Meshes"},
        })
        .data<&mesh_importer_meta::model_meta::weld_vertices>("weld_vertices"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weld_vertices"},
            entt::attribute{"pretty_name", "Weld Vertices"},
            entt::attribute{"tooltip",
                            "Identifies and joins identical vertex data sets within all imported meshes.\n"
                            "After this step is run, each mesh contains unique vertices,\n"
                            "so a vertex may be used by multiple faces. You usually want\n"
                            "to use this post processing step. If your application deals with\n"
                            "indexed geometry, this step is compulsory or you'll just waste rendering\n"
                            "time."},
        })
        .data<&mesh_importer_meta::model_meta::optimize_meshes>("optimize_meshes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "optimize_meshes"},
            entt::attribute{"pretty_name", "Optimize Meshes"},
            entt::attribute{"tooltip",
                            "A post-processing step to reduce the number of meshes.\n"
                            "This will, in fact, reduce the number of draw calls."},
        })
        .data<&mesh_importer_meta::model_meta::split_large_meshes>("split_large_meshes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "split_large_meshes"},
            entt::attribute{"pretty_name", "Split Large Meshes"},
        })
        .data<&mesh_importer_meta::model_meta::find_degenerates>("find_degenerates"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "find_degenerates"},
            entt::attribute{"pretty_name", "Find Degenerates"},
        })
        .data<&mesh_importer_meta::model_meta::find_invalid_data>("find_invalid_data"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "find_invalid_data"},
            entt::attribute{"pretty_name", "Find Invalid Data"},
            entt::attribute{
                "tooltip",
                "This step searches all meshes for invalid data, such as zeroed\n"
                "normal vectors or invalid UV coords and removes/fixes them. This is\n"
                "intended to get rid of some common exporter errors."},
        })
        .data<&mesh_importer_meta::model_meta::generate_lods>("generate_lods"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "generate_lods"},
            entt::attribute{"pretty_name", "Generate LODs"},
            entt::attribute{"tooltip",
                            "Enable automatic Level of Detail (LOD) generation during mesh compilation.\n"
                            "LODs are simplified versions of the mesh with fewer triangles for better\n"
                            "performance at distance. If disabled, only the base mesh will be available."},
        })
        .data<&mesh_importer_meta::model_meta::lod_target_error>("lod_target_error"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_target_error"},
            entt::attribute{"pretty_name", "LOD Target Error"},
            entt::attribute{"tooltip", "Target error for LOD generation (lower = higher quality, higher = more aggressive)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.001f},
        });

    // Register mesh_importer_meta::sdf_meta with entt
    entt::meta_factory<mesh_importer_meta::sdf_meta>{}
        .type("sdf_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sdf_meta"},
            entt::attribute{"pretty_name", "Distance Field"},
        })
        .data<&mesh_importer_meta::sdf_meta::generate_sdf>("generate_sdf"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "generate_sdf"},
            entt::attribute{"pretty_name", "Generate Distance Field"},
            entt::attribute{"tooltip",
                            "Bake a signed distance field for this mesh at compile time.\n"
                            "The surface cache GI tracer needs it to see this mesh; without one the\n"
                            "mesh neither occludes nor bounces indirect light."},
        })
        .data<&mesh_importer_meta::sdf_meta::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"tooltip",
                            "Target voxel count along the longest bounds axis. Higher resolves finer\n"
                            "detail and occludes thinner geometry, at a proportional memory cost."},
            entt::attribute{"min", 8},
            entt::attribute{"max", 256},
        })
        .data<&mesh_importer_meta::sdf_meta::min_voxel_size>("min_voxel_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_voxel_size"},
            entt::attribute{"pretty_name", "Min Voxel Size"},
            entt::attribute{"tooltip", "Lower clamp on the derived voxel size, in local units."},
            entt::attribute{"min", 0.001f},
            entt::attribute{"step", 0.001f},
        })
        .data<&mesh_importer_meta::sdf_meta::max_voxel_size>("max_voxel_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_voxel_size"},
            entt::attribute{"pretty_name", "Max Voxel Size"},
            entt::attribute{"tooltip", "Upper clamp on the derived voxel size, in local units."},
            entt::attribute{"min", 0.001f},
            entt::attribute{"step", 0.01f},
        })
        .data<&mesh_importer_meta::sdf_meta::max_total_voxels>("max_total_voxels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_total_voxels"},
            entt::attribute{"pretty_name", "Max Total Voxels"},
            entt::attribute{"tooltip",
                            "Ceiling on total grid voxels in ONE field.\n"
                            "The dominant control on both bake time and atlas footprint: each scales\n"
                            "with voxel count, and voxel count is cubic in resolution, so a per-axis\n"
                            "limit alone still permits millions of voxels in a single field.\n"
                            "Enforced by coarsening the voxel, so a field always covers its whole mesh.\n"
                            "Lower this on models split into very many submeshes."},
            entt::attribute{"min", 4096.0f},
            entt::attribute{"step", 4096.0f},
        })
        .data<&mesh_importer_meta::sdf_meta::lod_index>("lod_index"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_index"},
            entt::attribute{"pretty_name", "Bake From LOD"},
            entt::attribute{"tooltip",
                            "LOD the distance field is baked from. 0 is full detail.\n"
                            "A closest-point query costs roughly the SQUARE ROOT of the triangle\n"
                            "count, so simplified geometry is a real saving: 16x the triangles was\n"
                            "measured at 3.8x the bake time. The field is coarse enough that the\n"
                            "lost detail is usually below its own voxel size.\n"
                            "Raise this on dense models. Levels that were not generated fall back to 0.\n"
                            "Watch for a mesh becoming non-closed at higher levels: an open mesh bakes\n"
                            "as an unsigned shell, which loses interior solidity."},
            entt::attribute{"min", 0},
            entt::attribute{"max", 5},
        })
        .data<&mesh_importer_meta::sdf_meta::two_sided>("two_sided"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "two_sided"},
            entt::attribute{"pretty_name", "Two Sided"},
            entt::attribute{"tooltip",
                            "Bake an unsigned shell instead of a signed field.\n"
                            "Required for foliage cards and any other mesh that is not a closed\n"
                            "surface: inside/outside is undefined there, and a signed bake produces\n"
                            "randomly signed voxels that make the mesh flicker between solid and open."},
        })
        .data<&mesh_importer_meta::sdf_meta::two_sided_thickness>("two_sided_thickness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "two_sided_thickness"},
            entt::attribute{"pretty_name", "Two Sided Thickness"},
            entt::attribute{"tooltip", "Local-space half thickness given to the shell when Two Sided is set."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.005f},
        });

    // Register mesh_importer_meta::rig_meta with entt
    entt::meta_factory<mesh_importer_meta::rig_meta>{}
        .type("rig_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rig_meta"},
            entt::attribute{"pretty_name", "Rig Meta"},
        });

    // Register mesh_importer_meta::animations_meta with entt
    entt::meta_factory<mesh_importer_meta::animations_meta>{}
        .type("animations_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animations_meta"},
            entt::attribute{"pretty_name", "Animations Meta"},
        })
        .data<&mesh_importer_meta::animations_meta::import_animations>("import_animations"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "import_animations"},
            entt::attribute{"pretty_name", "Import Animations"},
        });

    // Register mesh_importer_meta::materials_meta with entt
    entt::meta_factory<mesh_importer_meta::materials_meta>{}
        .type("materials_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "materials_meta"},
            entt::attribute{"pretty_name", "Materials Meta"},
        })
        .data<&mesh_importer_meta::materials_meta::import_materials>("import_materials"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "import_materials"},
            entt::attribute{"pretty_name", "Import Materials"},
        })
        .data<&mesh_importer_meta::materials_meta::remove_redundant_materials>("remove_redundant_materials"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "remove_redundant_materials"},
            entt::attribute{"pretty_name", "Remove Redundant Materials"},
        });

    // Register mesh_importer_meta with entt
    entt::meta_factory<mesh_importer_meta>{}
        .type("mesh_importer_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh_importer_meta"},
            entt::attribute{"pretty_name", "Mesh Importer Meta"},
        })
        .data<&mesh_importer_meta::model>("model"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model"},
            entt::attribute{"pretty_name", "Model"},
        })
        .data<&mesh_importer_meta::rig>("rig"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rig"},
            entt::attribute{"pretty_name", "Rig"},
        })
        .data<&mesh_importer_meta::animations>("animations"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animations"},
            entt::attribute{"pretty_name", "Animations"},
        })
        .data<&mesh_importer_meta::materials>("materials"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "materials"},
            entt::attribute{"pretty_name", "Materials"},
        });
}

SAVE(mesh_importer_meta::model_meta)
{
    try_save(ar, ser20::make_nvp("weld_vertices", obj.weld_vertices));
    try_save(ar, ser20::make_nvp("optimize_meshes", obj.optimize_meshes));
    try_save(ar, ser20::make_nvp("split_large_meshes", obj.split_large_meshes));
    try_save(ar, ser20::make_nvp("find_degenerates", obj.find_degenerates));
    try_save(ar, ser20::make_nvp("find_invalid_data", obj.find_invalid_data));
    try_save(ar, ser20::make_nvp("generate_lods", obj.generate_lods));
    try_save(ar, ser20::make_nvp("lod_target_error", obj.lod_target_error));
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
    try_load(ar, ser20::make_nvp("generate_lods", obj.generate_lods));
    try_load(ar, ser20::make_nvp("lod_target_error", obj.lod_target_error));
}
LOAD_INSTANTIATE(mesh_importer_meta::model_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_importer_meta::model_meta, ser20::iarchive_binary_t);

SAVE(mesh_importer_meta::sdf_meta)
{
    try_save(ar, ser20::make_nvp("generate_sdf", obj.generate_sdf));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("min_voxel_size", obj.min_voxel_size));
    try_save(ar, ser20::make_nvp("max_voxel_size", obj.max_voxel_size));
    try_save(ar, ser20::make_nvp("max_total_voxels", obj.max_total_voxels));
    try_save(ar, ser20::make_nvp("lod_index", obj.lod_index));
    try_save(ar, ser20::make_nvp("two_sided", obj.two_sided));
    try_save(ar, ser20::make_nvp("two_sided_thickness", obj.two_sided_thickness));
}
SAVE_INSTANTIATE(mesh_importer_meta::sdf_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(mesh_importer_meta::sdf_meta, ser20::oarchive_binary_t);

LOAD(mesh_importer_meta::sdf_meta)
{
    try_load(ar, ser20::make_nvp("generate_sdf", obj.generate_sdf));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    try_load(ar, ser20::make_nvp("min_voxel_size", obj.min_voxel_size));
    try_load(ar, ser20::make_nvp("max_voxel_size", obj.max_voxel_size));
    try_load(ar, ser20::make_nvp("max_total_voxels", obj.max_total_voxels));
    try_load(ar, ser20::make_nvp("lod_index", obj.lod_index));
    try_load(ar, ser20::make_nvp("two_sided", obj.two_sided));
    try_load(ar, ser20::make_nvp("two_sided_thickness", obj.two_sided_thickness));
}
LOAD_INSTANTIATE(mesh_importer_meta::sdf_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_importer_meta::sdf_meta, ser20::iarchive_binary_t);

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
    try_save(ar, ser20::make_nvp("sdf", obj.sdf));
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
    try_load(ar, ser20::make_nvp("sdf", obj.sdf));
    try_load(ar, ser20::make_nvp("rig", obj.rig));
    try_load(ar, ser20::make_nvp("animations", obj.animations));
    try_load(ar, ser20::make_nvp("materials", obj.materials));
}
LOAD_INSTANTIATE(mesh_importer_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_importer_meta, ser20::iarchive_binary_t);

} // namespace unravel
