#include "graph_material.hpp"
#include <engine/meta/core/compiled_file.hpp>
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

// A compiled material's binding (material_graph_binding), in the binary archive alone: what loading the material binds
// without compiling its graph. Its parameters, textures and static switches are lists of these; enums are written as
// their underlying values.

SAVE(material_graph_parameter)
{
    try_save(ar, ser20::make_nvp("node_id", obj.node_id));
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("description", obj.description));
    try_save(ar, ser20::make_nvp("type", uint8_t(obj.type)));
    try_save(ar, ser20::make_nvp("default_value", obj.default_value));
    try_save(ar, ser20::make_nvp("register_index", obj.register_index));
    try_save(ar, ser20::make_nvp("component", obj.component));
    try_save(ar, ser20::make_nvp("changes", obj.changes));
}

LOAD(material_graph_parameter)
{
    uint8_t type = 0;
    try_load(ar, ser20::make_nvp("node_id", obj.node_id));
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("description", obj.description));
    try_load(ar, ser20::make_nvp("type", type));
    try_load(ar, ser20::make_nvp("default_value", obj.default_value));
    try_load(ar, ser20::make_nvp("register_index", obj.register_index));
    try_load(ar, ser20::make_nvp("component", obj.component));
    try_load(ar, ser20::make_nvp("changes", obj.changes));
    obj.type = material_value_type(type);
}

SAVE(material_graph_texture)
{
    try_save(ar, ser20::make_nvp("node_id", obj.node_id));
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("description", obj.description));
    try_save(ar, ser20::make_nvp("texture", obj.texture));
    try_save(ar, ser20::make_nvp("sampler", obj.sampler));
    try_save(ar, ser20::make_nvp("stage", obj.stage));
    try_save(ar, ser20::make_nvp("is_normal_map", obj.is_normal_map));
    try_save(ar, ser20::make_nvp("kind", uint8_t(obj.kind)));
    try_save(ar, ser20::make_nvp("address", uint8_t(obj.address)));
    try_save(ar, ser20::make_nvp("filter", uint8_t(obj.filter)));
    try_save(ar, ser20::make_nvp("changes", obj.changes));
}

LOAD(material_graph_texture)
{
    uint8_t kind = 0;
    uint8_t address = 0;
    uint8_t filter = 0;
    try_load(ar, ser20::make_nvp("node_id", obj.node_id));
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("description", obj.description));
    try_load(ar, ser20::make_nvp("texture", obj.texture));
    try_load(ar, ser20::make_nvp("sampler", obj.sampler));
    try_load(ar, ser20::make_nvp("stage", obj.stage));
    try_load(ar, ser20::make_nvp("is_normal_map", obj.is_normal_map));
    try_load(ar, ser20::make_nvp("kind", kind));
    try_load(ar, ser20::make_nvp("address", address));
    try_load(ar, ser20::make_nvp("filter", filter));
    try_load(ar, ser20::make_nvp("changes", obj.changes));
    obj.kind = material_value_type(kind);
    obj.address = material_texture_address(address);
    obj.filter = material_texture_filter(filter);
}

SAVE(material_graph_static_switch)
{
    try_save(ar, ser20::make_nvp("node_id", obj.node_id));
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("description", obj.description));
    try_save(ar, ser20::make_nvp("value", obj.value));
}

LOAD(material_graph_static_switch)
{
    try_load(ar, ser20::make_nvp("node_id", obj.node_id));
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("description", obj.description));
    try_load(ar, ser20::make_nvp("value", obj.value));
}

namespace
{
template<typename Archive>
void save_binding(Archive& ar, const material_graph_binding& binding)
{
    try_save(ar, ser20::make_nvp("parameters", binding.parameters));
    try_save(ar, ser20::make_nvp("parameter_registers", binding.parameter_registers));
    try_save(ar, ser20::make_nvp("textures", binding.textures));
    try_save(ar, ser20::make_nvp("static_switches", binding.static_switches));
    try_save(ar, ser20::make_nvp("emissive_is_known", binding.emissive.is_known));
    try_save(ar, ser20::make_nvp("emissive_constant", binding.emissive.constant));
    try_save(ar, ser20::make_nvp("emissive_parameters", binding.emissive.parameters));
    try_save(ar, ser20::make_nvp("uses_opacity_mask", binding.uses_opacity_mask));
    try_save(ar, ser20::make_nvp("is_opacity_mask_animated", binding.is_opacity_mask_animated));
    auto blend_mode = uint8_t(binding.blend_mode);
    try_save(ar, ser20::make_nvp("blend_mode", blend_mode));
    auto shading_model = uint8_t(binding.shading_model);
    try_save(ar, ser20::make_nvp("shading_model", shading_model));
    try_save(ar, ser20::make_nvp("preserve_specular", binding.preserve_specular));
    try_save(ar, ser20::make_nvp("taa_response", binding.taa_response));
    try_save(ar, ser20::make_nvp("uses_world_position_offset", binding.uses_world_position_offset));
    try_save(ar, ser20::make_nvp("is_world_position_offset_animated", binding.is_world_position_offset_animated));
    try_save(ar, ser20::make_nvp("max_world_position_offset", binding.max_world_position_offset));
    try_save(ar, ser20::make_nvp("reads_vertex_outputs", binding.reads_vertex_outputs));
    try_save(ar, ser20::make_nvp("computes_vertex_values", binding.computes_vertex_values));
    auto domain = uint8_t(binding.domain);
    try_save(ar, ser20::make_nvp("domain", domain));
    try_save(ar, ser20::make_nvp("decal_writes", binding.decal_writes));
    try_save(ar, ser20::make_nvp("decal_reads_surface", binding.decal_reads_surface));
    auto post_location = uint8_t(binding.post_location);
    try_save(ar, ser20::make_nvp("post_location", post_location));
    try_save(ar, ser20::make_nvp("post_priority", binding.post_priority));
    try_save(ar, ser20::make_nvp("scene_textures", binding.scene_textures));
    try_save(ar, ser20::make_nvp("samples_scene_neighbours", binding.samples_scene_neighbours));
    try_save(ar, ser20::make_nvp("order_independent", binding.order_independent));
    try_save(ar, ser20::make_nvp("casts_forward_shadow", binding.casts_forward_shadow));
    try_save(ar, ser20::make_nvp("depth_prepass", binding.depth_prepass));
    try_save(ar, ser20::make_nvp("writes_velocity", binding.writes_velocity));
    try_save(ar, ser20::make_nvp("has_gi_variant", binding.has_gi_variant));
    try_save(ar, ser20::make_nvp("has_gi_reflection_variant", binding.has_gi_reflection_variant));
}

template<typename Archive>
void load_binding(Archive& ar, material_graph_binding& binding)
{
    try_load(ar, ser20::make_nvp("parameters", binding.parameters));
    try_load(ar, ser20::make_nvp("parameter_registers", binding.parameter_registers));
    try_load(ar, ser20::make_nvp("textures", binding.textures));
    try_load(ar, ser20::make_nvp("static_switches", binding.static_switches));
    try_load(ar, ser20::make_nvp("emissive_is_known", binding.emissive.is_known));
    try_load(ar, ser20::make_nvp("emissive_constant", binding.emissive.constant));
    try_load(ar, ser20::make_nvp("emissive_parameters", binding.emissive.parameters));
    try_load(ar, ser20::make_nvp("uses_opacity_mask", binding.uses_opacity_mask));
    try_load(ar, ser20::make_nvp("is_opacity_mask_animated", binding.is_opacity_mask_animated));
    // Files of older format versions recompile (get_format_version<material>), so none is read here.
    uint8_t blend_mode = uint8_t(material_blend_mode::opaque);
    try_load(ar, ser20::make_nvp("blend_mode", blend_mode));
    binding.blend_mode = blend_mode < MATERIAL_BLEND_MODE_COUNT ? material_blend_mode(blend_mode) : material_blend_mode::opaque;
    uint8_t shading_model = 0;
    try_load(ar, ser20::make_nvp("shading_model", shading_model));
    binding.shading_model =
        shading_model < MATERIAL_SHADING_MODEL_COUNT ? material_shading_model(shading_model) : material_shading_model::default_lit;
    try_load(ar, ser20::make_nvp("preserve_specular", binding.preserve_specular));
    try_load(ar, ser20::make_nvp("taa_response", binding.taa_response));
    try_load(ar, ser20::make_nvp("uses_world_position_offset", binding.uses_world_position_offset));
    try_load(ar, ser20::make_nvp("is_world_position_offset_animated", binding.is_world_position_offset_animated));
    try_load(ar, ser20::make_nvp("max_world_position_offset", binding.max_world_position_offset));
    try_load(ar, ser20::make_nvp("reads_vertex_outputs", binding.reads_vertex_outputs));
    try_load(ar, ser20::make_nvp("computes_vertex_values", binding.computes_vertex_values));
    uint8_t domain = 0;
    try_load(ar, ser20::make_nvp("domain", domain));
    binding.domain = domain < MATERIAL_DOMAIN_COUNT ? material_domain(domain) : material_domain::surface;
    try_load(ar, ser20::make_nvp("decal_writes", binding.decal_writes));
    try_load(ar, ser20::make_nvp("decal_reads_surface", binding.decal_reads_surface));
    uint8_t post_location = 0;
    try_load(ar, ser20::make_nvp("post_location", post_location));
    binding.post_location = post_location < MATERIAL_POST_PROCESS_LOCATION_COUNT ? material_post_process_location(post_location)
                                                                                 : material_post_process_location::after_tonemapping;
    try_load(ar, ser20::make_nvp("post_priority", binding.post_priority));
    try_load(ar, ser20::make_nvp("scene_textures", binding.scene_textures));
    try_load(ar, ser20::make_nvp("samples_scene_neighbours", binding.samples_scene_neighbours));
    try_load(ar, ser20::make_nvp("order_independent", binding.order_independent));
    try_load(ar, ser20::make_nvp("casts_forward_shadow", binding.casts_forward_shadow));
    try_load(ar, ser20::make_nvp("depth_prepass", binding.depth_prepass));
    try_load(ar, ser20::make_nvp("writes_velocity", binding.writes_velocity));
    try_load(ar, ser20::make_nvp("has_gi_variant", binding.has_gi_variant));
    try_load(ar, ser20::make_nvp("has_gi_reflection_variant", binding.has_gi_reflection_variant));
}

constexpr const char* FUNCTIONS_KEY = "functions";

/// The copies of the material functions a compiled graph calls, keyed by the function's asset key.
template<typename Archive>
void save_functions(Archive& ar, const graph_material::function_copies& functions)
{
    ar.setNextName(FUNCTIONS_KEY);
    ar.startNode();
    for(const auto& [key, function] : functions)
    {
        try_save(ar, ser20::make_nvp(key.c_str(), *function));
    }
    ar.finishNode();
}

/// Loads what save_functions saved into functions; leaves them as they are when the document has none.
template<typename Archive>
void load_functions(Archive& ar, graph_material::function_copies& functions)
{
    graph_material::function_copies loaded;
    const bool has_functions = serialization::load_named_members(ar,
                                                                 FUNCTIONS_KEY,
                                                                 [&](const std::string& key)
                                                                 {
                                                                     auto function = std::make_shared<material_graph>();
                                                                     if(!try_load(ar, ser20::make_nvp(key.c_str(), *function)))
                                                                     {
                                                                         return false;
                                                                     }
                                                                     loaded[key] = std::move(function);
                                                                     return true;
                                                                 });
    if(has_functions)
    {
        functions = std::move(loaded);
    }
}
} // namespace

SAVE(graph_material)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<material>(&obj)));
    try_save(ar, ser20::make_nvp("graph", obj.get_graph()));
    // The source is the graph alone. A binary document - the compiled material, or a copy - keeps the material functions
    // the graph calls, and once the material is compiled, the code its shader files hold and what a draw of that code
    // binds. A material not compiled has neither: a copy of one binds its graph when loaded.
    if constexpr(std::is_same_v<Archive, ser20::oarchive_binary_t>)
    {
        if(!obj.get_shader_hash().empty())
        {
            try_save(ar, ser20::make_nvp("shader_hash", obj.get_shader_hash()));
            save_binding(ar, obj.get_binding());
        }
        save_functions(ar, obj.get_functions());
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
    // The copies first: a graph bound below calls them.
    load_functions(ar, obj.functions_);
    std::string shader_hash;
    if(try_load(ar, ser20::make_nvp("shader_hash", shader_hash)) && !shader_hash.empty())
    {
        // A compiled material binds what its compile laid out: loading it compiles nothing.
        material_graph_binding binding;
        load_binding(ar, binding);
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
    write_compiled_file(absolute_path, "graph_material_shader", shader);
}

auto load_from_file_bin(const std::string& absolute_path, compiled_graph_material_shader& shader) -> bool
{
    return load_compiled_file(absolute_path, "graph_material_shader", shader);
}

auto load_header_from_file_bin(const std::string& absolute_path, compiled_graph_material_shader_header& header) -> bool
{
    auto input = fs::open_readable_stream(absolute_path);
    if(!input)
    {
        return false;
    }
    // A file of an older format is simply not current: it compiles again, with nothing to warn about.
    if(!serialization::is_binary_document(*input))
    {
        return false;
    }
    const auto read_header = [&](auto& ar) -> bool
    {
        if(!ar.hasNextName("graph_material_shader"))
        {
            return false;
        }
        ar.setNextName("graph_material_shader");
        ar.startNode();
        ser20::size_type variant_count = 0;
        // Every shader file holds its variants, an empty list for a compile that failed.
        const bool has_header = try_load(ar, ser20::make_nvp("code_hash", header.code_hash)) &&
                                try_load(ar, ser20::make_nvp("compile_parameters", header.compile_parameters)) &&
                                ar.hasNextName("variants");
        if(has_header)
        {
            ar.setNextName("variants");
            ar.startNode();
            ar(ser20::make_size_tag(variant_count));
            ar.finishNode();
        }
        ar.finishNode();
        header.has_variants = variant_count > 0;
        return has_header;
    };
    // The two strings and the variants' count, from the file's first bytes: the variants' code, megabytes of it,
    // is not read. A header longer than those bytes is read from the whole file.
    constexpr size_t header_bytes = 64 * 1024;
    const auto start = input->tellg();
    input->seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(input->tellg() - start);
    input->seekg(start);
    if(serialization::read_binary_head(*input, header_bytes, read_header))
    {
        return true;
    }
    // A file the first bytes held whole has no more to read.
    if(size <= header_bytes)
    {
        return false;
    }
    input->clear();
    input->seekg(start);
    return serialization::read_binary(*input, read_header);
}

auto get_graph_material_shader_file(const std::string& compiled_material_path, const std::string& renderer_extension)
    -> std::string
{
    return compiled_material_path + renderer_extension;
}

} // namespace unravel
