#include "mesh_shape.hpp"

#include <filesystem/file_istream.h>
#include <serialization/associative_archive.h>

#include <fstream>

namespace unravel
{

/// The name of the object an .emesh file holds.
constexpr const char* MESH_SHAPE_FILE_ROOT = "shape";

REFLECT(mesh_shape)
{
    entt::meta_factory<mesh_shape_kind>{}
        .type("mesh_shape_kind"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh_shape_kind"},
            entt::attribute{"pretty_name", "Shape Kind"},
        })
        .data<mesh_shape_kind::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<mesh_shape_kind::cube>("cube"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cube"},
            entt::attribute{"pretty_name", "Cube"},
        })
        .data<mesh_shape_kind::rounded_cube>("rounded_cube"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rounded_cube"},
            entt::attribute{"pretty_name", "Rounded Cube"},
        })
        .data<mesh_shape_kind::sphere>("sphere"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sphere"},
            entt::attribute{"pretty_name", "Sphere"},
        })
        .data<mesh_shape_kind::plane>("plane"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "plane"},
            entt::attribute{"pretty_name", "Plane"},
        })
        .data<mesh_shape_kind::cylinder>("cylinder"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cylinder"},
            entt::attribute{"pretty_name", "Cylinder"},
        })
        .data<mesh_shape_kind::capsule>("capsule"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "capsule"},
            entt::attribute{"pretty_name", "Capsule"},
        })
        .data<mesh_shape_kind::cone>("cone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cone"},
            entt::attribute{"pretty_name", "Cone"},
        })
        .data<mesh_shape_kind::torus>("torus"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "torus"},
            entt::attribute{"pretty_name", "Torus"},
        })
        .data<mesh_shape_kind::teapot>("teapot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "teapot"},
            entt::attribute{"pretty_name", "Teapot"},
        })
        .data<mesh_shape_kind::icosahedron>("icosahedron"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "icosahedron"},
            entt::attribute{"pretty_name", "Icosahedron"},
        })
        .data<mesh_shape_kind::dodecahedron>("dodecahedron"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "dodecahedron"},
            entt::attribute{"pretty_name", "Dodecahedron"},
        })
        .data<mesh_shape_kind::icosphere>("icosphere"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "icosphere"},
            entt::attribute{"pretty_name", "Icosphere"},
        })
        .data<mesh_shape_kind::wave_terrain>("wave_terrain"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "wave_terrain"},
            entt::attribute{"pretty_name", "Wave Terrain"},
        });

    entt::meta_factory<mesh_shape>{}
        .type("mesh_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh_shape"},
            entt::attribute{"pretty_name", "Mesh Shape"},
        })
        .data<&mesh_shape::kind>("kind"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "kind"},
            entt::attribute{"pretty_name", "Kind"},
            entt::attribute{"tooltip", "The generator that builds the mesh."},
        })
        .data<&mesh_shape::width>("width"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "width"},
            entt::attribute{"pretty_name", "Width"},
            entt::attribute{"tooltip", "The size along X of a cube, a plane and a wave terrain."},
        })
        .data<&mesh_shape::height>("height"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "height"},
            entt::attribute{"pretty_name", "Height"},
            entt::attribute{"tooltip", "The size along Y of a cube, a cylinder, a capsule and a cone, and the height of a wave terrain's hills."},
        })
        .data<&mesh_shape::depth>("depth"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth"},
            entt::attribute{"pretty_name", "Depth"},
            entt::attribute{"tooltip", "The size along Z of a cube, a plane and a wave terrain."},
        })
        .data<&mesh_shape::radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radius"},
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"tooltip", "The radius of a sphere, a cylinder, a capsule and a cone, and of a torus's ring."},
        })
        .data<&mesh_shape::tube_radius>("tube_radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tube_radius"},
            entt::attribute{"pretty_name", "Tube Radius"},
            entt::attribute{"tooltip", "The radius of a torus's tube."},
        })
        .data<&mesh_shape::width_segments>("width_segments"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "width_segments"},
            entt::attribute{"pretty_name", "Width Segments"},
            entt::attribute{"tooltip", "The segments along X of a cube, a plane and a wave terrain."},
        })
        .data<&mesh_shape::height_segments>("height_segments"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "height_segments"},
            entt::attribute{"pretty_name", "Height Segments"},
            entt::attribute{"tooltip", "The segments along Y of a cube."},
        })
        .data<&mesh_shape::depth_segments>("depth_segments"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_segments"},
            entt::attribute{"pretty_name", "Depth Segments"},
            entt::attribute{"tooltip", "The segments along Z of a cube, a plane and a wave terrain."},
        })
        .data<&mesh_shape::stacks>("stacks"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stacks"},
            entt::attribute{"pretty_name", "Stacks"},
            entt::attribute{"tooltip", "The segments along the axis of a round shape, and around a torus's ring."},
        })
        .data<&mesh_shape::slices>("slices"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "slices"},
            entt::attribute{"pretty_name", "Slices"},
            entt::attribute{"tooltip", "The segments around the axis of a round shape, and around a torus's tube."},
        })
        .data<&mesh_shape::level>("level"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "level"},
            entt::attribute{"pretty_name", "Level"},
            entt::attribute{"tooltip", "How often each edge of an icosphere's icosahedron is split."},
        });
}

SAVE(mesh_shape)
{
    const std::string kind_name(get_shape_kind_name(obj.kind));
    try_save(ar, ser20::make_nvp("kind", kind_name));
    try_save(ar, ser20::make_nvp("width", obj.width));
    try_save(ar, ser20::make_nvp("height", obj.height));
    try_save(ar, ser20::make_nvp("depth", obj.depth));
    try_save(ar, ser20::make_nvp("radius", obj.radius));
    try_save(ar, ser20::make_nvp("tube_radius", obj.tube_radius));
    try_save(ar, ser20::make_nvp("width_segments", obj.width_segments));
    try_save(ar, ser20::make_nvp("height_segments", obj.height_segments));
    try_save(ar, ser20::make_nvp("depth_segments", obj.depth_segments));
    try_save(ar, ser20::make_nvp("stacks", obj.stacks));
    try_save(ar, ser20::make_nvp("slices", obj.slices));
    try_save(ar, ser20::make_nvp("level", obj.level));
}
SAVE_INSTANTIATE(mesh_shape, ser20::oarchive_associative_t);

LOAD(mesh_shape)
{
    std::string kind_name;
    try_load(ar, ser20::make_nvp("kind", kind_name));
    obj.kind = find_shape_kind(kind_name).value_or(mesh_shape_kind::none);
    try_load(ar, ser20::make_nvp("width", obj.width));
    try_load(ar, ser20::make_nvp("height", obj.height));
    try_load(ar, ser20::make_nvp("depth", obj.depth));
    try_load(ar, ser20::make_nvp("radius", obj.radius));
    try_load(ar, ser20::make_nvp("tube_radius", obj.tube_radius));
    try_load(ar, ser20::make_nvp("width_segments", obj.width_segments));
    try_load(ar, ser20::make_nvp("height_segments", obj.height_segments));
    try_load(ar, ser20::make_nvp("depth_segments", obj.depth_segments));
    try_load(ar, ser20::make_nvp("stacks", obj.stacks));
    try_load(ar, ser20::make_nvp("slices", obj.slices));
    try_load(ar, ser20::make_nvp("level", obj.level));
}
LOAD_INSTANTIATE(mesh_shape, ser20::iarchive_associative_t);

void save_to_file(const std::string& absolute_path, const mesh_shape& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp(MESH_SHAPE_FILE_ROOT, obj));
    }
}

auto load_from_file(const std::string& absolute_path, mesh_shape& obj) -> bool
{
    fs::file_istream input(absolute_path);
    if(!input.is_open())
    {
        return false;
    }
    auto ar = ser20::create_iarchive_associative(input);
    try_load(ar, ser20::make_nvp(MESH_SHAPE_FILE_ROOT, obj));
    return true;
}

} // namespace unravel
