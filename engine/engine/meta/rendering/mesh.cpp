#include "mesh.hpp"

#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/core/math/quaternion.hpp>
#include <engine/meta/core/math/transform.hpp>
#include <engine/meta/core/math/bbox.hpp>
#include <engine/meta/rendering/gi/lumen_mesh_cards.hpp>
#include <engine/meta/rendering/gi/mesh_sdf.hpp>

#include <fstream>
#include <filesystem/file_istream.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <serialization/types/array.hpp>
#include <serialization/types/vector.hpp>

namespace bgfx
{
SAVE(VertexLayout)
{
    try_save(ar, ser20::make_nvp("hash", obj.m_hash));
    try_save(ar, ser20::make_nvp("stride", obj.m_stride));
    try_save(ar, ser20::make_nvp("offset", obj.m_offset));
    try_save(ar, ser20::make_nvp("attributes", obj.m_attributes));
}
SAVE_INSTANTIATE(VertexLayout, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(VertexLayout, ser20::oarchive_associative_t);

LOAD(VertexLayout)
{
    try_load(ar, ser20::make_nvp("hash", obj.m_hash));
    try_load(ar, ser20::make_nvp("stride", obj.m_stride));
    try_load(ar, ser20::make_nvp("offset", obj.m_offset));
    try_load(ar, ser20::make_nvp("attributes", obj.m_attributes));
}
LOAD_INSTANTIATE(VertexLayout, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(VertexLayout, ser20::oarchive_associative_t);

} // namespace bgfx

namespace unravel
{
REFLECT(mesh::info::lod_info)
{
    entt::meta_factory<mesh::info::lod_info>{}
        .type("lod_info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_info"},
        })
        .data<nullptr, &mesh::info::lod_info::triangles>("triangles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "triangles"},
            entt::attribute{"pretty_name", "Triangles"},
            entt::attribute{"tooltip", "Number of triangles in this LOD."},
        })
        .data<nullptr, &mesh::info::lod_info::percent>("percent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "percent"},
            entt::attribute{"pretty_name", "Percent"},
            entt::attribute{"tooltip", "Percentage of triangles in this LOD."},
        });
}

REFLECT(mesh::info)
{
    entt::meta_factory<mesh::info>{}
        .type("info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "info"},
        })
        .data<nullptr, &mesh::info::vertices>("vertices"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vertices"},
            entt::attribute{"pretty_name", "Vertices"},
            entt::attribute{"tooltip", "Vertices count."},
        })
        .data<nullptr, &mesh::info::triangles>("triangles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "triangles"},
            entt::attribute{"pretty_name", "Triangles"},
            entt::attribute{"tooltip", "Triangles count."},
        })
        .data<nullptr, &mesh::info::submeshes>("submeshes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submeshes"},
            entt::attribute{"pretty_name", "Submeshes"},
            entt::attribute{"tooltip", "submeshes count."},
        })
        .data<nullptr, &mesh::info::data_groups>("data_groups"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "data_groups"},
            entt::attribute{"pretty_name", "Material Groups"},
            entt::attribute{"tooltip", "Materials count."},
        })
        .data<nullptr, &mesh::info::lods>("lods"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lods"},
            entt::attribute{"pretty_name", "LODs"},
            entt::attribute{"tooltip", "Information about each LOD level."},
        })
        .data<nullptr, &mesh::info::vertex_memory>("vertex_memory"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vertex_memory"},
            entt::attribute{"pretty_name", "Vertex Memory"},
            entt::attribute{"tooltip", "CPU memory of the vertex data."},
        })
        .data<nullptr, &mesh::info::index_memory>("index_memory"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "index_memory"},
            entt::attribute{"pretty_name", "Index Memory"},
            entt::attribute{"tooltip", "CPU memory of the index data of every LOD."},
        });
}

REFLECT(mesh::gi_info)
{
    entt::meta_factory<mesh::gi_info>{}
        .type("gi_info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_info"},
        })
        .data<nullptr, &mesh::gi_info::fields>("fields"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fields"},
            entt::attribute{"pretty_name", "Distance Fields"},
            entt::attribute{"tooltip",
                            "Submeshes with a distance field. A submesh without one (skinned, alpha blended, or\n"
                            "refused by the bake) neither occludes nor bounces indirect light."},
        })
        .data<nullptr, &mesh::gi_info::two_sided_fields>("two_sided_fields"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "two_sided_fields"},
            entt::attribute{"pretty_name", "Two Sided Fields"},
            entt::attribute{"tooltip", "Fields baked as unsigned shells: open or two-sided surfaces."},
        })
        .data<nullptr, &mesh::gi_info::surface_bricks>("surface_bricks"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface_bricks"},
            entt::attribute{"pretty_name", "Surface Bricks"},
            entt::attribute{"tooltip", "Bricks of 8x8x8 voxels near the surface, over the finest levels."},
        })
        .data<nullptr, &mesh::gi_info::min_voxel_size>("min_voxel_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_voxel_size"},
            entt::attribute{"pretty_name", "Finest Voxel"},
            entt::attribute{"tooltip", "The smallest voxel edge over the fields' finest levels, in local units."},
        })
        .data<nullptr, &mesh::gi_info::max_voxel_size>("max_voxel_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_voxel_size"},
            entt::attribute{"pretty_name", "Coarsest Voxel"},
            entt::attribute{"tooltip", "The largest voxel edge over the fields' finest levels, in local units."},
        })
        .data<nullptr, &mesh::gi_info::field_memory>("field_memory"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "field_memory"},
            entt::attribute{"pretty_name", "Field Memory"},
            entt::attribute{"tooltip",
                            "CPU memory of every level of every field. Each field keeps coarser levels the GI\n"
                            "falls back to when its atlas is full, about a quarter of the level above each."},
        })
        .data<nullptr, &mesh::gi_info::finest_field_memory>("finest_field_memory"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "finest_field_memory"},
            entt::attribute{"pretty_name", "Finest Level Memory"},
            entt::attribute{"tooltip",
                            "The finest levels alone: what the GI distance field atlas holds for one placement\n"
                            "of this mesh when every field is resident at full detail."},
        })
        .data<nullptr, &mesh::gi_info::card_source>("card_source"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_source"},
            entt::attribute{"pretty_name", "Surface Cache Cards"},
            entt::attribute{"tooltip",
                            "Compiled: built with the asset (Import > Cards).\n"
                            "Built at runtime: the asset carries none, so they are built on first use.\n"
                            "Disabled: the import settings turned them off."},
        })
        .data<nullptr, &mesh::gi_info::card_sets>("card_sets"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_sets"},
            entt::attribute{"pretty_name", "Submeshes With Cards"},
            entt::attribute{"tooltip", "Submeshes with at least one compiled card."},
        })
        .data<nullptr, &mesh::gi_info::cards>("cards"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cards"},
            entt::attribute{"pretty_name", "Cards"},
            entt::attribute{"tooltip",
                            "Compiled cards over every submesh. The surface cache captures and lights each one,\n"
                            "so more cards means better coverage and more runtime work."},
        })
        .data<nullptr, &mesh::gi_info::max_cards_per_submesh>("max_cards_per_submesh"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_cards_per_submesh"},
            entt::attribute{"pretty_name", "Most Cards In A Submesh"},
            entt::attribute{"tooltip", "The most cards any one submesh has (the import settings' Max Cards caps it)."},
        })
        .data<nullptr, &mesh::gi_info::card_memory>("card_memory"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_memory"},
            entt::attribute{"pretty_name", "Card Memory"},
            entt::attribute{"tooltip", "CPU memory of the compiled cards."},
        })
        .data<nullptr, &mesh::gi_info::card_table_memory>("card_table_memory"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_table_memory"},
            entt::attribute{"pretty_name", "Card Table Per Placement"},
            entt::attribute{"tooltip",
                            "GPU memory one placement of the whole mesh takes in the surface cache's card table.\n"
                            "The captured texels come on top, from the shared surface cache atlas."},
        });
}

REFLECT(mesh)
{
    entt::meta_factory<mesh>{}
        .type("mesh"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh"},
        })
        .data<nullptr, &mesh::get_info>("info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "info"},
            entt::attribute{"pretty_name", "Info"},
            entt::attribute{"tooltip", "Info about the mesh."},
        })
        .data<nullptr, &mesh::get_gi_info>("gi_info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_info"},
            entt::attribute{"pretty_name", "Global Illumination"},
            entt::attribute{"tooltip", "The mesh's distance fields and surface cache cards, and their memory."},
        })
        .data<nullptr, &mesh::get_imported_materials>("imported_materials"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "imported_materials"},
            entt::attribute{"pretty_name", "Imported Materials"},
            entt::attribute{"tooltip", "Imported materials."},
        });
}
SAVE(mesh::submesh)
{
    try_save(ar, ser20::make_nvp("data_group_id", obj.data_group_id));
    try_save(ar, ser20::make_nvp("vertex_start", obj.vertex_start));
    try_save(ar, ser20::make_nvp("vertex_count", obj.vertex_count));
    try_save(ar, ser20::make_nvp("face_start", obj.face_start));
    try_save(ar, ser20::make_nvp("face_count", obj.face_count));
    try_save(ar, ser20::make_nvp("bbox", obj.bbox));
    try_save(ar, ser20::make_nvp("skinned", obj.skinned));
    try_save(ar, ser20::make_nvp("stable_id", obj.stable_id));
}
SAVE_INSTANTIATE(mesh::submesh, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(mesh::submesh, ser20::oarchive_associative_t);

LOAD(mesh::submesh)
{
    try_load(ar, ser20::make_nvp("data_group_id", obj.data_group_id));
    try_load(ar, ser20::make_nvp("vertex_start", obj.vertex_start));
    try_load(ar, ser20::make_nvp("vertex_count", obj.vertex_count));
    try_load(ar, ser20::make_nvp("face_start", obj.face_start));
    try_load(ar, ser20::make_nvp("face_count", obj.face_count));
    try_load(ar, ser20::make_nvp("bbox", obj.bbox));
    try_load(ar, ser20::make_nvp("skinned", obj.skinned));
    try_load(ar, ser20::make_nvp("stable_id", obj.stable_id));
}
LOAD_INSTANTIATE(mesh::submesh, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(mesh::submesh, ser20::iarchive_associative_t);

SAVE(mesh::triangle)
{
    try_save(ar, ser20::make_nvp("data_group_id", obj.data_group_id));
    try_save(ar, ser20::make_nvp("indices", obj.indices));
    try_save(ar, ser20::make_nvp("flags", obj.flags));
}
SAVE_INSTANTIATE(mesh::triangle, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(mesh::triangle, ser20::oarchive_associative_t);

LOAD(mesh::triangle)
{
    try_load(ar, ser20::make_nvp("data_group_id", obj.data_group_id));
    try_load(ar, ser20::make_nvp("indices", obj.indices));
    try_load(ar, ser20::make_nvp("flags", obj.flags));
}
LOAD_INSTANTIATE(mesh::triangle, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(mesh::triangle, ser20::iarchive_associative_t);

SAVE(skin_bind_data::vertex_influence)
{
    try_save(ar, ser20::make_nvp("vertex_index", obj.vertex_index));
    try_save(ar, ser20::make_nvp("weight", obj.weight));
}
SAVE_INSTANTIATE(skin_bind_data::vertex_influence, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(skin_bind_data::vertex_influence, ser20::oarchive_associative_t);

LOAD(skin_bind_data::vertex_influence)
{
    try_load(ar, ser20::make_nvp("vertex_index", obj.vertex_index));
    try_load(ar, ser20::make_nvp("weight", obj.weight));
}
LOAD_INSTANTIATE(skin_bind_data::vertex_influence, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(skin_bind_data::vertex_influence, ser20::iarchive_associative_t);

SAVE(skin_bind_data::bone_influence)
{
    try_save(ar, ser20::make_nvp("bone_id", obj.bone_id));
    try_save(ar, ser20::make_nvp("bind_pose_transform", obj.bind_pose_transform));
    try_save(ar, ser20::make_nvp("influences", obj.influences));
    try_save(ar, ser20::make_nvp("bounds", obj.bounds));
}
SAVE_INSTANTIATE(skin_bind_data::bone_influence, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(skin_bind_data::bone_influence, ser20::oarchive_associative_t);

LOAD(skin_bind_data::bone_influence)
{
    try_load(ar, ser20::make_nvp("bone_id", obj.bone_id));
    try_load(ar, ser20::make_nvp("bind_pose_transform", obj.bind_pose_transform));
    try_load(ar, ser20::make_nvp("influences", obj.influences));
    try_load(ar, ser20::make_nvp("bounds", obj.bounds));
}
LOAD_INSTANTIATE(skin_bind_data::bone_influence, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(skin_bind_data::bone_influence, ser20::iarchive_associative_t);

SAVE(skin_bind_data)
{
    try_save(ar, ser20::make_nvp("bones", obj.get_bones()));
}
SAVE_INSTANTIATE(skin_bind_data, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(skin_bind_data, ser20::oarchive_associative_t);

LOAD(skin_bind_data)
{
    try_load(ar, ser20::make_nvp("bones", obj.get_bones()));
}
LOAD_INSTANTIATE(skin_bind_data, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(skin_bind_data, ser20::iarchive_associative_t);

SAVE(mesh::armature_node)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("local_transform", obj.local_transform));
    try_save(ar, ser20::make_nvp("children", obj.children));
    try_save(ar, ser20::make_nvp("submeshes", obj.submeshes));
    try_save(ar, ser20::make_nvp("index", obj.index));
}
SAVE_INSTANTIATE(mesh::armature_node, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(mesh::armature_node, ser20::oarchive_associative_t);

LOAD(mesh::armature_node)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("local_transform", obj.local_transform));
    try_load(ar, ser20::make_nvp("children", obj.children));
    try_load(ar, ser20::make_nvp("submeshes", obj.submeshes));
    try_load(ar, ser20::make_nvp("index", obj.index));
}
LOAD_INSTANTIATE(mesh::armature_node, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(mesh::armature_node, ser20::iarchive_associative_t);

SAVE(mesh::lod_load_data)
{
    try_save(ar, ser20::make_nvp("index_data", obj.index_data));
    try_save(ar, ser20::make_nvp("face_count", obj.face_count));
    try_save(ar, ser20::make_nvp("submeshes", obj.submeshes));
    try_save(ar, ser20::make_nvp("simplification_error", obj.simplification_error));
}
SAVE_INSTANTIATE(mesh::lod_load_data, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(mesh::lod_load_data, ser20::oarchive_associative_t);

LOAD(mesh::lod_load_data)
{
    try_load(ar, ser20::make_nvp("index_data", obj.index_data));
    try_load(ar, ser20::make_nvp("face_count", obj.face_count));
    try_load(ar, ser20::make_nvp("submeshes", obj.submeshes));
    try_load(ar, ser20::make_nvp("simplification_error", obj.simplification_error));
}
LOAD_INSTANTIATE(mesh::lod_load_data, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(mesh::lod_load_data, ser20::iarchive_associative_t);

SAVE(mesh::load_data)
{
    try_save(ar, ser20::make_nvp("vertex_format", obj.vertex_format));
    try_save(ar, ser20::make_nvp("vertex_count", obj.vertex_count));
    try_save(ar, ser20::make_nvp("vertex_data", obj.vertex_data));
    try_save(ar, ser20::make_nvp("triangle_count", obj.triangle_count));
    try_save(ar, ser20::make_nvp("triangle_data", obj.triangle_data));
    try_save(ar, ser20::make_nvp("material_count", obj.material_count));
    try_save(ar, ser20::make_nvp("submeshes", obj.submeshes));
    try_save(ar, ser20::make_nvp("skin_data", obj.skin_data));
    try_save(ar, ser20::make_nvp("root_node", obj.root_node));
    try_save(ar, ser20::make_nvp("bbox", obj.bbox));
    try_save(ar, ser20::make_nvp("skin_is_prepared", obj.skin_is_prepared));
    try_save(ar, ser20::make_nvp("bone_palette_bones", obj.bone_palette_bones));
    try_save(ar, ser20::make_nvp("lods", obj.lods));
    try_save(ar, ser20::make_nvp("default_material_uids", obj.default_material_uids));
    try_save(ar, ser20::make_nvp("submesh_sdfs", obj.submesh_sdfs));
    try_save(ar, ser20::make_nvp("submesh_sdf_coarse_mips", obj.submesh_sdf_coarse_mips));
    try_save(ar, ser20::make_nvp("submesh_cards", obj.submesh_cards));
    try_save(ar, ser20::make_nvp("are_cards_disabled", obj.are_cards_disabled));
    try_save(ar, ser20::make_nvp("cards_lod_index", obj.cards_lod_index));

    // Changes here should be reflected in ex::get_format_version<mesh>() in asset_extensions.h
}
SAVE_INSTANTIATE(mesh::load_data, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(mesh::load_data, ser20::oarchive_associative_t);

LOAD(mesh::load_data)
{
    try_load(ar, ser20::make_nvp("vertex_format", obj.vertex_format));
    try_load(ar, ser20::make_nvp("vertex_count", obj.vertex_count));
    try_load(ar, ser20::make_nvp("vertex_data", obj.vertex_data));
    try_load(ar, ser20::make_nvp("triangle_count", obj.triangle_count));
    try_load(ar, ser20::make_nvp("triangle_data", obj.triangle_data));
    try_load(ar, ser20::make_nvp("material_count", obj.material_count));
    try_load(ar, ser20::make_nvp("submeshes", obj.submeshes));
    try_load(ar, ser20::make_nvp("skin_data", obj.skin_data));
    try_load(ar, ser20::make_nvp("root_node", obj.root_node));
    try_load(ar, ser20::make_nvp("bbox", obj.bbox));
    try_load(ar, ser20::make_nvp("skin_is_prepared", obj.skin_is_prepared));
    try_load(ar, ser20::make_nvp("bone_palette_bones", obj.bone_palette_bones));
    try_load(ar, ser20::make_nvp("lods", obj.lods));
    try_load(ar, ser20::make_nvp("default_material_uids", obj.default_material_uids));
    try_load(ar, ser20::make_nvp("submesh_sdfs", obj.submesh_sdfs));
    try_load(ar, ser20::make_nvp("submesh_sdf_coarse_mips", obj.submesh_sdf_coarse_mips));
    try_load(ar, ser20::make_nvp("submesh_cards", obj.submesh_cards));
    try_load(ar, ser20::make_nvp("are_cards_disabled", obj.are_cards_disabled));
    try_load(ar, ser20::make_nvp("cards_lod_index", obj.cards_lod_index));

    // Changes here should be reflected in ex::get_format_version<mesh>() in asset_extensions.h
}
LOAD_INSTANTIATE(mesh::load_data, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(mesh::load_data, ser20::iarchive_associative_t);

void save_to_file(const std::string& absolute_path, const mesh::load_data& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("mesh", obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const mesh::load_data& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("mesh", obj));
    }
}

void load_from_file(const std::string& absolute_path, mesh::load_data& obj)
{
    fs::file_istream input(absolute_path);
    if(!input.is_open())
    {
        return;
    }

    auto ar = ser20::create_iarchive_associative(input);

    try_load(ar, ser20::make_nvp("mesh", obj));
}

void load_from_file_bin(const std::string& absolute_path, mesh::load_data& obj)
{
    fs::file_istream input(absolute_path, std::ios::binary);
    if(!input.is_open())
    {
        return;
    }
    ser20::iarchive_binary_t ar(input);
    try_load(ar, ser20::make_nvp("mesh", obj));
}

} // namespace unravel
