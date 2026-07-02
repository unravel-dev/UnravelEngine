#include "mesh.hpp"

#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/core/math/quaternion.hpp>
#include <engine/meta/core/math/transform.hpp>
#include <engine/meta/core/math/bbox.hpp>

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
