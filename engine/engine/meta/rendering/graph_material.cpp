#include "graph_material.hpp"
#include "material.hpp"
#include "material_graph.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
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

namespace
{
// A compiled material's binding (material_graph_binding), in the binary archive alone: what loading the material binds
// without compiling its graph. Enums are written as their underlying values.

template<typename Archive>
void save_parameter(Archive& ar, const material_graph_parameter& parameter)
{
    try_save(ar, ser20::make_nvp("node_id", parameter.node_id));
    try_save(ar, ser20::make_nvp("name", parameter.name));
    try_save(ar, ser20::make_nvp("description", parameter.description));
    try_save(ar, ser20::make_nvp("type", uint8_t(parameter.type)));
    try_save(ar, ser20::make_nvp("default_value", parameter.default_value));
    try_save(ar, ser20::make_nvp("register_index", parameter.register_index));
    try_save(ar, ser20::make_nvp("component", parameter.component));
    try_save(ar, ser20::make_nvp("changes", parameter.changes));
}

template<typename Archive>
void load_parameter(Archive& ar, material_graph_parameter& parameter)
{
    uint8_t type = 0;
    try_load(ar, ser20::make_nvp("node_id", parameter.node_id));
    try_load(ar, ser20::make_nvp("name", parameter.name));
    try_load(ar, ser20::make_nvp("description", parameter.description));
    try_load(ar, ser20::make_nvp("type", type));
    try_load(ar, ser20::make_nvp("default_value", parameter.default_value));
    try_load(ar, ser20::make_nvp("register_index", parameter.register_index));
    try_load(ar, ser20::make_nvp("component", parameter.component));
    try_load(ar, ser20::make_nvp("changes", parameter.changes));
    parameter.type = material_value_type(type);
}

template<typename Archive>
void save_texture(Archive& ar, const material_graph_texture& texture)
{
    try_save(ar, ser20::make_nvp("node_id", texture.node_id));
    try_save(ar, ser20::make_nvp("name", texture.name));
    try_save(ar, ser20::make_nvp("description", texture.description));
    try_save(ar, ser20::make_nvp("texture", texture.texture));
    try_save(ar, ser20::make_nvp("sampler", texture.sampler));
    try_save(ar, ser20::make_nvp("stage", texture.stage));
    try_save(ar, ser20::make_nvp("is_normal_map", texture.is_normal_map));
    try_save(ar, ser20::make_nvp("kind", uint8_t(texture.kind)));
    try_save(ar, ser20::make_nvp("address", uint8_t(texture.address)));
    try_save(ar, ser20::make_nvp("filter", uint8_t(texture.filter)));
    try_save(ar, ser20::make_nvp("changes", texture.changes));
}

template<typename Archive>
void load_texture(Archive& ar, material_graph_texture& texture)
{
    uint8_t kind = 0;
    uint8_t address = 0;
    uint8_t filter = 0;
    try_load(ar, ser20::make_nvp("node_id", texture.node_id));
    try_load(ar, ser20::make_nvp("name", texture.name));
    try_load(ar, ser20::make_nvp("description", texture.description));
    try_load(ar, ser20::make_nvp("texture", texture.texture));
    try_load(ar, ser20::make_nvp("sampler", texture.sampler));
    try_load(ar, ser20::make_nvp("stage", texture.stage));
    try_load(ar, ser20::make_nvp("is_normal_map", texture.is_normal_map));
    try_load(ar, ser20::make_nvp("kind", kind));
    try_load(ar, ser20::make_nvp("address", address));
    try_load(ar, ser20::make_nvp("filter", filter));
    try_load(ar, ser20::make_nvp("changes", texture.changes));
    texture.kind = material_value_type(kind);
    texture.address = material_texture_address(address);
    texture.filter = material_texture_filter(filter);
}

template<typename Archive>
void save_static_switch(Archive& ar, const material_graph_static_switch& entry)
{
    try_save(ar, ser20::make_nvp("node_id", entry.node_id));
    try_save(ar, ser20::make_nvp("name", entry.name));
    try_save(ar, ser20::make_nvp("description", entry.description));
    try_save(ar, ser20::make_nvp("value", entry.value));
}

template<typename Archive>
void load_static_switch(Archive& ar, material_graph_static_switch& entry)
{
    try_load(ar, ser20::make_nvp("node_id", entry.node_id));
    try_load(ar, ser20::make_nvp("name", entry.name));
    try_load(ar, ser20::make_nvp("description", entry.description));
    try_load(ar, ser20::make_nvp("value", entry.value));
}

/// Writes @p entries as their count, then each with @p save_entry.
template<typename Archive, typename Entry, typename Save>
void save_entries(Archive& ar, const std::vector<Entry>& entries, Save save_entry)
{
    auto count = uint32_t(entries.size());
    try_save(ar, ser20::make_nvp("count", count));
    for(const auto& entry : entries)
    {
        save_entry(ar, entry);
    }
}

/// Reads what save_entries wrote.
template<typename Archive, typename Entry, typename Load>
void load_entries(Archive& ar, std::vector<Entry>& entries, Load load_entry)
{
    uint32_t count = 0;
    try_load(ar, ser20::make_nvp("count", count));
    entries.assign(count, Entry{});
    for(auto& entry : entries)
    {
        load_entry(ar, entry);
    }
}

template<typename Archive>
void save_binding(Archive& ar, const material_graph_binding& binding)
{
    save_entries(ar, binding.parameters, save_parameter<Archive>);
    try_save(ar, ser20::make_nvp("parameter_registers", binding.parameter_registers));
    save_entries(ar, binding.textures, save_texture<Archive>);
    save_entries(ar, binding.static_switches, save_static_switch<Archive>);
    try_save(ar, ser20::make_nvp("emissive_is_known", binding.emissive.is_known));
    try_save(ar, ser20::make_nvp("emissive_constant", binding.emissive.constant));
    try_save(ar, ser20::make_nvp("emissive_parameters", binding.emissive.parameters));
    try_save(ar, ser20::make_nvp("uses_opacity_mask", binding.uses_opacity_mask));
    try_save(ar, ser20::make_nvp("is_opacity_mask_animated", binding.is_opacity_mask_animated));
    try_save(ar, ser20::make_nvp("is_translucent", binding.is_translucent));
    try_save(ar, ser20::make_nvp("uses_world_position_offset", binding.uses_world_position_offset));
    try_save(ar, ser20::make_nvp("is_world_position_offset_animated", binding.is_world_position_offset_animated));
    try_save(ar, ser20::make_nvp("max_world_position_offset", binding.max_world_position_offset));
    try_save(ar, ser20::make_nvp("reads_vertex_outputs", binding.reads_vertex_outputs));
    try_save(ar, ser20::make_nvp("computes_vertex_values", binding.computes_vertex_values));
}

template<typename Archive>
void load_binding(Archive& ar, material_graph_binding& binding)
{
    load_entries(ar, binding.parameters, load_parameter<Archive>);
    try_load(ar, ser20::make_nvp("parameter_registers", binding.parameter_registers));
    load_entries(ar, binding.textures, load_texture<Archive>);
    load_entries(ar, binding.static_switches, load_static_switch<Archive>);
    try_load(ar, ser20::make_nvp("emissive_is_known", binding.emissive.is_known));
    try_load(ar, ser20::make_nvp("emissive_constant", binding.emissive.constant));
    try_load(ar, ser20::make_nvp("emissive_parameters", binding.emissive.parameters));
    try_load(ar, ser20::make_nvp("uses_opacity_mask", binding.uses_opacity_mask));
    try_load(ar, ser20::make_nvp("is_opacity_mask_animated", binding.is_opacity_mask_animated));
    try_load(ar, ser20::make_nvp("is_translucent", binding.is_translucent));
    try_load(ar, ser20::make_nvp("uses_world_position_offset", binding.uses_world_position_offset));
    try_load(ar, ser20::make_nvp("is_world_position_offset_animated", binding.is_world_position_offset_animated));
    try_load(ar, ser20::make_nvp("max_world_position_offset", binding.max_world_position_offset));
    try_load(ar, ser20::make_nvp("reads_vertex_outputs", binding.reads_vertex_outputs));
    try_load(ar, ser20::make_nvp("computes_vertex_values", binding.computes_vertex_values));
}
} // namespace

SAVE(graph_material)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<material>(&obj)));
    try_save(ar, ser20::make_nvp("graph", obj.get_graph()));
    // The source is the graph alone; the compiled material names the code its shader files hold, keeps what a draw
    // of that code binds, and the material functions the graph calls.
    if constexpr(is_binary_archive<Archive>())
    {
        try_save(ar, ser20::make_nvp("shader_hash", obj.get_shader_hash()));
        save_binding(ar, obj.get_binding());
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
        material_graph_binding binding;
        try_load(ar, ser20::make_nvp("shader_hash", shader_hash));
        load_binding(ar, binding);
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
        // A compiled material binds what its compile laid out: loading it compiles nothing.
        obj.functions_ = std::move(functions);
        obj.take_compiled(std::move(graph), std::move(shader_hash), std::move(binding));
    }
    else
    {
        obj.set_graph(std::move(graph));
    }

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
