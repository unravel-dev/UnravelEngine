#include "graph_material.hpp"
#include "material.hpp"
#include "material_graph.hpp"

#include <filesystem/filesystem.h>
#include <filesystem/pack.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <fstream>

namespace unravel
{

REFLECT(graph_material)
{
    entt::meta_factory<graph_material>{}
        .type("graph_material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "graph_material"},
            entt::attribute{"pretty_name", "Material Graph"},
        })
        .base<material>()
        .func<&graph_material::get_meta_type>("get_meta_type"_hs)
        .func<&graph_material::get_static_meta_type>("get_static_meta_type"_hs)
        .func<&graph_material::as_derived>("as_derived"_hs);
}

SAVE(graph_material)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<material>(&obj)));
    try_save(ar, ser20::make_nvp("graph", obj.get_graph()));
    // The source is the graph alone; the compiled material names the code its shader files hold, and keeps the
    // material functions the graph calls.
    if constexpr(is_binary_archive<Archive>())
    {
        try_save(ar, ser20::make_nvp("shader_hash", obj.get_shader_hash()));
        auto count = uint32_t(obj.get_functions().size());
        try_save(ar, ser20::make_nvp("function_count", count));
        for(const auto& [key, function] : obj.get_functions())
        {
            auto name = key;
            try_save(ar, ser20::make_nvp("function_key", name));
            try_save(ar, ser20::make_nvp("function", *function));
        }
    }

    // Changes here should be reflected in ex::get_format_version<material>() in asset_extensions.h
}
SAVE_INSTANTIATE(graph_material, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(graph_material, ser20::oarchive_binary_t);

LOAD(graph_material)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<material>(&obj)));
    material_graph graph;
    try_load(ar, ser20::make_nvp("graph", graph));
    if constexpr(is_binary_archive<Archive>())
    {
        std::string shader_hash;
        try_load(ar, ser20::make_nvp("shader_hash", shader_hash));
        obj.set_shader_hash(std::move(shader_hash));
        uint32_t count = 0;
        try_load(ar, ser20::make_nvp("function_count", count));
        graph_material::function_copies functions;
        for(uint32_t i = 0; i < count; ++i)
        {
            std::string key;
            auto function = std::make_shared<material_graph>();
            try_load(ar, ser20::make_nvp("function_key", key));
            try_load(ar, ser20::make_nvp("function", *function));
            functions[key] = std::move(function);
        }
        // Before the graph, without a compile of their own: set_graph binds the graph against them.
        obj.functions_ = std::move(functions);
    }
    obj.set_graph(std::move(graph));

    // Changes here should be reflected in ex::get_format_version<material>() in asset_extensions.h
}
LOAD_INSTANTIATE(graph_material, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(graph_material, ser20::iarchive_binary_t);

SAVE(compiled_graph_material_shader)
{
    try_save(ar, ser20::make_nvp("code_hash", obj.code_hash));
    try_save(ar, ser20::make_nvp("compile_parameters", obj.compile_parameters));
    try_save(ar, ser20::make_nvp("variants", obj.variants));
    try_save(ar, ser20::make_nvp("vertex_variants", obj.vertex_variants));
}
SAVE_INSTANTIATE(compiled_graph_material_shader, ser20::oarchive_binary_t);

LOAD(compiled_graph_material_shader)
{
    try_load(ar, ser20::make_nvp("code_hash", obj.code_hash));
    try_load(ar, ser20::make_nvp("compile_parameters", obj.compile_parameters));
    try_load(ar, ser20::make_nvp("variants", obj.variants));
    try_load(ar, ser20::make_nvp("vertex_variants", obj.vertex_variants));
}
LOAD_INSTANTIATE(compiled_graph_material_shader, ser20::iarchive_binary_t);

void save_to_file_bin(const std::string& absolute_path, const compiled_graph_material_shader& shader)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("graph_material_shader", shader));
    }
}

auto load_from_file_bin(const std::string& absolute_path, compiled_graph_material_shader& shader) -> bool
{
    auto input = fs::open_readable_stream(absolute_path);
    if(!input)
    {
        return false;
    }
    ser20::iarchive_binary_t ar(*input);
    return try_load(ar, ser20::make_nvp("graph_material_shader", shader));
}

auto load_header_from_file_bin(const std::string& absolute_path, compiled_graph_material_shader_header& header) -> bool
{
    auto input = fs::open_readable_stream(absolute_path);
    if(!input)
    {
        return false;
    }
    // The fields SAVE(compiled_graph_material_shader) writes first, then the variants' count.
    ser20::iarchive_binary_t ar(*input);
    ser20::size_type variant_count = 0;
    try
    {
        ar(header.code_hash, header.compile_parameters, ser20::make_size_tag(variant_count));
    }
    catch(const std::exception&)
    {
        return false;
    }
    header.has_variants = variant_count > 0;
    return true;
}

auto get_graph_material_shader_file(const std::string& compiled_material_path, const std::string& renderer_extension)
    -> std::string
{
    return compiled_material_path + renderer_extension;
}

} // namespace unravel
