/*
 * Validation suite for material graphs: the node graph a material is authored as, and the G-buffer fragment shader
 * the compiler generates from it.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "material graphs"
 *
 * What it pins:
 *   - links, ids and the editing helpers: renaming a node renames the links that name it, removing one clears them,
 *     the surface output is never removed;
 *   - copy and paste: pasted nodes get free ids and keep their links among themselves, a pasted surface output wires
 *     the graph's own;
 *   - the node library: unique types and shortcuts, every template names only pins and settings its node has;
 *   - the JSON format: a round trip keeps the graph, a hand-written graph loads with its fields in any order and the
 *     ones it leaves out at their defaults;
 *   - dynamic types resolve to the widest linked value; Append and Swizzle keep each input's own width;
 *   - conversions where a value is read: a float is broadcast, a wider vector truncated, a narrower one padded with a
 *     warning, and a vector never goes into a float - not linked directly, nor through a node it widens;
 *   - components: a link reads some components of an output ("color.rgba.rg"), a "pin.c" entry feeds one component
 *     of a vector input, which is then built from its components; split pins follow the component links;
 *   - textures: a node's texture is an asset handle, saved by uid and key, and found by its key alone;
 *   - what is reported, and where: unknown types, missing nodes and pins, loops, links into value-only inputs,
 *     clashing parameter names, bad masks and choices, a graph without output; check_material_graph_link refuses the
 *     links the editor must not make;
 *   - only what feeds the output is in the shader; each node's lines are where the result says;
 *   - parameters pack vectors first, then scalars four to a register;
 *   - every example the editor ships compiles without errors, and its shader - and one using every node type - compiles
 *     with shaderc on every renderer of this platform and the web's (WGSL, GLSL ES);
 *   - the binding a renderer relies on: samplers named by index on stages clear of the skinning palette's, at most
 *     MATERIAL_GRAPH_MAX_TEXTURES of them and MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS registers;
 *   - a graph material packs its parameters' defaults as the shader reads them, draws two-sided as its graph says,
 *     and round-trips through a .mat;
 *   - a graph whose Opacity Mask clips has the four shadow caster variants, and knows whether the mask reads the time
 *     or the view (its shadow then changes every frame);
 *   - a Static Switch puts only the input it picks in the code; a Named Reroute Usage reads its declaration (a missing
 *     or doubled name is an error, its hidden input takes no link); a Custom node becomes a function of its inputs
 *     returning its Output type, and reading the time from it makes a mask animated; a Triplanar Sample reads a texture
 *     of the material through mg_triplanar;
 *   - World Position Offset moves the vertices in a vertex shader of its own: only what the offset reads is in it,
 *     Custom functions included, the fragment shader gets the shadow casters, and nothing of the offset; the offset
 *     animates when it reads the time; a Normal Map cannot feed it (an error, and no offset), a Screen Position reads
 *     the middle of the view (a warning); Max Offset is what bounds grow by, never below zero; a graph material keeps
 *     it all, draws its own shadow, counts among the materials growing bounds while it lives, copies too; its vertex
 *     shader compiles with shaderc on every tested renderer, every G-buffer and velocity variant, into the same file
 *     beside the compiled material;
 *   - a Material Function node puts its function's nodes in its place: its pins feed the function's inputs (a value
 *     becomes the default), its outputs give the function's; parameters of one name are one value across calls; a
 *     missing or recursive function is an error on the call, as is a problem inside one, named; Named Reroutes stay
 *     each call's own; functions call functions; a function compiled alone shows its first output; a graph material
 *     keeps copies of its functions in the compiled material and compiles with them where the files are gone;
 *   - parameters and textures take values by name over the graph's, kept across a new graph; the first four registers
 *     are what an instanced draw reads per instance, through the instance_material variant;
 *   - copying a graph material's values copies its graph, values and shader too (a material instance's resolved copy
 *     follows its shared material), and a value the source no longer sets goes back to the graph's default;
 *   - a material instance overrides graph parameters and textures by name: refused for names the graph lacks, applied
 *     on the resolved material over the shared one, shared draws only for parameters in the first registers of a graph
 *     whose mask does not clip, and kept through a JSON and a binary round trip;
 *   - a graph material's source holds its graph alone; compiling it writes nothing beside it and gives the compiled
 *     material the hash of its generated code, which a value edit keeps and a code edit changes; its shader compiles
 *     for every renderer of this platform into the files beside the compiled material, which a value edit leaves
 *     current and a code edit does not;
 *   - a parameter's default and a texture node's texture are set by node, refused for other nodes, and change no code;
 *     a scalar parameter's Slider Min and Max give its editing range only while the maximum is the greater; a Normal
 *     Map reads its texture's channel layout from u_mg_normal_channels, declared only where a normal map is read;
 *   - a PBR material converts to a graph that draws as it does: its maps read at its tiling, roughness and metallic
 *     from one map's green and blue or each map's red, the normal map at its bumpiness, its values as parameters named
 *     and ranged as the PBR inspector shows them, in linear color, its alpha cutout as the clipped Opacity Mask, no
 *     culling as Two Sided; what a graph cannot draw (blending, front-face culling, the distance fade) is noted.
 */

#include "../tests.h"

#include <asset_compiler/asset_compiler.h>
#include <engine/assets/asset_manager.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/rendering/graph_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/material_instance.hpp>
#include <engine/meta/rendering/material_graph.hpp>
#include <engine/meta/rendering/shader_variants.hpp>
#include <engine/meta/rendering/standard_material.hpp>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>
#include <engine/rendering/material_graph/material_graph_library.h>
#include <engine/rendering/material_graph/standard_material.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/skinning_palette_buffer.h>
#include <uuid/uuid.h>
#include <filesystem/filesystem.h>
#include <graphics/graphics.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel;

namespace
{
namespace ac = unravel::asset_compiler;

/// The folder under the tests' app protocol the generated shaders are written to.
constexpr const char* SHADER_FOLDER = "data/material_graph_tests";
constexpr const char* EXAMPLES_FOLDER = "data/material_graphs/examples";
constexpr const char* GRAPH_EXTENSION = ".mgraph";

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto contains(const std::string& text, const std::string& part) -> bool
{
    return text.find(part) != std::string::npos;
}

auto add_node(material_graph& graph, const std::string& id, const std::string& type) -> material_graph_node&
{
    auto& node = graph.nodes.emplace_back();
    node.id = id;
    node.type = type;
    return node;
}

void link(material_graph& graph, const std::string& node_id, const std::string& pin, const std::string& link_text)
{
    auto& node = graph.nodes[size_t(find_material_graph_node(graph, node_id))];
    get_or_add_material_graph_input(node, pin, {}).link = link_text;
}

void set_value(material_graph& graph, const std::string& node_id, const std::string& pin, const math::vec4& value)
{
    auto& node = graph.nodes[size_t(find_material_graph_node(graph, node_id))];
    get_or_add_material_graph_input(node, pin, {}).value = value;
}

auto make_graph_with_output() -> material_graph
{
    material_graph graph;
    ensure_material_graph_output(graph);
    return graph;
}

auto get_info(const material_graph& graph, const material_graph_compile_result& result, const std::string& id)
    -> const material_graph_node_info&
{
    return result.nodes[size_t(find_material_graph_node(graph, id))];
}

auto has_diagnostic(const material_graph_compile_result& result,
                    material_graph_severity severity,
                    const std::string& node_id,
                    const std::string& pin = {}) -> bool
{
    return std::any_of(result.diagnostics.begin(),
                       result.diagnostics.end(),
                       [&](const material_graph_diagnostic& diagnostic)
                       {
                           return diagnostic.severity == severity && diagnostic.node_id == node_id &&
                                  (pin.empty() || diagnostic.pin == pin);
                       });
}

auto has_warning(const material_graph_compile_result& result) -> bool
{
    return std::any_of(result.diagnostics.begin(),
                       result.diagnostics.end(),
                       [](const material_graph_diagnostic& diagnostic)
                       {
                           return diagnostic.severity == material_graph_severity::warning;
                       });
}

auto describe(const material_graph_compile_result& result) -> std::string
{
    std::string text;
    for(const auto& diagnostic : result.diagnostics)
    {
        text += "\n    [" + diagnostic.node_id + "." + diagnostic.pin + "] " + diagnostic.message;
    }
    return text;
}

void test_links_and_ids()
{
    std::printf("test_links_and_ids\n");
    const auto end = parse_material_graph_link("albedo.rgb");
    check(end.node == "albedo" && end.pin == "rgb", "a link splits into node and pin");
    check(parse_material_graph_link("albedo").pin.empty(), "a link without a pin names the first output");
    check(make_material_graph_link("albedo", "rgb") == "albedo.rgb" && make_material_graph_link("uv", "") == "uv",
          "links are made back the same way");
    const auto picked = parse_material_graph_link("color.rgba.rg");
    check(picked.node == "color" && picked.pin == "rgba" && picked.components == "rg", "a link can pick components");
    check(make_material_graph_link("color", "rgba", "r") == "color.rgba.r", "and is made back with them");
    const auto component = parse_material_graph_pin("base_color.g");
    check(component.pin == "base_color" && component.component == 1, "an entry can name one component of a pin");
    check(parse_material_graph_pin("base_color").component == -1, "an entry without one names the whole pin");
    check(make_material_graph_component_pin("normal", 2, "xyzw") == "normal.z", "component entries are made back");
    check(sanitize_material_graph_identifier("Base Color!") == "base_color", "an id loses what a shader name cannot hold");
    check(sanitize_material_graph_identifier("2nd") == "n2nd", "an id never starts with a digit");
    check(sanitize_material_graph_identifier("a__b") == "a_b", "an id never holds two underscores in a row (GLSL reserves them)");
    check(sanitize_material_graph_identifier("") == "node", "an empty id becomes a name");
    auto graph = make_graph_with_output();
    add_node(graph, "tint", "multiply");
    check(make_unique_material_graph_node_id(graph, "tint") == "tint_1", "a taken id is numbered");
    check(make_unique_material_graph_node_id(graph, "Glow") == "glow", "a free id is kept, made safe");
    link(graph, "output", "base_color", "tint.out");
    check(rename_material_graph_node(graph, "tint", "Tinted Albedo") == "tinted_albedo", "a rename returns the safe id");
    const auto* input = find_material_graph_input(graph.nodes[0], "base_color");
    check(input && input->link == "tinted_albedo.out", "a rename renames the links that name the node");
    check(remove_material_graph_node(graph, "tinted_albedo"), "a node is removed");
    check(input->link.empty(), "removing a node clears the links into it");
    check(!remove_material_graph_node(graph, "output"), "the surface output is never removed");
    check(!ensure_material_graph_output(graph), "a graph keeps its one output");
}

void test_copy_and_paste()
{
    std::printf("test_copy_and_paste\n");
    auto graph = make_graph_with_output();
    add_node(graph, "uv", "texcoord");
    add_node(graph, "scaled", "multiply");
    link(graph, "scaled", "a", "uv");
    add_node(graph, "wave", "sine");
    link(graph, "wave", "in", "scaled.out");
    const auto fragment = copy_material_graph_nodes(graph, {"scaled", "wave", "output"});
    check(fragment.nodes.size() == 2, "a copy holds the picked nodes, never the output");
    const auto added = paste_material_graph_nodes(graph, fragment, {10.0f, 20.0f});
    check(added.size() == 2 && added[0] == "scaled_1" && added[1] == "wave_1", "pasted nodes get free ids");
    const auto& wave = graph.nodes[size_t(find_material_graph_node(graph, "wave_1"))];
    check(wave.inputs[0].link == "scaled_1.out", "a link between pasted nodes follows their new ids");
    const auto& scaled = graph.nodes[size_t(find_material_graph_node(graph, "scaled_1"))];
    check(scaled.inputs[0].link == "uv", "a link to a node the graph has stays");
    check(scaled.position == math::vec2(10.0f, 20.0f), "pasted nodes move by the offset");
    material_graph whole;
    ensure_material_graph_output(whole);
    add_node(whole, "glow", "constant");
    link(whole, "output", "emissive", "glow");
    link(whole, "glow", "value", "missing");
    material_graph target = make_graph_with_output();
    add_node(target, "glow", "time");
    paste_material_graph_nodes(target, whole, {});
    const auto* emissive = find_material_graph_input(target.nodes[0], "emissive");
    check(target.nodes.size() == 3 && emissive && emissive->link == "glow_1", "a pasted graph wires the output it brings into the graph's own");
    check(target.nodes[2].inputs[0].link.empty(), "a link to a node neither has is cleared");
    material_graph function;
    add_node(function, "value", "function_input");
    add_node(function, "result", "function_output");
    paste_material_graph_nodes(function, whole, {});
    check(function.nodes.size() == 3 && find_material_graph_output(function) < 0,
          "a whole graph pasted into a material function brings its nodes and no Surface Output");
}

/// A reroute of either kind: its value is its input's.
auto is_pass_through(const material_node_def& def) -> bool
{
    return def.code == material_node_code::reroute || def.code == material_node_code::named_reroute_declaration ||
           def.code == material_node_code::named_reroute || def.code == material_node_code::function_output;
}

void test_library()
{
    std::printf("test_library\n");
    const auto& library = get_material_node_library();
    std::set<std::string> types;
    std::set<char> shortcuts;
    for(const auto& def : library)
    {
        check(types.insert(def.type).second, "the node type " + def.type + " is unique");
        check(def.shortcut == 0 || shortcuts.insert(def.shortcut).second, def.type + " has a shortcut of its own");
        const bool is_root = def.code == material_node_code::surface_output || def.code == material_node_code::discard;
        check(!def.outputs.empty() || is_root, def.type + " has an output");
        check(!def.description.empty(), def.type + " says what it does");
        const bool has_value = !is_root && !is_pass_through(def);
        check(has_material_node_preview(def) == has_value, def.type + (has_value ? " has a preview swatch" : " has no preview swatch"));
        for(const auto& setting : def.settings)
        {
            check(setting.choice_labels.empty() || setting.choice_labels.size() == setting.choices.size(),
                  def.type + "'s " + setting.name + " names every choice it labels");
        }
        for(const auto* expression : {&def.expression, &def.scalar_expression})
        {
            size_t position = 0;
            while((position = expression->find('{', position)) != std::string::npos)
            {
                const size_t close = expression->find('}', position);
                const std::string name = expression->substr(position + 1, close - position - 1);
                check(find_material_pin_def(def.inputs, name) || find_material_setting_def(def, name),
                      def.type + "'s code names " + name + ", a pin or setting it has");
                position = close;
            }
        }
    }
    check(find_material_node_def(MATERIAL_GRAPH_OUTPUT_TYPE) != nullptr, "the library has the surface output");
}

void test_round_trip()
{
    std::printf("test_round_trip\n");
    auto graph = make_graph_with_output();
    auto& constant = add_node(graph, "rough", "constant");
    constant.position = {-200.0f, 40.0f};
    set_value(graph, "rough", "value", {0.25f, 0.0f, 0.0f, 0.0f});
    link(graph, "output", "roughness", "rough");
    set_material_graph_setting(graph.nodes[0], "two_sided", "on");
    graph.comments.push_back({"Note", {1.0f, 2.0f}, {300.0f, 100.0f}});
    check(!contains(save_to_string(graph), "preview_hidden"), "a node showing its preview writes nothing for it");
    constant.is_preview_hidden = true;
    const std::string text = save_to_string(graph);
    check(contains(text, "\"preview_hidden\": true"), "a node with its preview hidden says so");
    material_graph loaded;
    check(load_from_string(text, loaded), "a saved graph loads");
    const int rough = find_material_graph_node(loaded, "rough");
    check(loaded == graph && rough >= 0 && loaded.nodes[size_t(rough)].is_preview_hidden, "a round trip keeps the graph, hidden previews too");
    check(contains(text, "\"value\": [\n") || contains(text, "\"value\": ["), "values save as lists of numbers");
    // Hand-written: fields in another order, positions and values left out.
    const std::string hand_written = R"({"material_graph": {"nodes": [
        {"type": "surface_output", "id": "output", "inputs": [{"link": "c", "pin": "base_color"}]},
        {"inputs": [{"value": [1.0, 0.5], "pin": "value"}], "type": "vector2", "id": "c"}
    ]}})";
    material_graph written;
    check(load_from_string(hand_written, written), "a hand-written graph loads");
    check(written.nodes.size() == 2 && written.nodes[1].inputs.size() == 1 && written.nodes[1].inputs[0].value == math::vec4(1.0f, 0.5f, 0.0f, 0.0f),
          "fields load in any order, and the missing ones keep their defaults");
    check(is_material_graph_never_laid_out(written), "a graph without positions is laid out by the editor");
    material_graph broken;
    check(!load_from_string("{ not json", broken), "text that does not parse fails the load");
}

void test_types()
{
    std::printf("test_types\n");
    auto graph = make_graph_with_output();
    add_node(graph, "uv", "texcoord");
    add_node(graph, "scaled", "multiply");
    link(graph, "scaled", "a", "uv");
    set_value(graph, "scaled", "b", math::vec4(4.0f));
    add_node(graph, "facing", "dot");
    link(graph, "facing", "a", "normal_in");
    add_node(graph, "normal_in", "vertex_normal");
    add_node(graph, "joined", "append");
    link(graph, "joined", "a", "scaled");
    add_node(graph, "position", "world_position");
    add_node(graph, "ground", "swizzle");
    link(graph, "ground", "in", "position");
    set_material_graph_setting(graph.nodes.back(), "mask", "xz");
    add_node(graph, "parts", "split");
    link(graph, "parts", "in", "ground");
    link(graph, "output", "base_color", "joined");
    link(graph, "output", "roughness", "facing");
    link(graph, "output", "metallic", "parts.z");
    link(graph, "output", "ambient_occlusion", "parts.y");
    const auto result = compile_material_graph(graph);
    check(!result.has_errors(), "the graph compiles" + describe(result));
    check(get_info(graph, result, "scaled").type == material_value_type::float2, "Multiply takes the widest linked value");
    const auto& facing = get_info(graph, result, "facing");
    check(facing.type == material_value_type::float1 && facing.input_types[1] == material_value_type::float3,
          "Dot gives a float, and its unlinked input reads as wide as its linked one");
    check(get_info(graph, result, "joined").type == material_value_type::float3, "Append adds the widths up (2 + 1)");
    check(get_info(graph, result, "ground").type == material_value_type::float2, "Swizzle is as wide as its mask");
    check(contains(result.shader, "s.metallic = 0.0;"), "a component a vector does not have reads 0");
    check(contains(result.shader, "n_scaled = (i.uv0 * vec2_splat(4.0))"), "an unlinked dynamic input broadcasts its value");
    check(contains(result.shader, "vec3 n_joined = vec3(n_scaled, 0.0)"), "Append joins its inputs at their own widths");
}

void test_components()
{
    std::printf("test_components\n");
    auto graph = make_graph_with_output();
    add_node(graph, "paint", "color");
    set_value(graph, "paint", "value", {0.2f, 0.4f, 0.6f, 1.0f});
    add_node(graph, "noise", "noise");
    add_node(graph, "panned", "panner");
    link(graph, "panned", "speed", "paint.rgba.rg");
    link(graph, "noise", "uv", "panned");
    link(graph, "output", "roughness", "paint.rgba.g");
    link(graph, "output", "base_color.g", "noise");
    set_value(graph, "output", "base_color", {0.1f, 0.0f, 0.3f, 0.0f});
    const auto result = compile_material_graph(graph);
    check(!result.has_errors(), "component links compile" + describe(result));
    check(contains(result.shader, "s.roughness = vec4(0.2, 0.4, 0.6, 1.0).y;"), "a link reads one component of an output");
    check(contains(result.shader, "vec4(0.2, 0.4, 0.6, 1.0).xy * i.time"), "or several, in order");
    check(contains(result.shader, "s.base_color = vec3(0.1, n_noise, 0.3);"),
          "a vector input with a linked component is built from its components, the others from its value");
    check(split_material_graph_component_pins(graph), "component links mark their pins split");
    check(is_material_graph_output_split(graph.nodes[size_t(find_material_graph_node(graph, "paint"))], "rgba") &&
              is_material_graph_input_split(graph.nodes[0], "base_color"),
          "the output a single component is read from, and the input a component feeds");
    link(graph, "output", "emissive", "paint.rgba.q");
    link(graph, "output", "metallic", "panned.uv.z");
    const auto bad = compile_material_graph(graph);
    check(has_diagnostic(bad, material_graph_severity::error, "output", "emissive"), "a component that is no letter is an error");
    check(has_diagnostic(bad, material_graph_severity::error, "output", "metallic"), "so is one the output does not have");
}

void test_vector_into_float()
{
    std::printf("test_vector_into_float\n");
    auto graph = make_graph_with_output();
    add_node(graph, "paint", "color");
    add_node(graph, "wide", "multiply");
    link(graph, "output", "roughness", "wide");
    add_node(graph, "position", "world_position");
    check(!check_material_graph_link(graph, "paint", "rgba", "output", "metallic").empty(), "a vector is not linked into a float");
    check(check_material_graph_link(graph, "paint", "rgba.r", "output", "metallic").empty(), "one of its components is");
    check(check_material_graph_link(graph, "paint", "a", "output", "metallic").empty(), "and so is a float output");
    check(!check_material_graph_link(graph, "position", "xyz", "wide", "a").empty(),
          "a vector is not linked into a node it would widen while that node feeds a float");
    check(check_material_graph_link(graph, "paint", "rgba", "output", "base_color").empty(), "a wider vector still goes into a vector");
    check(check_material_graph_link(graph, "paint", "rgba.b", "output", "base_color.r").empty(), "a float goes into a component");
    check(!check_material_graph_link(graph, "paint", "rgba", "output", "base_color.r").empty(), "a vector does not");
    check(!check_material_graph_link(graph, "paint", "rgba", "output", "roughness.r").empty(), "a float input has no components");
    link(graph, "wide", "a", "position");
    const auto result = compile_material_graph(graph);
    check(has_diagnostic(result, material_graph_severity::error, "output", "roughness"), "a vector reaching a float is an error in a file too");
}

void test_textures()
{
    std::printf("test_textures\n");
    const std::string key = "engine:/data/textures/default_color.dds";
    const std::string text = R"({"material_graph": {"nodes": [
        {"id": "output", "type": "surface_output", "inputs": [{"pin": "base_color", "link": "albedo.rgb"}]},
        {"id": "albedo", "type": "texture_sample", "texture_key": ")" + key + R"("}
    ]}})";
    material_graph graph;
    check(load_from_string(text, graph), "a graph naming a texture by key loads");
    const auto& albedo = graph.nodes[1];
    check(albedo.texture && albedo.texture.id() == key, "the key finds the texture");
    const auto result = compile_material_graph(graph);
    check(result.textures.size() == 1 && result.textures[0].texture == albedo.texture, "the compile binds the node's texture");
    check(!has_diagnostic(result, material_graph_severity::warning, "albedo"), "a node with a texture warns of nothing");
    check(contains(result.shader, "SAMPLER2D(s_mg_texture0, 0); // albedo"), "the shader names the texture's node");
    check(!contains(result.shader, MATERIAL_GRAPH_NORMAL_CHANNELS_UNIFORM), "a graph reading no normal map declares no layouts");
    material_graph other_texture = graph;
    other_texture.nodes[1].texture = {};
    check(compile_material_graph(other_texture).shader == result.shader,
          "another texture leaves the code as it is: it is bound at draw time");
    const std::string saved = save_to_string(graph);
    check(contains(saved, "\"texture_key\": \"" + key + "\"") && contains(saved, "\"uid\""), "it saves by uid and key");
    material_graph reloaded;
    check(load_from_string(saved, reloaded) && reloaded.nodes[1].texture == albedo.texture, "and reads back the same texture");
    check(!contains(save_to_string(make_graph_with_output()), "texture"), "a node without a texture saves none");
}

void test_conversions()
{
    std::printf("test_conversions\n");
    auto graph = make_graph_with_output();
    add_node(graph, "grey", "constant");
    set_value(graph, "grey", "value", math::vec4(0.5f));
    add_node(graph, "paint", "color");
    add_node(graph, "uv", "texcoord");
    link(graph, "output", "base_color", "grey");
    link(graph, "output", "emissive", "paint");
    link(graph, "output", "subsurface_color", "uv");
    const auto result = compile_material_graph(graph);
    check(contains(result.shader, "s.base_color = vec3_splat(0.5);"), "a float is broadcast to a vector");
    check(contains(result.shader, "s.emissive = vec4_splat(1.0).xyz;"), "a wider vector loses its last components");
    check(contains(result.shader, "s.subsurface_color = vec3(i.uv0, 0.0);"), "a narrower vector is padded with zeros");
    check(has_diagnostic(result, material_graph_severity::warning, "output", "subsurface_color"), "padding a vector is a warning");
    check(!result.has_errors(), "conversions are not errors");
}

void test_errors()
{
    std::printf("test_errors\n");
    auto graph = make_graph_with_output();
    add_node(graph, "mystery", "no_such_node");
    add_node(graph, "lost", "add");
    link(graph, "lost", "a", "nowhere");
    link(graph, "lost", "b", "uv.nothing");
    add_node(graph, "uv", "texcoord");
    add_node(graph, "loop_a", "add");
    add_node(graph, "loop_b", "add");
    link(graph, "loop_a", "a", "loop_b");
    link(graph, "loop_b", "a", "loop_a");
    add_node(graph, "fixed", "constant");
    link(graph, "fixed", "value", "uv");
    add_node(graph, "first", "scalar_parameter");
    set_material_graph_setting(graph.nodes.back(), "name", "Strength");
    add_node(graph, "second", "vector_parameter");
    set_material_graph_setting(graph.nodes.back(), "name", "Strength");
    add_node(graph, "picker", "swizzle");
    set_material_graph_setting(graph.nodes.back(), "mask", "xq");
    add_node(graph, "sum", "add");
    link(graph, "sum", "a", "first");
    link(graph, "sum", "b", "second.x");
    add_node(graph, "picked", "add");
    link(graph, "picked", "a", "picker");
    link(graph, "picked", "b", "loop_a");
    add_node(graph, "both", "add");
    link(graph, "both", "a", "sum");
    link(graph, "both", "b", "picked");
    link(graph, "output", "roughness", "both");
    link(graph, "output", "metallic", "fixed");
    link(graph, "output", "ambient_occlusion", "lost");
    set_material_graph_setting(graph.nodes[0], "two_sided", "sometimes");
    const auto result = compile_material_graph(graph);
    check(has_diagnostic(result, material_graph_severity::error, "mystery"), "an unknown node type is an error");
    check(has_diagnostic(result, material_graph_severity::error, "lost", "a"), "a link to a missing node is an error");
    check(has_diagnostic(result, material_graph_severity::error, "lost", "b"), "a link to a missing output is an error");
    check(has_diagnostic(result, material_graph_severity::error, "loop_a") || has_diagnostic(result, material_graph_severity::error, "loop_b"),
          "a loop is an error");
    check(has_diagnostic(result, material_graph_severity::error, "fixed", "value"), "a link into a value-only input is an error");
    check(has_diagnostic(result, material_graph_severity::error, "second", "name"), "two parameters of one name and two types are an error");
    check(contains(result.shader, "vec4 n_second = vec4") && !contains(result.shader, "n_second = MG_PARAM"),
          "and the second reads its default, not the first one's register");
    check(has_diagnostic(result, material_graph_severity::error, "picker", "mask"), "a bad mask is an error");
    check(has_diagnostic(result, material_graph_severity::error, "output", "two_sided"), "a choice that is not one is an error");
    check(contains(result.shader, "void evaluate_material"), "a graph with errors still gives a shader");
    material_graph empty;
    check(compile_material_graph(empty).has_errors(), "a graph without output is an error");
    check(!check_material_graph_link(graph, "loop_a", "", "loop_a", "b").empty(), "a node never links into itself");
    check(!check_material_graph_link(graph, "both", "", "sum", "a").empty(), "a link that closes a loop is refused");
    check(!check_material_graph_link(graph, "uv", "", "fixed", "value").empty(), "a value-only input takes no link");
    check(!check_material_graph_link(graph, "uv", "nothing", "sum", "a").empty(), "a missing output takes no link");
    check(check_material_graph_link(graph, "uv", "u", "sum", "a").empty(), "any two value types link");
}

void test_used_nodes_and_lines()
{
    std::printf("test_used_nodes_and_lines\n");
    auto graph = make_graph_with_output();
    add_node(graph, "wave", "sine");
    add_node(graph, "time", "time");
    link(graph, "wave", "in", "time");
    add_node(graph, "orphan", "cosine");
    link(graph, "output", "roughness", "wave");
    const auto result = compile_material_graph(graph);
    check(get_info(graph, result, "wave").is_used && get_info(graph, result, "time").is_used, "what feeds the output is used");
    check(!get_info(graph, result, "orphan").is_used, "a node the output never reads is unused");
    check(!contains(result.shader, "n_orphan"), "an unused node is not in the shader");
    check(!contains(result.shader, "n_time"), "a built-in input is written where it is read");
    const auto& wave = get_info(graph, result, "wave");
    std::vector<std::string> lines;
    std::stringstream stream(result.shader);
    for(std::string line; std::getline(stream, line);)
    {
        lines.push_back(line);
    }
    check(wave.first_line > 0 && wave.first_line <= lines.size() && contains(lines[wave.first_line - 1], "// wave: Sine"),
          "a node's first line is its comment");
    check(wave.line_count == 2 && contains(lines[wave.first_line], "float n_wave = sin(i.time);"), "and its statement follows");
}

void test_parameters()
{
    std::printf("test_parameters\n");
    auto graph = make_graph_with_output();
    std::string previous;
    const auto chain = [&](const std::string& id, const std::string& output)
    {
        const std::string read = make_material_graph_link(id, output);
        if(previous.empty())
        {
            previous = read;
            return;
        }
        const std::string sum = "sum_" + id;
        add_node(graph, sum, "add");
        link(graph, sum, "a", previous);
        link(graph, sum, "b", read);
        previous = sum;
    };
    for(int i = 0; i < 5; ++i)
    {
        const std::string id = "scalar_" + std::to_string(i);
        add_node(graph, id, "scalar_parameter");
        chain(id, "");
    }
    for(int i = 0; i < 2; ++i)
    {
        const std::string id = "color_" + std::to_string(i);
        add_node(graph, id, "color_parameter");
        chain(id, "rgb");
    }
    link(graph, "output", "base_color", previous);
    const auto result = compile_material_graph(graph);
    check(!result.has_errors(), "parameters compile" + describe(result));
    check(result.parameters.size() == 7 && result.parameter_registers == 4, "2 vectors and 5 scalars take 4 registers");
    const auto find = [&](const std::string& id) -> const material_graph_parameter*
    {
        for(const auto& parameter : result.parameters)
        {
            if(parameter.node_id == id)
            {
                return &parameter;
            }
        }
        return nullptr;
    };
    check(find("color_0") && find("color_0")->register_index == 0 && find("color_1")->register_index == 1, "vectors come first");
    check(find("scalar_4") && find("scalar_4")->register_index == 3 && find("scalar_4")->component == 0, "scalars share registers four to one");
    check(contains(result.shader, "uniform vec4 u_mg_params[4];"), "the array is declared at its size");
    check(find("scalar_0") && find("scalar_0")->name == "scalar_0", "a parameter without a name takes its node's id");
}

/// A texture handle found by @p key, as a graph file naming it gives it.
auto make_texture_handle(const std::string& key) -> asset_handle<gfx::texture>
{
    const std::string text = R"({"material_graph": {"nodes": [{"id": "map", "type": "texture_sample", "texture_key": ")" + key + R"("}]}})";
    material_graph graph;
    load_from_string(text, graph);
    return graph.nodes.empty() ? asset_handle<gfx::texture>{} : graph.nodes.front().texture;
}

auto find_parameter(const material_graph_compile_result& result, const std::string& name) -> const material_graph_parameter*
{
    const auto it = std::find_if(result.parameters.begin(),
                                 result.parameters.end(),
                                 [&](const material_graph_parameter& parameter)
                                 {
                                     return parameter.name == name;
                                 });
    return it == result.parameters.end() ? nullptr : &*it;
}

auto find_link(const material_graph& graph, const std::string& node_id, const std::string& pin) -> std::string
{
    const int index = find_material_graph_node(graph, node_id);
    const auto* input = index >= 0 ? find_material_graph_input(graph.nodes[size_t(index)], pin) : nullptr;
    return input ? input->link : std::string{};
}

void test_graph_defaults()
{
    std::printf("test_graph_defaults\n");
    auto graph = make_graph_with_output();
    add_node(graph, "gloss", "scalar_parameter");
    add_node(graph, "albedo", "texture_sample");
    link(graph, "output", "roughness", "gloss");
    link(graph, "output", "base_color", "albedo.rgb");
    const auto before = compile_material_graph(graph);
    check(set_material_graph_parameter_default(graph, "gloss", math::vec4(0.7f)), "a parameter's default is set by node");
    check(!set_material_graph_parameter_default(graph, "albedo", math::vec4(0.7f)), "a texture node has no parameter default");
    const auto texture = make_texture_handle("engine:/data/textures/default_color.dds");
    check(set_material_graph_node_texture(graph, "albedo", texture), "a texture node's texture is set by node");
    check(!set_material_graph_node_texture(graph, "gloss", texture), "a parameter reads no texture");
    const auto after = compile_material_graph(graph);
    const auto* gloss = find_parameter(after, "gloss");
    check(gloss && std::abs(gloss->default_value.x - 0.7f) < 0.0001f, "the compile takes the new default");
    check(after.shader == before.shader, "a default and a texture change no code: the shader stays");
    auto& node = graph.nodes[size_t(find_material_graph_node(graph, "gloss"))];
    check(!find_material_parameter_slider(node), "a parameter has no slider until Slider Max is above Slider Min");
    set_material_graph_setting(node, MATERIAL_PARAMETER_SLIDER_MAX_SETTING, "2");
    const auto slider = find_material_parameter_slider(node);
    check(slider && slider->x == 0.0f && slider->y == 2.0f, "Slider Max alone ranges from the default minimum, 0");
    set_material_graph_setting(node, MATERIAL_PARAMETER_SLIDER_MIN_SETTING, "2");
    check(!find_material_parameter_slider(node), "an empty range is no slider");
}

/// The names of the Standard graph's maps, in the order the PBR inspector showed them.
const std::vector<std::string> STANDARD_MAPS = {"Color Map", "Normal Map", "Roughness Map", "Metallic Map", "Emissive Map", "AO Map"};

/// A masked, tiled, two-sided Standard material with a color map, a map of its own for roughness and for metallic,
/// and a normal map, as its graph: every node kind the builder makes.
auto make_mapped_standard_graph() -> material_graph
{
    standard_surface mapped;
    mapped.color_map = make_texture_handle("engine:/data/textures/default_color.dds");
    mapped.roughness_map = make_texture_handle("engine:/data/textures/default_color.dds");
    mapped.metallic_map = make_texture_handle("engine:/data/textures/default_normal.dds");
    mapped.normal_map = make_texture_handle("engine:/data/textures/default_normal.dds");
    mapped.tiling = math::vec2(2.0f, 3.0f);
    mapped.alpha = alpha_mode::mask;
    mapped.alpha_cutoff = 0.4f;
    mapped.camera_fade_distance = 2.0f;
    mapped.cull = cull_type::none;
    return make_standard_material_graph(mapped);
}

/// A Standard material in the specular workflow, with its specular map.
auto make_specular_standard_graph() -> material_graph
{
    standard_surface specular;
    specular.workflow = standard_workflow::specular;
    specular.specular_color = math::color(0.5f, 0.5f, 0.5f, 1.0f);
    specular.glossiness = 0.7f;
    specular.specular_map = make_texture_handle("engine:/data/textures/default_color.dds");
    return make_standard_material_graph(specular);
}

auto find_static_switch_value(const material_graph& graph, const std::string& id) -> std::string
{
    const int index = find_material_graph_node(graph, id);
    const auto* value = index >= 0 ? find_material_graph_setting(graph.nodes[size_t(index)], "value") : nullptr;
    return value ? *value : std::string{};
}

auto get_texture_names(const material_graph_compile_result& result) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(const auto& texture : result.textures)
    {
        names.push_back(texture.name);
    }
    return names;
}

auto find_texture(const material_graph_compile_result& result, const std::string& name) -> const material_graph_texture*
{
    for(const auto& texture : result.textures)
    {
        if(texture.name == name)
        {
            return &texture;
        }
    }
    return nullptr;
}

void test_standard_material()
{
    std::printf("test_standard_material\n");
    const standard_surface plain;
    const auto standard = make_standard_material_graph(plain);
    const auto plain_result = compile_material_graph(standard);
    check(!plain_result.has_errors() && !has_warning(plain_result),
          "the default Standard material compiles cleanly, empty maps without warnings" + describe(plain_result));
    for(const char* name : {"Base Color", "Roughness", "Metallic", "Bumpiness", "Emissive Color", "Emissive Intensity",
                            "Subsurface Color", "Tiling", "Dither Threshold"})
    {
        check(find_parameter(plain_result, name) != nullptr, std::string("its values are parameters: ") + name);
    }
    check(find_parameter(plain_result, "Alpha Cutoff") == nullptr, "an opaque material has no Alpha Cutoff to clip with");
    check(find_parameter(plain_result, "Specular Color") == nullptr, "the metallic workflow has no specular color");
    check(get_texture_names(plain_result) == STANDARD_MAPS, "every map is a named texture, empty or not, in the PBR inspector's order");
    const auto* roughness_map = find_texture(plain_result, "Roughness Map");
    check(roughness_map && !roughness_map->description.empty(), "a map carries its hint");
    check(find_link(standard, "color_map", "uv") == "uv" && find_link(standard, "uv", "tiling") == "tiling.xyzw.xy",
          "the maps are read at the UV the Tiling parameter scales");
    const auto* roughness = find_parameter(plain_result, "Roughness");
    check(roughness && std::abs(roughness->default_value.x - plain.roughness) < 0.0001f, "a parameter's default is the surface's value");
    const auto* base = find_parameter(plain_result, "Base Color");
    check(base && base->default_value == plain.base_color.to_linear().value, "colors are given in linear space");
    check(find_static_switch_value(standard, "packed_metal_roughness") == "false", "two empty maps are not one packed map");
    check(plain_result.static_switches.size() == 1 && plain_result.static_switches.front().name == "Packed Metal Roughness" &&
              !plain_result.static_switches.front().value,
          "the packing is a named Static Switch the inspector shows");
    const auto& roughness_node = standard.nodes[size_t(find_material_graph_node(standard, "roughness"))];
    const auto range = find_material_parameter_slider(roughness_node);
    check(range && range->x == 0.0f && range->y == 1.0f, "Roughness keeps the PBR inspector's slider");
    check(find_link(standard, "output", "camera_fade_distance") == "dither_threshold", "the Dither Threshold fades it near the camera");
    graph_material plain_graph;
    plain_graph.set_graph(standard);
    check(!plain_result.uses_opacity_mask && plain_graph.get_cull_type() != cull_type::none && plain_graph.casts_shadow(),
          "an opaque, culled material draws and casts so");
    check(find_parameter(plain_result, "Base Color")->changes == render_change::albedo &&
              find_parameter(plain_result, "Roughness")->changes == render_change::specular &&
              find_parameter(plain_result, "Emissive Intensity")->changes == render_change::emission &&
              find_texture(plain_result, "Color Map")->changes == render_change::albedo,
          "each value changes only the aspects its channels feed");

    standard_surface packed;
    packed.roughness_map = make_texture_handle("engine:/data/textures/default_color.dds");
    packed.metallic_map = packed.roughness_map;
    const auto packed_graph = make_standard_material_graph(packed);
    const auto packed_result = compile_material_graph(packed_graph);
    check(find_static_switch_value(packed_graph, "packed_metal_roughness") == "true" && packed_result.static_switches.front().value,
          "one map for both is the packed one");
    check(find_link(packed_graph, "packed_roughness_metallic", "a") == "roughness_map.g" &&
              find_link(packed_graph, "packed_roughness_metallic", "b") == "metallic_map.b" &&
              get_texture_names(packed_result) == STANDARD_MAPS,
          "packed, roughness is read from green and metallic from blue, and both maps keep their rows");

    const auto graph = make_mapped_standard_graph();
    const auto result = compile_material_graph(graph);
    check(!result.has_errors() && !has_warning(result), "a mapped, masked, two-sided material compiles cleanly" + describe(result));
    check(find_link(graph, "roughness_metallic", "a") == "roughness_map.r" && find_link(graph, "roughness_metallic", "b") == "metallic_map.r",
          "a map of each reads roughness and metallic from their red");
    check(find_link(graph, "normal_map", "strength") == "bumpiness", "the normal map reads at the material's bumpiness");
    const auto* tiling = find_parameter(result, "Tiling");
    check(tiling && tiling->default_value.x == 2.0f && tiling->default_value.y == 3.0f, "the Tiling parameter holds the surface's");
    check(find_link(graph, "output", "opacity_mask") == "albedo.out.a" && find_link(graph, "output", "opacity_mask_clip") == "alpha_cutoff" &&
              result.uses_opacity_mask,
          "the alpha cutout is the Opacity Mask, clipped at the Alpha Cutoff parameter");
    const auto* cutoff = find_parameter(result, "Alpha Cutoff");
    check(cutoff && std::abs(cutoff->default_value.x - 0.4f) < 0.0001f && cutoff->changes == render_change::coverage,
          "Alpha Cutoff holds the surface's cutoff, and changes coverage");
    check((find_parameter(result, "Base Color")->changes & render_change::coverage) != 0u, "a masked base color's alpha is coverage");
    const auto* fade = find_parameter(result, "Dither Threshold");
    check(fade && fade->default_value.x == 2.0f, "Dither Threshold holds the surface's distance");
    graph_material masked_graph;
    masked_graph.set_graph(graph);
    check(masked_graph.get_cull_type() == cull_type::none, "no culling is Two Sided");
    const size_t two_sided_define = result.shader.find("#define MATERIAL_TWO_SIDED 1");
    check(two_sided_define != std::string::npos && two_sided_define < result.shader.find("material_graph/material_graph.sh"),
          "Two Sided is defined before material_graph.sh, whose back-face normal flip tests it with #if");
    check(contains(result.shader, std::string("uniform vec4 ") + MATERIAL_GRAPH_NORMAL_CHANNELS_UNIFORM) &&
              contains(result.shader, std::string(MATERIAL_GRAPH_NORMAL_CHANNELS_UNIFORM) + "["),
          "a normal map reads its texture's channel layout, set at draw time");

    standard_surface blended;
    blended.alpha = alpha_mode::blend;
    blended.cull = cull_type::clockwise;
    const auto blended_standard = make_standard_material_graph(blended);
    const auto blended_result = compile_material_graph(blended_standard);
    check(blended_result.is_translucent && !blended_result.uses_opacity_mask &&
              contains(blended_result.shader, "#define MATERIAL_TRANSLUCENT 1"),
          "a transparent material is Translucent: dithered by its alpha, with no shadow casters");
    graph_material blended_graph;
    blended_graph.set_graph(blended_standard);
    check(!blended_graph.casts_shadow() && blended_graph.is_translucent(), "and casts no shadow");
    check(blended_graph.get_cull_type() == cull_type::clockwise, "front-face culling is Cull Front Faces");
    standard_surface faded;
    faded.base_color = math::color(1.0f, 1.0f, 1.0f, 0.5f);
    const auto faded_result = compile_material_graph(make_standard_material_graph(faded));
    check(faded_result.uses_opacity_mask && !faded_result.is_translucent && contains(faded_result.shader, "s.opacity_mask_clip = 0.25;"),
          "an opaque material whose base color's alpha is below 1 dithers it out, as the PBR material did");
}

/// Where scripts' Standard properties land: Roughness is Glossiness inverted on a specular material, which has no
/// Metallic or Roughness Map.
void test_standard_property_resolution()
{
    std::printf("test_standard_property_resolution\n");
    const auto* roughness = find_standard_property("roughness");
    const auto* glossiness = find_standard_property("glossiness");
    const auto* metallic_property = find_standard_property("metallic");
    const auto* roughness_map = find_standard_property("roughness_map");
    check(roughness && glossiness && metallic_property && roughness_map, "the table has the properties");
    if(!roughness || !glossiness || !metallic_property || !roughness_map)
    {
        return;
    }
    graph_material metallic;
    metallic.set_graph(make_standard_material_graph({}));
    const auto metallic_roughness = resolve_standard_property(metallic, *roughness);
    check(metallic_roughness.property == roughness && !metallic_roughness.is_inverted, "a metallic material holds Roughness itself");
    standard_surface specular_surface;
    specular_surface.workflow = standard_workflow::specular;
    graph_material specular;
    specular.set_graph(make_standard_material_graph(specular_surface));
    const auto specular_roughness = resolve_standard_property(specular, *roughness);
    check(specular_roughness.property == glossiness && specular_roughness.is_inverted,
          "a specular material holds Roughness as Glossiness, inverted");
    const auto held = make_standard_parameter_value(*glossiness, apply_standard_inversion(specular_roughness, math::vec4(0.3f)));
    check(specular.set_parameter(glossiness->name, held) && std::abs(specular.get_parameter(glossiness->name)->x - 0.7f) < 0.0001f,
          "a roughness of 0.3 is a glossiness of 0.7");
    check(std::abs(apply_standard_inversion(specular_roughness, *specular.get_parameter(glossiness->name)).x - 0.3f) < 0.0001f,
          "and reads back as 0.3");
    check(resolve_standard_property(specular, *metallic_property).property == nullptr &&
              resolve_standard_property(specular, *roughness_map).property == nullptr,
          "it holds no Metallic and no Roughness Map");
}

void test_specular_workflow()
{
    std::printf("test_specular_workflow\n");
    const auto graph = make_specular_standard_graph();
    const auto result = compile_material_graph(graph);
    check(!result.has_errors() && !has_warning(result), "a specular Standard material compiles cleanly" + describe(result));
    const size_t define = result.shader.find("#define MATERIAL_SPECULAR_WORKFLOW 1");
    check(define != std::string::npos && define < result.shader.find("material_graph/material_graph.sh"),
          "the workflow is defined before the includes");
    check(contains(result.shader, "s.specular = ") && contains(result.shader, "s.glossiness = ") &&
              !contains(result.shader, "s.metallic = ") && !contains(result.shader, "s.roughness = "),
          "the shader writes specular and glossiness, not the other workflow's channels");
    check(find_parameter(result, "Specular Color") && find_parameter(result, "Glossiness") && !find_parameter(result, "Roughness"),
          "its values are the specular workflow's");
    check(find_parameter(result, "Specular Color")->register_index >= material::MAX_INSTANCE_CONSTANTS &&
              find_parameter(result, "Glossiness")->register_index < material::MAX_INSTANCE_CONSTANTS,
          "the specular color is the material's, so the scalars keep a per-instance register");
    check(find_texture(result, "Specular Map") && !find_texture(result, "Roughness Map"), "the specular map replaces roughness and metallic");
    check((find_parameter(result, "Base Color")->changes & render_change::specular) != 0u, "the diffuse color takes part in the specular");
    auto metallic = make_graph_with_output();
    add_node(metallic, "rough", "scalar_parameter");
    link(metallic, "output", "roughness", "rough");
    auto& output = metallic.nodes[size_t(find_material_graph_output(metallic))];
    set_material_graph_setting(output, MATERIAL_WORKFLOW_SETTING, MATERIAL_WORKFLOW_SPECULAR);
    const auto unused = compile_material_graph(metallic);
    check(!get_info(metallic, unused, "rough").is_used && unused.parameters.empty(),
          "a link into the other workflow's channel is not in the shader");
    check(is_material_graph_specular_workflow(metallic) && get_material_graph_workflow_label(metallic) == "Specular" &&
              !is_material_graph_specular_workflow(make_graph_with_output()) &&
              get_material_graph_workflow_label(make_graph_with_output()) == "Metallic",
          "the workflow is named as the Surface Output's setting shows it");
    const auto* workflow = find_material_setting_def(*find_material_node_def(MATERIAL_GRAPH_OUTPUT_TYPE), MATERIAL_WORKFLOW_SETTING);
    check(get_material_setting_choice_label(*workflow, MATERIAL_WORKFLOW_METALLIC) == "Metallic" &&
              get_material_setting_choice_label(*workflow, "unknown") == "unknown",
          "a choice shows its label, a value outside the choices itself");
    const auto dielectric = convert_specular_to_metallic(math::vec3(0.8f, 0.2f, 0.2f), math::vec3(0.02f), 0.6f);
    check(dielectric.metallic == 0.0f && std::abs(dielectric.roughness - 0.4f) < 0.0001f,
          "a specular below a dielectric's is a dielectric, rough by 1 - glossiness");
    const auto metal = convert_specular_to_metallic(math::vec3(0.0f), math::vec3(0.95f, 0.64f, 0.54f), 1.0f);
    check(metal.metallic > 0.99f && std::abs(metal.base_color.x - 0.95f) < 0.01f && metal.roughness == 0.0f,
          "a black diffuse under a bright specular is a metal of the specular's color");
}

void test_graph_naming_and_estimates()
{
    std::printf("test_graph_naming_and_estimates\n");
    auto graph = make_graph_with_output();
    add_node(graph, "tex", "texture_sample");
    set_material_graph_setting(graph.nodes.back(), MATERIAL_PARAMETER_NAME_SETTING, "Detail Map");
    set_material_graph_setting(graph.nodes.back(), MATERIAL_PARAMETER_DESCRIPTION_SETTING, "Fine detail.");
    add_node(graph, "glow", "color_parameter");
    set_material_graph_setting(graph.nodes.back(), MATERIAL_PARAMETER_NAME_SETTING, "Glow");
    set_value(graph, "glow", "value", math::vec4(0.5f, 0.25f, 1.0f, 1.0f));
    add_node(graph, "strength", "scalar_parameter");
    set_material_graph_setting(graph.nodes.back(), MATERIAL_PARAMETER_PER_INSTANCE_SETTING, MATERIAL_PARAMETER_PER_INSTANCE_OFF);
    set_value(graph, "strength", "value", math::vec4(4.0f));
    add_node(graph, "tint", "multiply");
    link(graph, "tint", "a", "tex.rgb");
    link(graph, "tint", "b", "glow.rgb");
    add_node(graph, "lit", "multiply");
    link(graph, "lit", "a", "tint");
    link(graph, "lit", "b", "strength");
    link(graph, "output", "emissive", "lit");
    add_node(graph, "tone", "vector_parameter");
    link(graph, "output", "base_color", "tone.xyz");
    const auto result = compile_material_graph(graph);
    check(!result.has_errors(), "a named texture compiles" + describe(result));
    const auto* texture = find_texture(result, "Detail Map");
    check(texture && texture->description == "Fine detail.", "a texture node is set by its Parameter Name, and carries its Description");
    const auto* strength = find_parameter(result, "strength");
    const auto* tone = find_parameter(result, "tone");
    check(strength && tone && strength->register_index > tone->register_index,
          "a parameter that is not per instance takes a register after those that are");
    check(result.parameters.size() == 3 && result.parameters[0].name == "Glow" && result.parameters[1].name == "strength",
          "the parameters list in the graph's order");
    check(result.emissive.is_known && result.emissive.parameters.size() == 2, "the emissive chain of Multiply nodes is estimated");
    graph_material mat;
    mat.set_graph(graph);
    const auto emissive = mat.get_emissive_estimate();
    check(std::abs(emissive.x - 2.0f) < 0.0001f && std::abs(emissive.z - 4.0f) < 0.0001f,
          "the estimate is the product of the parameters' values, a texture reading white");
    mat.set_parameter("strength", math::vec4(1.0f));
    check(std::abs(mat.get_emissive_estimate().z - 1.0f) < 0.0001f, "and follows a value set by name");
    auto noisy = make_graph_with_output();
    add_node(noisy, "holes", "noise");
    link(noisy, "output", "emissive", "holes");
    check(!compile_material_graph(noisy).emissive.is_known, "an emissive that is not such a chain is not estimated");
}

/// A PBR material's .mat as an older version saved it: values packed into surface_data, the alpha test apart.
constexpr const char* LEGACY_PBR_SOURCE = R"({"material": {"polymorphic_id": 2147483649, "polymorphic_name": "pbr_material",
    "ptr_wrapper": {"id": 2147483649, "data": {
        "base_type": {"cull_type": 0},
        "base_color": {"r": 1.0, "g": 0.5, "b": 0.25, "a": 1.0},
        "emissive_intensity": 2.0,
        "surface_data": {"x": 0.6, "y": 0.2, "z": 1.5, "w": 0.4},
        "tiling": {"x": 2.0, "y": 2.0},
        "dither_threshold": {"x": 0.5, "y": 3.0}}}}})";

void test_legacy_pbr_material()
{
    std::printf("test_legacy_pbr_material\n");
    const fs::path file = fs::temp_directory_path() / "unravel_legacy_pbr_material.mat";
    {
        std::ofstream out(file, std::ios::binary);
        out << LEGACY_PBR_SOURCE;
    }
    std::shared_ptr<material> loaded;
    load_from_file(file.string(), loaded);
    const auto* graph = loaded ? loaded->safe_cast<graph_material>() : nullptr;
    check(graph != nullptr, "a PBR material's file loads as a graph material");
    if(graph == nullptr)
    {
        return;
    }
    check(std::abs(graph->get_parameter("Roughness")->x - 0.6f) < 0.0001f &&
              std::abs(graph->get_parameter("Metallic")->x - 0.2f) < 0.0001f &&
              std::abs(graph->get_parameter("Bumpiness")->x - 1.5f) < 0.0001f,
          "its packed values become the Standard graph's parameters");
    check(graph->get_parameter("Base Color") == math::color(1.0f, 0.5f, 0.25f, 1.0f).to_linear().value, "its colors, in linear space");
    check(graph->uses_opacity_mask() && std::abs(graph->get_parameter("Alpha Cutoff")->x - 0.4f) < 0.0001f,
          "a cutoff other than the opaque one, saved before the alpha mode was, makes it masked");
    check(graph->get_parameter("Dither Threshold")->x == 3.0f, "the dither threshold keeps its distance");
    check(graph->get_cull_type() == cull_type::none, "no culling stays Two Sided");
    fs::error_code ec;
    fs::remove(file, ec);
}

void test_conditions_and_discard()
{
    std::printf("test_conditions_and_discard\n");
    auto graph = make_graph_with_output();
    add_node(graph, "clock", "time");
    add_node(graph, "half", "constant");
    set_value(graph, "half", "value", math::vec4(0.5f));
    add_node(graph, "early", "compare");
    set_material_graph_setting(graph.nodes.back(), "operation", "<");
    link(graph, "early", "a", "clock");
    link(graph, "early", "b", "half");
    add_node(graph, "red", "vector3");
    set_value(graph, "red", "value", math::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    add_node(graph, "pick", "branch");
    link(graph, "pick", "condition", "early");
    link(graph, "pick", "true", "red");
    add_node(graph, "split_at", "if");
    link(graph, "split_at", "a", "clock");
    link(graph, "split_at", "b", "half");
    link(graph, "split_at", "greater", "pick");
    link(graph, "output", "base_color", "split_at");
    add_node(graph, "holes", "noise");
    add_node(graph, "cut", "discard");
    link(graph, "cut", "value", "holes");
    auto result = compile_material_graph(graph);
    check(!result.has_errors(), "If, Compare, Branch and Discard compile" + describe(result));
    check(get_info(graph, result, "pick").type == material_value_type::float3 &&
              get_info(graph, result, "split_at").type == material_value_type::float3,
          "Branch and If take the widest input they choose between");
    check(contains(result.shader, "((i.time < 0.5) ? 1.0 : 0.0)"), "Compare writes its operation between A and B");
    check(get_info(graph, result, "cut").is_used && contains(result.shader, "mg_discard_if(n_holes < 0.5);"),
          "a Discard node acts with no link: its test is in the shader");
    check(result.uses_opacity_mask && !result.is_opacity_mask_animated, "a Discard node masks the material: shadow casters clip too");
    link(graph, "cut", "threshold", "clock");
    result = compile_material_graph(graph);
    check(result.is_opacity_mask_animated, "a Discard node reading the time changes its shadow every frame");
    material_graph_compile_options preview;
    preview.is_preview = true;
    check(!contains(compile_material_graph(graph, preview).shader, "mg_discard_if"), "a node's swatch never discards");
    auto offset = make_graph_with_output();
    add_node(offset, "cut", "discard");
    set_value(offset, "output", "world_position_offset", math::vec4(0.0f, 0.1f, 0.0f, 0.0f));
    result = compile_material_graph(offset);
    check(!contains(result.vertex_shader, "mg_discard_if"), "the vertex stage never discards");
}

void test_graph_upgrade()
{
    std::printf("test_graph_upgrade\n");
    const std::string text = R"({"material_graph": {"nodes": [
        {"id": "output", "type": "surface_output", "inputs": [{"pin": "opacity_mask", "value": [0.2]}],
         "settings": [{"name": "opacity_mask_clip", "value": "0.3"}]}
    ]}})";
    material_graph graph;
    check(load_from_string(text, graph), "a graph naming Opacity Mask Clip as a setting loads");
    const auto& output = graph.nodes.front();
    const auto* clip = find_material_graph_input(output, MATERIAL_OPACITY_MASK_CLIP_PIN);
    check(clip && std::abs(clip->value.x - 0.3f) < 0.0001f && find_material_graph_setting(output, MATERIAL_OPACITY_MASK_CLIP_PIN) == nullptr,
          "the setting becomes the input's value");
    check(contains(compile_material_graph(graph).shader, "s.opacity_mask_clip = 0.3;"), "the shader clips at it");
}

/// The renderers a graph shader must compile for: this platform's, and the web's, which deploys cook.
auto get_tested_renderers() -> std::set<std::string>
{
    const auto& platform = gfx::get_renderer_platform_supported_filename_extensions();
    std::set<std::string> renderers(platform.begin(), platform.end());
    renderers.insert(gfx::get_renderer_filename_extension(bgfx::RendererType::WebGPU));
    renderers.insert(gfx::get_renderer_filename_extension(bgfx::RendererType::OpenGLES));
    return renderers;
}

auto read_text(const fs::path& file) -> std::string
{
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_text(const fs::path& file, const std::string& text)
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

/**
 * @brief A graph with one node of every type, each read by a chain of Adds that ends in the emissive channel. Each
 * node type with a dynamic input is there twice: with nothing linked (its float form) and with a Vector 3 linked
 * into its first input.
 */
auto make_graph_of_every_node() -> material_graph
{
    auto graph = make_graph_with_output();
    add_node(graph, "position", "world_position");
    std::string previous = "position";
    const auto add_read = [&](const std::string& id)
    {
        const std::string sum = make_unique_material_graph_node_id(graph, "sum_" + id);
        add_node(graph, sum, "add");
        link(graph, sum, "a", previous);
        link(graph, sum, "b", id);
        previous = sum;
    };
    for(const auto& def : get_material_node_library())
    {
        // Functions have tests of their own: a call needs a function.
        const bool is_function_node = def.code == material_node_code::function_input || def.code == material_node_code::function_output ||
                                      def.code == material_node_code::function_call;
        if(def.code == material_node_code::surface_output || is_function_node)
        {
            continue;
        }
        const std::string id = make_unique_material_graph_node_id(graph, def.type);
        auto& node = add_node(graph, id, def.type);
        // A usage reads the declaration the library lists before it.
        if(def.code == material_node_code::named_reroute)
        {
            set_material_graph_setting(node, "name", "named_reroute_declaration");
        }
        // A Discard node has no output to read: it acts as it is. A texture or a surface is read through a node
        // taking it: a sample, Break Material Attributes.
        if(!def.outputs.empty())
        {
            const auto output_type = def.outputs.front().type;
            const char* consumer_type = output_type == material_value_type::texture2d      ? "texture_sample"
                                        : output_type == material_value_type::texture_cube ? "sample_cubemap"
                                        : output_type == material_value_type::attributes   ? "break_material_attributes"
                                                                                           : nullptr;
            if(consumer_type == nullptr)
            {
                add_read(id);
            }
            else
            {
                const std::string consumer = make_unique_material_graph_node_id(graph, id + "_reader");
                add_node(graph, consumer, consumer_type);
                link(graph, consumer, output_type == material_value_type::attributes ? "attributes" : MATERIAL_TEXTURE_PIN, id);
                add_read(consumer);
            }
        }
        const auto first_dynamic = std::find_if(def.inputs.begin(),
                                                def.inputs.end(),
                                                [](const material_pin_def& pin)
                                                {
                                                    return pin.type == material_value_type::dynamic && !pin.is_hidden;
                                                });
        // A Custom node's default code reads a float.
        if(first_dynamic == def.inputs.end() || def.code == material_node_code::custom)
        {
            continue;
        }
        const std::string vector_id = make_unique_material_graph_node_id(graph, def.type + "_vector");
        add_node(graph, vector_id, def.type);
        link(graph, vector_id, first_dynamic->name, "position");
        add_read(vector_id);
    }
    link(graph, "output", "emissive", previous);
    link(graph, "output", "normal", "normal_map");
    return graph;
}

/// Random's code follows the width of its seed; a pixel-only node cannot feed the offset; a Color Ramp blends its
/// stops in the order of their positions; Math Constant and Wave name their choices.
void test_node_code_choices()
{
    std::printf("test_node_code_choices\n");
    auto graph = make_graph_with_output();
    add_node(graph, "seed", "world_position");
    add_node(graph, "random_vector", "random");
    link(graph, "random_vector", "seed", "seed");
    add_node(graph, "random_float", "random");
    add_node(graph, "sum", "add");
    link(graph, "sum", "a", "random_vector");
    link(graph, "sum", "b", "random_float");
    link(graph, "output", "roughness", "sum");
    const auto result = compile_material_graph(graph);
    check(!result.has_errors() && contains(result.shader, "mg_hash13(") && contains(result.shader, "mg_hash11("),
          "Random hashes its seed by the seed's width" + describe(result));

    auto offset = make_graph_with_output();
    add_node(offset, "slope", "ddx");
    add_node(offset, "position", "world_position");
    link(offset, "slope", "in", "position");
    link(offset, "output", "world_position_offset", "slope");
    const auto derived = compile_material_graph(offset);
    check(has_diagnostic(derived, material_graph_severity::error, "slope") && !derived.uses_world_position_offset &&
              !contains(derived.vertex_shader, "dFdx"),
          "a pixel-only node cannot feed World Position Offset: an error, and nothing moves");

    auto ramp = make_graph_with_output();
    auto& ramp_node = add_node(ramp, "ramp", "color_ramp");
    set_material_graph_setting(ramp_node, MATERIAL_COLOR_RAMP_COUNT_SETTING, "3");
    set_value(ramp, "ramp", get_material_color_ramp_position_pin(2), math::vec4(0.5f));
    set_value(ramp, "ramp", get_material_color_ramp_color_pin(2), math::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    link(ramp, "output", "base_color", "ramp.rgb");
    const auto ramped = compile_material_graph(ramp);
    const auto red = ramped.shader.find("vec4(1.0, 0.0, 0.0, 1.0)");
    const auto white = ramped.shader.find("vec4_splat(1.0)");
    check(!ramped.has_errors() && red != std::string::npos && white != std::string::npos && red < white,
          "a Color Ramp blends its stops in the order of their positions, the red one at 0.5 before the white one at 1" +
              describe(ramped));
    set_material_graph_setting(ramp.nodes[size_t(find_material_graph_node(ramp, "ramp"))], MATERIAL_COLOR_RAMP_INTERPOLATION_SETTING, "constant");
    check(contains(compile_material_graph(ramp).shader, "step(0.5, "), "Constant interpolation steps at each stop");

    auto shapes = make_graph_with_output();
    auto& constant = add_node(shapes, "pi", "math_constant");
    set_material_graph_setting(constant, "constant", "tau");
    auto& wave = add_node(shapes, "wave", "wave");
    set_material_graph_setting(wave, "shape", "triangle");
    add_node(shapes, "both", "multiply");
    link(shapes, "both", "a", "pi");
    link(shapes, "both", "b", "wave");
    link(shapes, "output", "metallic", "both");
    const auto shaped = compile_material_graph(shapes);
    check(!shaped.has_errors() && contains(shaped.shader, "mg_const_tau") && contains(shaped.shader, "mg_wave_triangle(i.time"),
          "Math Constant and Wave put their choices into the code" + describe(shaped));
}

/// Object inputs, Transform and Vertex Interpolators give a graph a vertex stage of its own.
void test_vertex_stage_outputs()
{
    std::printf("test_vertex_stage_outputs\n");
    auto object = make_graph_with_output();
    add_node(object, "pivot", "object_position");
    add_node(object, "tint", "object_random");
    add_node(object, "both", "multiply");
    link(object, "both", "a", "pivot");
    link(object, "both", "b", "tint");
    link(object, "output", "base_color", "both");
    const auto read = compile_material_graph(object);
    check(!read.has_errors() && read.reads_object_data && !read.uses_world_position_offset && !read.computes_vertex_values(),
          "a graph reading the object's pivot reads object data and computes nothing per vertex" + describe(read));
    check(contains(read.shader, "#define MATERIAL_OBJECT_DATA 1") && contains(read.shader, "flat vec4 v_object0") &&
              contains(read.vertex_shader, "flat vec4 v_object2 = vec4(0.0, 0.0, 1.0, 0.0)") &&
              contains(read.vertex_shader, "INSTANCED INSTANCE_MATERIAL\n"),
          "its pixels read the object's transform from varyings its own vertex shader writes, which has the instanced material "
          "variants so instances keep sharing the draw");

    auto transform = make_graph_with_output();
    auto& to_object = add_node(transform, "local", "transform");
    set_material_graph_setting(to_object, "from", "world");
    set_material_graph_setting(to_object, "to", "object");
    set_material_graph_setting(to_object, "kind", "position");
    add_node(transform, "position", "world_position");
    link(transform, "local", "in", "position");
    link(transform, "output", "emissive", "local");
    const auto local = compile_material_graph(transform);
    check(!local.has_errors() && local.reads_object_data && contains(local.shader, "mg_world_to_object_position(i, "),
          "Transform into object space reads the object's transform" + describe(local));
    auto& tangent = transform.nodes[size_t(find_material_graph_node(transform, "local"))];
    set_material_graph_setting(tangent, "to", "tangent");
    check(!compile_material_graph(transform).reads_object_data, "Transform between world and tangent space reads no object data");

    auto interpolated = make_graph_with_output();
    add_node(interpolated, "clock", "time");
    add_node(interpolated, "wave", "sine");
    link(interpolated, "wave", "in", "clock");
    add_node(interpolated, "per_vertex", "vertex_interpolator");
    link(interpolated, "per_vertex", "in", "wave");
    link(interpolated, "output", "metallic", "per_vertex");
    add_node(interpolated, "mask", "vertex_interpolator");
    add_node(interpolated, "uv", "texcoord");
    link(interpolated, "mask", "in", "uv");
    link(interpolated, "output", "opacity_mask", "mask.out.x");
    const auto vertex = compile_material_graph(interpolated);
    check(!vertex.has_errors() && vertex.interpolator_count == 2 && vertex.computes_vertex_values() && !vertex.uses_world_position_offset,
          "two Vertex Interpolators take two varyings, and the vertex shader computes them" + describe(vertex));
    check(contains(vertex.shader, "#define MATERIAL_INTERPOLATORS 2") && contains(vertex.shader, "i.interpolator0") &&
              !contains(vertex.shader, "sin(i.time)") && contains(vertex.vertex_shader, "\ts.interpolator0 = vec4(") &&
              contains(vertex.vertex_shader, "sin(i.time)"),
          "the pixels read the varyings, the vertex shader computes what feeds them");
    check(vertex.is_opacity_mask_animated == false && vertex.uses_opacity_mask, "a mask read through an interpolator of the UV holds still");
    link(interpolated, "output", "opacity_mask", "per_vertex");
    check(compile_material_graph(interpolated).is_opacity_mask_animated, "a mask read through an interpolator of the time animates");
    auto pixel_only = make_graph_with_output();
    add_node(pixel_only, "slope", "fwidth");
    add_node(pixel_only, "per_vertex", "vertex_interpolator");
    link(pixel_only, "per_vertex", "in", "slope");
    link(pixel_only, "output", "metallic", "per_vertex");
    check(has_diagnostic(compile_material_graph(pixel_only), material_graph_severity::error, "slope"),
          "a pixel-only node cannot feed a Vertex Interpolator");
}

/// Texture Objects feed texture nodes, Custom nodes and material functions; cube textures read along a direction.
void test_texture_objects()
{
    std::printf("test_texture_objects\n");
    auto graph = make_graph_with_output();
    add_node(graph, "albedo", "texture_object");
    add_node(graph, "read", "texture_sample");
    link(graph, "read", MATERIAL_TEXTURE_PIN, "albedo");
    add_node(graph, "bumps", "texture_object");
    add_node(graph, "normal", "normal_map");
    link(graph, "normal", MATERIAL_TEXTURE_PIN, "bumps");
    link(graph, "output", "base_color", "read.rgb");
    link(graph, "output", "normal", "normal");
    const auto result = compile_material_graph(graph);
    check(!result.has_errors() && result.textures.size() == 2 && result.textures[0].node_id == "albedo" &&
              !result.textures[0].is_normal_map && result.textures[1].is_normal_map,
          "the objects hold the samplers, the nodes reading them none; the one a Normal Map reads binds as a normal map" +
              describe(result));
    check(contains(result.shader, "mg_texture2D(" + result.textures[0].sampler + ", i.uv0"),
          "the sample reads the object's sampler");

    auto custom = make_graph_with_output();
    add_node(custom, "albedo", "texture_object");
    auto& code = add_node(custom, "code", "custom");
    set_material_graph_setting(code, "code", "return mg_texture2D(a, i.uv0, i.mip_bias).rgb;");
    link(custom, "code", "a", "albedo");
    link(custom, "output", "base_color", "code");
    const auto customized = compile_material_graph(custom);
    check(!customized.has_errors() && contains(customized.shader, "sampler2D a"), "a Custom node takes a texture as a sampler2D" + describe(customized));

    auto wrong = make_graph_with_output();
    add_node(wrong, "albedo", "texture_object");
    add_node(wrong, "sum", "add");
    link(wrong, "sum", "a", "albedo");
    link(wrong, "output", "metallic", "sum");
    check(has_diagnostic(compile_material_graph(wrong), material_graph_severity::error, "sum", "a"), "a texture never goes into a number");

    material_graph function;
    auto& input = add_node(function, "tex", MATERIAL_FUNCTION_INPUT_TYPE);
    set_material_graph_setting(input, "type", "sampler2D");
    add_node(function, "read", "texture_sample");
    link(function, "read", MATERIAL_TEXTURE_PIN, "tex");
    add_node(function, "result", MATERIAL_FUNCTION_OUTPUT_TYPE);
    link(function, "result", "in", "read.rgb");
    auto caller = make_graph_with_output();
    add_node(caller, "albedo", "texture_object");
    auto& call = add_node(caller, "call", MATERIAL_FUNCTION_CALL_TYPE);
    set_material_graph_setting(call, MATERIAL_FUNCTION_KEY_SETTING, "app:/data/functions/read.mgraph");
    link(caller, "call", get_material_function_input_pin(0), "albedo");
    link(caller, "output", "base_color", "call." + get_material_function_output_pin(0));
    material_graph_compile_options options;
    const auto shared_function = std::make_shared<const material_graph>(function);
    options.resolve_function = [shared_function](const std::string&)
    {
        return shared_function;
    };
    const auto called = compile_material_graph(caller, options);
    check(!called.has_errors() && called.textures.size() == 1 && called.textures[0].node_id == "albedo" &&
              contains(called.shader, "mg_texture2D(" + called.textures[0].sampler),
          "a material function reads the texture its caller links in" + describe(called));
    caller.nodes[size_t(find_material_graph_node(caller, "call"))].inputs.clear();
    const auto own = compile_material_graph(caller, options);
    check(!own.has_errors() && own.textures.size() == 1 && own.textures[0].node_id != "albedo",
          "unlinked, the function's texture input reads its own texture" + describe(own));

    auto cube = make_graph_with_output();
    add_node(cube, "sky", "cubemap_object");
    add_node(cube, "mirror", "sample_cubemap");
    link(cube, "mirror", MATERIAL_TEXTURE_PIN, "sky");
    link(cube, "output", "emissive", "mirror.rgb");
    const auto reflected = compile_material_graph(cube);
    check(!reflected.has_errors() && reflected.textures.size() == 1 && reflected.textures[0].kind == material_value_type::texture_cube &&
              contains(reflected.shader, "SAMPLERCUBE(") && contains(reflected.shader, "mg_textureCube("),
          "a cube texture is a cube sampler read along a direction" + describe(reflected));

    auto mip = make_graph_with_output();
    auto& level = add_node(mip, "read", "texture_sample");
    set_material_graph_setting(level, MATERIAL_TEXTURE_MIP_MODE_SETTING, "level");
    set_value(mip, "read", "mip", math::vec4(2.0f));
    link(mip, "output", "base_color", "read.rgb");
    check(contains(compile_material_graph(mip).shader, "mg_texture2D_level("), "Mip Level reads the given mip");
    auto addressed = mip;
    set_material_graph_setting(addressed.nodes[size_t(find_material_graph_node(addressed, "read"))], MATERIAL_TEXTURE_ADDRESS_SETTING, "clamp");
    set_material_graph_setting(addressed.nodes[size_t(find_material_graph_node(addressed, "read"))], MATERIAL_TEXTURE_FILTER_SETTING, "point");
    const auto clamped = compile_material_graph(addressed);
    check(has_same_material_graph_code(mip, addressed) && !clamped.textures.empty() &&
              clamped.textures[0].address == material_texture_address::clamp && clamped.textures[0].filter == material_texture_filter::point,
          "a sampler state is bound, not compiled: the code stays");
}

/// Material Attributes blend whole surfaces; the Surface Output takes them, channels linked besides replace theirs.
void test_material_attributes()
{
    std::printf("test_material_attributes\n");
    auto graph = make_graph_with_output();
    add_node(graph, "dry", "make_material_attributes");
    set_value(graph, "dry", "base_color", math::vec4(0.6f, 0.4f, 0.2f, 1.0f));
    add_node(graph, "wet", "make_material_attributes");
    set_value(graph, "wet", "roughness", math::vec4(0.05f));
    add_node(graph, "blend", "blend_material_attributes");
    link(graph, "blend", "a", "dry");
    link(graph, "blend", "b", "wet");
    link(graph, "output", MATERIAL_ATTRIBUTES_PIN, "blend");
    add_node(graph, "glow", "color");
    link(graph, "output", "emissive", "glow.rgb");
    const auto result = compile_material_graph(graph);
    check(!result.has_errors() && contains(result.shader, "MaterialSurface n_blend = mg_blend_attributes(n_dry, n_wet") &&
              contains(result.shader, "\ts = n_blend;") && contains(result.shader, "\ts.emissive = ") &&
              !contains(result.shader, "\ts.base_color = ") && contains(result.shader, "\ts.opacity_mask_clip = "),
          "the surface starts from the attributes, the channels linked besides replace theirs, the clip stays the output's" +
              describe(result));
    check(!result.uses_opacity_mask, "attributes without a mask leave the material opaque");
    set_value(graph, "wet", "opacity_mask", math::vec4(0.3f));
    check(compile_material_graph(graph).uses_opacity_mask, "attributes with a mask below 1 make the material masked");
    add_node(graph, "split", "break_material_attributes");
    link(graph, "split", "attributes", "dry");
    link(graph, "output", "metallic", "split.metallic");
    check(contains(compile_material_graph(graph).shader, "n_split.metallic"), "Break Material Attributes reads a channel");
}

/// Quality Switch: the compile option picks the branch, as a Static Switch's value does.
void test_quality_switch()
{
    std::printf("test_quality_switch\n");
    auto graph = make_graph_with_output();
    add_node(graph, "rich", "noise_3d");
    add_node(graph, "cheap", "constant");
    set_value(graph, "cheap", "value", math::vec4(0.25f));
    add_node(graph, "quality", "quality_switch");
    link(graph, "quality", "high", "rich");
    link(graph, "quality", "low", "cheap");
    link(graph, "output", "roughness", "quality");
    const auto high = compile_material_graph(graph);
    material_graph_compile_options options;
    options.quality = material_graph_quality::low;
    const auto low = compile_material_graph(graph, options);
    check(!high.has_errors() && contains(high.shader, "mg_noise_value3") && !low.has_errors() && !contains(low.shader, "mg_noise_value3(") &&
              contains(low.shader, "s.roughness = 0.25;"),
          "High compiles the noise, Low the constant alone" + describe(low));
}

/// A graph of every node a vertex can run, its sum the World Position Offset: the vertex stage compiles each.
auto make_vertex_graph_of_every_node() -> material_graph
{
    auto graph = make_graph_of_every_node();
    std::vector<std::string> removed;
    for(const auto& node : graph.nodes)
    {
        const auto* def = find_material_node_def(node.type);
        if(def != nullptr && (def->is_pixel_only || def->code == material_node_code::normal_map || def->code == material_node_code::discard))
        {
            removed.push_back(node.id);
        }
    }
    for(const auto& id : removed)
    {
        remove_material_graph_node(graph, id);
    }
    auto& output = graph.nodes[size_t(find_material_graph_output(graph))];
    // The Normal Map's link went with it: its entry would read a zero normal.
    std::erase_if(output.inputs,
                  [](const material_graph_input& input)
                  {
                      return input.link.empty();
                  });
    for(auto& input : output.inputs)
    {
        if(input.pin == "emissive")
        {
            input.pin = "world_position_offset";
        }
    }
    return graph;
}

auto is_shaderc_present() -> bool
{
    fs::error_code ec;
    return fs::exists(fs::resolve_protocol(std::string("binary:/shaderc") + fs::executable_extension()), ec);
}

/// The variants of a generated shader: default, card_capture and instance_material, and the four shadow casters of a
/// graph that clips or moves its vertices; a preview shader has one; the vertex shader of a graph with a vertex stage
/// has twelve G-buffer variants, eight velocity ones and three that write the position alone.
constexpr size_t PLAIN_VARIANT_COUNT = 3;
constexpr size_t MASKED_VARIANT_COUNT = 7;
constexpr size_t PREVIEW_VARIANT_COUNT = 1;
constexpr size_t VERTEX_VARIANT_COUNT = 23;

/// A variant WebGL 2 leaves without code: it reads the skinning palettes from storage buffers, which need OpenGL ES
/// 3.1, and its *_textures twin draws there instead (compile_eshader_variants).
auto is_storage_buffer_variant(const std::string& name) -> bool
{
    return name.find("skinned") != std::string::npos && !name.ends_with("_textures");
}

/// Compiles the shader text @p text, named @p file_name, with shaderc for each of @p renderers; each compile must give
/// @p variant_count variants with code, but the storage buffer variants of WebGL 2.
void check_text_compiles(asset_manager& am,
                         const std::string& name,
                         const std::string& file_name,
                         const std::string& text,
                         size_t variant_count,
                         const std::set<std::string>& renderers,
                         const fs::path& output_folder)
{
    const std::string key = std::string("app:/") + SHADER_FOLDER + "/" + file_name;
    write_text(fs::resolve_protocol("app:/") / SHADER_FOLDER / file_name, text);
    for(const auto& extension : renderers)
    {
        const auto output = output_folder / (file_name + ".asset" + extension);
        const bool compiled = ac::compile<gfx::shader>(am, key, output);
        std::vector<compiled_shader_variant> variants;
        if(compiled)
        {
            load_from_file_bin(output.string(), variants);
        }
        const bool is_webgl2 = extension == gfx::get_renderer_filename_extension(bgfx::RendererType::OpenGLES);
        const bool has_code = std::all_of(variants.begin(),
                                          variants.end(),
                                          [is_webgl2](const compiled_shader_variant& variant)
                                          {
                                              return !variant.bytes.empty() || (is_webgl2 && is_storage_buffer_variant(variant.name));
                                          });
        check(compiled && variants.size() == variant_count && has_code,
              name + " compiles with shaderc on " + extension + ", every variant");
    }
}

/// Compiles a generated shader, and its vertex shader if it has one, with shaderc for every tested renderer; a preview
/// for the renderers of this platform, as the editor compiles it for its own.
void check_shader_compiles(asset_manager& am,
                           const std::string& name,
                           const material_graph_compile_result& result,
                           const fs::path& output_folder)
{
    const bool is_preview = contains(result.shader, "evaluate_preview");
    const bool has_casters = result.uses_opacity_mask || result.uses_world_position_offset;
    const size_t variant_count = is_preview ? PREVIEW_VARIANT_COUNT : (has_casters ? MASKED_VARIANT_COUNT : PLAIN_VARIANT_COUNT);
    const std::string file_name = sanitize_material_graph_identifier(name) + ".eshader";
    const auto& platform = gfx::get_renderer_platform_supported_filename_extensions();
    const auto renderers = is_preview ? std::set<std::string>(platform.begin(), platform.end()) : get_tested_renderers();
    check_text_compiles(am, name, "fs_" + file_name, result.shader, variant_count, renderers, output_folder);
    if(!result.vertex_shader.empty())
    {
        check_text_compiles(am, name + "'s vertex shader", "vs_" + file_name, result.vertex_shader, VERTEX_VARIANT_COUNT, renderers, output_folder);
    }
}

/// A graph lifting its vertices along their normal by a wave of the time and its Height parameter, with a texture and
/// a Custom node of its own feeding the offset, and others feeding the base color alone.
auto make_wave_offset_graph() -> material_graph
{
    auto graph = make_graph_with_output();
    add_node(graph, "clock", "time");
    add_node(graph, "wave", "sine");
    link(graph, "wave", "in", "clock");
    add_node(graph, "height", "scalar_parameter");
    set_material_graph_setting(graph.nodes.back(), "name", "Height");
    set_value(graph, "height", "value", math::vec4(0.2f));
    add_node(graph, "normal", "vertex_normal");
    add_node(graph, "bumps", "texture_sample");
    add_node(graph, "shape", "custom");
    set_material_graph_setting(graph.nodes.back(), "output", "float");
    set_material_graph_setting(graph.nodes.back(), "code", "return a * 0.5 + 0.5;");
    link(graph, "shape", "a", "wave");
    add_node(graph, "lift", "multiply");
    link(graph, "lift", "a", "shape");
    link(graph, "lift", "b", "bumps.r");
    add_node(graph, "scaled", "multiply");
    link(graph, "scaled", "a", "lift");
    link(graph, "scaled", "b", "height");
    // The UV widened to the normal's width: a warning on the node both stages read.
    add_node(graph, "uv", "texcoord");
    add_node(graph, "spread", "multiply");
    link(graph, "spread", "a", "normal");
    link(graph, "spread", "b", "uv");
    add_node(graph, "offset", "multiply");
    link(graph, "offset", "a", "spread");
    link(graph, "offset", "b", "scaled");
    link(graph, "output", "world_position_offset", "offset");
    add_node(graph, "tint", "custom");
    set_material_graph_setting(graph.nodes.back(), "code", "return vec3(1.0, 0.5, dFdx(i.uv0.x));");
    link(graph, "output", "base_color", "tint");
    link(graph, "output", "emissive", "spread");
    set_material_graph_setting(graph.nodes[size_t(find_material_graph_output(graph))], "max_world_position_offset", "0.4");
    return graph;
}

/// The reroutes of @p graph, named ones too: they show their input's swatch, not one of their own.
auto count_reroutes(const material_graph& graph) -> size_t
{
    return size_t(std::count_if(graph.nodes.begin(),
                                graph.nodes.end(),
                                [](const material_graph_node& node)
                                {
                                    const auto* def = find_material_node_def(node.type);
                                    return def != nullptr && is_pass_through(*def);
                                }));
}

void test_examples(asset_manager* am, const fs::path& output_folder)
{
    std::printf("test_examples\n");
    const fs::path folder = fs::path(EDITOR_DATA_DIR) / EXAMPLES_FOLDER;
    size_t count = 0;
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(folder, ec))
    {
        if(entry.path().extension() != GRAPH_EXTENSION)
        {
            continue;
        }
        ++count;
        const std::string name = entry.path().stem().string();
        material_graph graph;
        check(load_from_string(read_text(entry.path()), graph), name + " loads");
        material_graph_compile_options options;
        options.source_name = name;
        const auto result = compile_material_graph(graph, options);
        check(!result.has_errors(), name + " compiles without errors" + describe(result));
        const bool every_node_used = std::all_of(result.nodes.begin(),
                                                 result.nodes.end(),
                                                 [](const material_graph_node_info& info)
                                                 {
                                                     return info.is_used;
                                                 });
        check(every_node_used, name + " uses every node it has");
        material_graph_compile_options preview_options;
        preview_options.source_name = name;
        preview_options.is_preview = true;
        const auto preview = compile_material_graph(graph, preview_options);
        check(!preview.has_errors() && contains(preview.shader, "vec4 evaluate_preview(MaterialInputs i, float node)"),
              name + " has a preview shader" + describe(preview));
        if(am)
        {
            check_shader_compiles(*am, name, result, output_folder);
            check_shader_compiles(*am, name + "_preview", preview, output_folder);
        }
    }
    check(count >= 5, "the examples are found");
    const auto every_node = make_graph_of_every_node();
    const auto result = compile_material_graph(every_node);
    check(!result.has_errors(), "a graph of every node type compiles without errors" + describe(result));
    material_graph_compile_options preview_options;
    preview_options.is_preview = true;
    const auto preview = compile_material_graph(every_node, preview_options);
    size_t previewed = 0;
    for(size_t i = 0; i < every_node.nodes.size(); ++i)
    {
        previewed += has_material_graph_preview(every_node, preview, i) &&
                     contains(preview.shader, "if(abs(node - " + std::to_string(i) + ".0) < 0.5) return")
                         ? 1
                         : 0;
    }
    const auto has_no_output = [](const material_graph_node& node)
    {
        const auto* def = find_material_node_def(node.type);
        return def != nullptr && def->outputs.empty();
    };
    const auto without_value = size_t(std::count_if(every_node.nodes.begin(), every_node.nodes.end(), has_no_output));
    check(previewed + without_value >= every_node.nodes.size() - count_reroutes(every_node),
          "every node with a value has its swatch: the output and Discard have none");
    if(am)
    {
        check_shader_compiles(*am, "every_node", result, output_folder);
        check_shader_compiles(*am, "every_node_preview", preview, output_folder);
        const auto vertex_every_node = compile_material_graph(make_vertex_graph_of_every_node());
        check(!vertex_every_node.has_errors() && vertex_every_node.uses_world_position_offset,
              "a graph of every node a vertex runs moves the vertices without errors" + describe(vertex_every_node));
        check_shader_compiles(*am, "vertex_every_node", vertex_every_node, output_folder);
        material_graph_compile_options low;
        low.quality = material_graph_quality::low;
        check_shader_compiles(*am, "every_node_low", compile_material_graph(every_node, low), output_folder);
        check_shader_compiles(*am, "wave_offset", compile_material_graph(make_wave_offset_graph()), output_folder);
        check_shader_compiles(*am, "standard_mapped", compile_material_graph(make_mapped_standard_graph()), output_folder);
        check_shader_compiles(*am, "standard_specular", compile_material_graph(make_specular_standard_graph()), output_folder);
        standard_surface blended;
        blended.alpha = alpha_mode::blend;
        check_shader_compiles(*am, "standard_translucent", compile_material_graph(make_standard_material_graph(blended)), output_folder);
    }
}

/// A graph reading @p count nodes of @p type through a chain of Adds into the base color; the nodes are
/// "<prefix>0", "<prefix>1"... in the graph's order.
auto make_summed_graph(const std::string& type, const std::string& output, uint32_t count, const std::string& prefix)
    -> material_graph
{
    auto graph = make_graph_with_output();
    std::string previous;
    for(uint32_t i = 0; i < count; ++i)
    {
        const std::string id = prefix + std::to_string(i);
        add_node(graph, id, type);
        const std::string read = make_material_graph_link(id, output);
        if(previous.empty())
        {
            previous = read;
            continue;
        }
        const std::string sum = "sum_" + id;
        add_node(graph, sum, "add");
        link(graph, sum, "a", previous);
        link(graph, sum, "b", read);
        previous = sum;
    }
    link(graph, "output", "base_color", previous);
    return graph;
}

void test_binding_limits()
{
    std::printf("test_binding_limits\n");
    const auto textures = compile_material_graph(make_summed_graph("texture_sample", "rgb", MATERIAL_GRAPH_MAX_TEXTURES + 1, "tex_"));
    check(textures.textures.size() == MATERIAL_GRAPH_MAX_TEXTURES, "a material binds at most the most textures");
    check(has_diagnostic(textures, material_graph_severity::error, "tex_" + std::to_string(MATERIAL_GRAPH_MAX_TEXTURES)),
          "the texture past the limit is an error");
    bool is_bound_by_index = true;
    std::set<uint32_t> stages;
    for(uint32_t i = 0; i < uint32_t(textures.textures.size()); ++i)
    {
        const auto& texture = textures.textures[i];
        is_bound_by_index = is_bound_by_index && texture.sampler == get_material_graph_sampler_name(i) &&
                            texture.stage == get_material_graph_texture_stage(i);
        stages.insert(texture.stage);
    }
    check(is_bound_by_index, "each texture's sampler and stage follow its index");
    check(stages.size() == textures.textures.size() && stages.count(SKINNING_PALETTE_STAGE) == 0 &&
              *stages.rbegin() < 16,
          "the stages are distinct, within bgfx's 16 and clear of the skinning palette's");
    check(get_material_graph_texture_stage(SKINNING_PALETTE_STAGE - 1) == SKINNING_PALETTE_STAGE - 1 &&
              get_material_graph_texture_stage(SKINNING_PALETTE_STAGE) == SKINNING_PALETTE_STAGE + 1,
          "the stage after the palette's takes over from it");
    check(contains(textures.shader, "SAMPLER2D(s_mg_texture7, 8);"), "the shader declares the samplers at their stages");
    const auto parameters = compile_material_graph(
        make_summed_graph("vector_parameter", "xyzw", MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS + 1, "vector_"));
    check(parameters.parameter_registers == MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS, "the registers stop at the limit");
    check(parameters.parameters.size() == MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS, "only the parameters within it are bound");
    check(has_diagnostic(parameters,
                         material_graph_severity::error,
                         "vector_" + std::to_string(MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS)),
          "the parameter past the limit is an error");
}

/// A Vector Parameter into the base color and two Scalar Parameters into roughness and metallic.
auto make_binding_graph() -> material_graph
{
    auto graph = make_graph_with_output();
    add_node(graph, "tint", "vector_parameter");
    set_value(graph, "tint", "value", math::vec4(0.1f, 0.2f, 0.3f, 0.4f));
    add_node(graph, "shine", "scalar_parameter");
    set_value(graph, "shine", "value", math::vec4(0.75f));
    add_node(graph, "metal", "scalar_parameter");
    set_value(graph, "metal", "value", math::vec4(0.25f));
    link(graph, "output", "base_color", "tint.xyz");
    link(graph, "output", "roughness", "shine");
    link(graph, "output", "metallic", "metal");
    return graph;
}

/// A compiled material binds what the compile of its code laid out: loading one compiles nothing, and a material cooked
/// at Low loads with the Low binding, which a compile at High could not give. A copy (an instance) takes it too.
void test_compiled_quality_binding()
{
    std::printf("test_compiled_quality_binding\n");
    auto graph = make_binding_graph();
    add_node(graph, "detail", "scalar_parameter");
    set_value(graph, "detail", "value", math::vec4(0.5f));
    add_node(graph, "grain", "texture_sample");
    add_node(graph, "quality", "quality_switch");
    link(graph, "quality", "high", "grain.r");
    link(graph, "quality", "low", "detail");
    link(graph, "output", "ambient_occlusion", "quality");
    material_graph_compile_options low_options;
    low_options.quality = material_graph_quality::low;
    const auto low = compile_material_graph(graph, low_options);
    const auto high = compile_material_graph(graph);
    check(!low.has_errors() && !high.has_errors() && high.textures.size() == 1 && low.textures.empty() &&
              low.parameters.size() == high.parameters.size() + 1,
          "the Low branch reads a parameter and no texture, the High branch a texture" + describe(low));

    auto cooked = std::make_shared<graph_material>();
    cooked->set_graph(graph);
    cooked->set_compiled_code("low code", make_material_graph_binding(low));
    check(cooked->find_parameter("detail") != nullptr && cooked->get_textures().empty(), "the cooked material binds the Low layout");
    const fs::path file = fs::temp_directory_path() / "unravel_compiled_quality_binding.mat";
    std::shared_ptr<material> saved = cooked;
    save_to_file_bin(file.string(), saved);
    std::shared_ptr<material> loaded;
    load_from_file_bin(file.string(), loaded);
    fs::error_code ec;
    fs::remove(file, ec);
    const auto loaded_graph = safe_pointer_cast<graph_material>(loaded);
    check(loaded_graph && loaded_graph->get_shader_hash() == "low code" && loaded_graph->get_graph() == graph,
          "the compiled file keeps the graph and the hash of the code it was cooked with");
    check(loaded_graph && loaded_graph->find_parameter("detail") != nullptr && loaded_graph->get_textures().empty() &&
              loaded_graph->get_parameter_registers() == make_material_graph_parameter_registers(low),
          "loading it binds the Low layout it was saved with, not a compile of the graph at High");

    graph_material instance;
    instance.set_graph(make_graph_with_output());
    if(loaded_graph)
    {
        copy_material_values(*loaded_graph, instance);
    }
    check(instance.get_shader_hash() == "low code" && instance.find_parameter("detail") != nullptr && instance.get_textures().empty(),
          "a copy of it binds the same Low layout");
    check(instance.set_parameter("detail", math::vec4(0.9f)) && !instance.set_parameter("missing", math::vec4(0.0f)),
          "so values set by name reach the Low code's parameters");
}

void test_opacity_mask_shadows()
{
    std::printf("test_opacity_mask_shadows\n");
    const auto plain = compile_material_graph(make_binding_graph());
    check(!plain.uses_opacity_mask && !contains(plain.shader, "SHADOW_CASTER"), "a graph that does not clip has no shadow casters");
    auto textured = make_binding_graph();
    add_node(textured, "leaf", "texture_sample");
    link(textured, "output", "opacity_mask", "leaf.a");
    const auto masked = compile_material_graph(textured);
    check(masked.uses_opacity_mask && !masked.is_opacity_mask_animated, "a texture's alpha clips, and holds still");
    check(contains(masked.shader, "shadow_vsm_linear   SHADOW_CASTER PACK_VSM LINEAR") &&
              contains(masked.shader, "#if SHADOW_CASTER\n#include \"material_graph/fs_material_graph_shadow.sh\""),
          "a graph that clips has the four shadow casters, and their main");
    auto constant = make_graph_with_output();
    set_value(constant, "output", "opacity_mask", math::vec4(0.2f));
    check(compile_material_graph(constant).uses_opacity_mask, "a constant mask below one clips too");
    auto timed = textured;
    add_node(timed, "clock", "time");
    add_node(timed, "wave", "sine");
    link(timed, "wave", "in", "clock");
    add_node(timed, "fade", "multiply");
    link(timed, "fade", "a", "leaf.a");
    link(timed, "fade", "b", "wave");
    link(timed, "output", "opacity_mask", "fade");
    check(compile_material_graph(timed).is_opacity_mask_animated, "a mask that reads the time changes by itself");
    auto rim = make_graph_with_output();
    add_node(rim, "rim", "fresnel");
    link(rim, "output", "opacity_mask", "rim");
    check(compile_material_graph(rim).is_opacity_mask_animated, "so does one that reads the view");
    auto timed_color = textured;
    add_node(timed_color, "clock", "time");
    link(timed_color, "output", "emissive", "clock");
    check(!compile_material_graph(timed_color).is_opacity_mask_animated, "time read only by other channels leaves the mask still");
    graph_material mat;
    mat.set_graph(timed);
    check(mat.uses_opacity_mask() && mat.is_opacity_mask_animated(), "a graph material keeps both");
}

void test_world_position_offset()
{
    std::printf("test_world_position_offset\n");
    const auto plain = compile_material_graph(make_binding_graph());
    check(!plain.uses_world_position_offset && plain.vertex_shader.empty(), "a graph without an offset has no vertex shader");
    const auto graph = make_wave_offset_graph();
    const auto result = compile_material_graph(graph);
    check(!result.has_errors() && result.uses_world_position_offset && result.is_world_position_offset_animated,
          "an offset reading the time moves the vertices, and animates" + describe(result));
    check(contains(result.vertex_shader, "void evaluate_vertex(MaterialInputs i, inout MaterialVertex s)") &&
              contains(result.vertex_shader, "\ts.world_position_offset = ") &&
              contains(result.vertex_shader, "#include \"material_graph/vs_material_graph.sh\"") &&
              contains(result.vertex_shader, "velocity_skinned_baked_instanced_textures"),
          "its vertex shader computes the offset, with the G-buffer and velocity variants");
    check(!contains(result.shader, "world_position_offset") && contains(result.shader, "shadow_vsm_linear   SHADOW_CASTER PACK_VSM LINEAR"),
          "the fragment shader has nothing of it, and the shadow casters, as the shadow moves too");
    check(contains(result.vertex_shader, "mg_texture2D(") && contains(result.vertex_shader, "mg_custom_n_shape") &&
              !contains(result.vertex_shader, "dFdx") && !contains(result.vertex_shader, "mg_custom_n_tint"),
          "the vertex shader reads the texture and the Custom function the offset reads, and no other");
    check(contains(result.shader, "n_spread") && contains(result.vertex_shader, "n_spread") && !contains(result.shader, "n_lift"),
          "a node feeding both stages is computed in each, one feeding the offset alone in the vertex shader alone");
    const auto warnings = std::count_if(result.diagnostics.begin(),
                                        result.diagnostics.end(),
                                        [](const material_graph_diagnostic& diagnostic)
                                        {
                                            return diagnostic.node_id == "spread";
                                        });
    check(warnings == 1, "a node's problem is told once though both stages read it" + describe(result));
    check(std::abs(result.max_world_position_offset - 0.4f) < 0.0001f, "Max Offset is what the bounds grow by");
    auto still = make_graph_with_output();
    set_value(still, "output", "world_position_offset", math::vec4(0.0f, 0.0f, 0.5f, 0.0f));
    set_material_graph_setting(still.nodes[size_t(find_material_graph_output(still))], "max_world_position_offset", "-2");
    const auto lifted = compile_material_graph(still);
    check(lifted.uses_world_position_offset && !lifted.is_world_position_offset_animated && lifted.max_world_position_offset == 0.0f,
          "a constant offset moves the vertices and holds still; Max Offset is never below zero");
    auto flat = make_graph_with_output();
    add_node(flat, "bump", "normal_map");
    link(flat, "output", "world_position_offset", "bump");
    const auto bumped = compile_material_graph(flat);
    check(has_diagnostic(bumped, material_graph_severity::error, "bump") && !bumped.uses_world_position_offset && bumped.vertex_shader.empty(),
          "a Normal Map cannot feed the offset: an error, and nothing moves");
    auto screen = make_graph_with_output();
    add_node(screen, "spot", "screen_position");
    add_node(screen, "rise", "append");
    link(screen, "rise", "a", "spot");
    link(screen, "output", "world_position_offset", "rise");
    const auto screened = compile_material_graph(screen);
    check(has_diagnostic(screened, material_graph_severity::warning, "spot") && screened.uses_world_position_offset,
          "a Screen Position in the offset reads the middle of the view, with a warning");
    graph_material mat;
    mat.set_graph(graph);
    check(mat.uses_world_position_offset() && mat.is_world_position_offset_animated() && mat.draws_own_shadow() &&
              mat.is_shadow_animated() && std::abs(mat.get_max_world_position_offset() - 0.4f) < 0.0001f,
          "a graph material keeps it all, and draws its own shadow");
    check(graph_material::has_offset_bounds() && graph_material::has_animated_shadows(),
          "it counts among the materials growing bounds, and those whose shadow animates");
    auto copy = std::make_unique<graph_material>(mat);
    mat.set_graph(make_binding_graph());
    check(graph_material::has_offset_bounds() && copy->uses_world_position_offset(), "and so does a copy, once the original moves nothing");
}

void test_graph_material_binding()
{
    std::printf("test_graph_material_binding\n");
    graph_material mat;
    check(find_material_graph_output(mat.get_graph()) >= 0, "a new graph material has a surface output");
    const auto revision = mat.get_revision();
    mat.set_graph(make_binding_graph());
    check(mat.get_revision() != revision, "a new graph counts as an edit of the material");
    const auto& registers = mat.get_parameter_registers();
    check(registers.size() == 2, "a vector and two scalars take two registers");
    check(registers.size() == 2 && registers[0] == math::vec4(0.1f, 0.2f, 0.3f, 0.4f), "the vector's default fills its register");
    check(registers.size() == 2 && registers[1].x == 0.75f && registers[1].y == 0.25f,
          "the scalars' defaults share the next register in the graph's order");
    check(mat.get_textures().empty(), "a graph without texture nodes binds none");
    check(mat.get_cull_type() == cull_type::counter_clockwise, "a graph material draws one side by default");
    auto two_sided = make_binding_graph();
    set_material_graph_setting(two_sided.nodes[size_t(find_material_graph_output(two_sided))], "two_sided", "on");
    mat.set_graph(two_sided);
    check(mat.get_cull_type() == cull_type::none, "Two Sided draws both faces");
}

void test_flow_nodes()
{
    std::printf("test_flow_nodes\n");
    auto graph = make_graph_with_output();
    add_node(graph, "red", "vector3");
    set_value(graph, "red", "value", math::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    add_node(graph, "blue", "vector3");
    set_value(graph, "blue", "value", math::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    add_node(graph, "pick", "static_switch");
    link(graph, "pick", "true", "red");
    link(graph, "pick", "false", "blue");
    link(graph, "output", "base_color", "pick");
    auto result = compile_material_graph(graph);
    const int blue = find_material_graph_node(graph, "blue");
    check(!result.has_errors() && contains(result.shader, "s.base_color = vec3(1.0, 0.0, 0.0);"), "a Static Switch passes True on by default");
    check(!result.nodes[size_t(blue)].is_used, "its other branch is not in the code");
    set_material_graph_setting(graph.nodes[size_t(find_material_graph_node(graph, "pick"))], "value", "false");
    result = compile_material_graph(graph);
    check(contains(result.shader, "s.base_color = vec3(0.0, 0.0, 1.0);"), "and False when Value says so");
    auto reroutes = make_graph_with_output();
    add_node(reroutes, "tint", "vector3");
    set_value(reroutes, "tint", "value", math::vec4(0.25f, 0.5f, 0.75f, 0.0f));
    add_node(reroutes, "declare", "named_reroute_declaration");
    set_material_graph_setting(reroutes.nodes.back(), "name", "Tint");
    link(reroutes, "declare", "in", "tint");
    add_node(reroutes, "use", "named_reroute");
    set_material_graph_setting(reroutes.nodes.back(), "name", "Tint");
    link(reroutes, "output", "base_color", "use");
    result = compile_material_graph(reroutes);
    check(!result.has_errors() && contains(result.shader, "s.base_color = vec3(0.25, 0.5, 0.75);"), "a Named Reroute Usage reads its declaration");
    check(!check_material_graph_link(reroutes, "tint", "xyz", "use", "in").empty(), "a usage's hidden input takes no link");
    set_material_graph_setting(reroutes.nodes.back(), "name", "Nothing");
    check(has_diagnostic(compile_material_graph(reroutes), material_graph_severity::error, "use"), "a usage naming no declaration is an error");
    set_material_graph_setting(reroutes.nodes.back(), "name", "Tint");
    add_node(reroutes, "declare_again", "named_reroute_declaration");
    set_material_graph_setting(reroutes.nodes.back(), "name", "Tint");
    check(has_diagnostic(compile_material_graph(reroutes), material_graph_severity::error, "declare_again"), "two declarations of one name are an error");
    auto custom = make_graph_with_output();
    add_node(custom, "uv", "texcoord");
    add_node(custom, "wave", "custom");
    set_material_graph_setting(custom.nodes.back(), "output", "float");
    set_material_graph_setting(custom.nodes.back(), "code", "float w = sin(a.x * 10.0 + i.time);\nreturn w * 0.5 + 0.5;");
    link(custom, "wave", "a", "uv.uv");
    link(custom, "output", "opacity_mask", "wave");
    result = compile_material_graph(custom);
    check(!result.has_errors() && contains(result.shader, "float mg_custom_") && contains(result.shader, "(MaterialInputs i, vec2 a, float b, float c, float d)") &&
              contains(result.shader, "\tfloat w = sin(a.x * 10.0 + i.time);"),
          "a Custom node is a function of its inputs, each as wide as its link, returning its Output type");
    check(result.uses_opacity_mask && result.is_opacity_mask_animated, "reading the time in it makes a mask animated");
    auto triplanar = make_graph_with_output();
    add_node(triplanar, "rock", "triplanar_sample");
    link(triplanar, "output", "base_color", "rock.rgb");
    result = compile_material_graph(triplanar);
    check(result.textures.size() == 1 && contains(result.shader, "mg_triplanar(s_mg_texture0, i.world_position * 1.0, i.vertex_normal, 4.0, i.mip_bias)"),
          "a Triplanar Sample reads a texture of the material along the world axes");
}

/// A function scaling its Value input by its own Gain parameter, with a Named Reroute inside.
auto make_gain_function() -> material_graph
{
    material_graph function;
    add_node(function, "value", "function_input");
    set_material_graph_setting(function.nodes.back(), "name", "Value");
    set_material_graph_setting(function.nodes.back(), "type", "vec3");
    set_value(function, "value", "default", math::vec4(0.5f));
    add_node(function, "gain", "scalar_parameter");
    set_material_graph_setting(function.nodes.back(), "name", "Gain");
    set_value(function, "gain", "value", math::vec4(2.0f));
    add_node(function, "scaled", "multiply");
    link(function, "scaled", "a", "value");
    link(function, "scaled", "b", "gain");
    add_node(function, "keep", "named_reroute_declaration");
    set_material_graph_setting(function.nodes.back(), "name", "Scaled");
    link(function, "keep", "in", "scaled");
    add_node(function, "use", "named_reroute");
    set_material_graph_setting(function.nodes.back(), "name", "Scaled");
    add_node(function, "result", "function_output");
    set_material_graph_setting(function.nodes.back(), "name", "Result");
    link(function, "result", "in", "use");
    return function;
}

void test_material_functions()
{
    std::printf("test_material_functions\n");
    const auto gain = std::make_shared<material_graph>(make_gain_function());
    material_graph_compile_options options;
    options.resolve_function = [&gain](const std::string& key) -> std::shared_ptr<const material_graph>
    {
        return key == "app:/data/gain.mgraph" ? gain : nullptr;
    };
    check(is_material_function_graph(*gain) && get_material_function_interface(*gain).inputs.size() == 1,
          "a graph of Function Inputs and Outputs is a function with its pins");
    auto graph = make_graph_with_output();
    add_node(graph, "red", "vector3");
    set_value(graph, "red", "value", math::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    add_node(graph, "first", "material_function");
    set_material_graph_setting(graph.nodes.back(), "function", "app:/data/gain.mgraph");
    link(graph, "first", "in0", "red");
    add_node(graph, "second", "material_function");
    set_material_graph_setting(graph.nodes.back(), "function", "app:/data/gain.mgraph");
    set_value(graph, "second", "in0", math::vec4(0.25f));
    link(graph, "output", "base_color", "first.out0");
    link(graph, "output", "emissive", "second.out0");
    auto result = compile_material_graph(graph, options);
    check(!result.has_errors(), "two calls of a function compile" + describe(result));
    check(result.functions.size() == 1 && result.functions.front() == "app:/data/gain.mgraph", "the result names the function once");
    check(result.parameters.size() == 1 && result.parameters.front().name == "Gain", "the calls share the parameter of one name");
    check(contains(result.shader, "vec3(1.0, 0.0, 0.0)") && contains(result.shader, "vec3_splat(0.25)"),
          "a linked pin feeds the function's input, a value its default");
    check(result.nodes.size() == graph.nodes.size() && result.nodes[size_t(find_material_graph_node(graph, "first"))].type == material_value_type::float3,
          "the result describes the graph's own nodes; a call is as wide as its first output");
    auto missing = graph;
    set_material_graph_setting(missing.nodes[size_t(find_material_graph_node(missing, "second"))], "function", "app:/data/none.mgraph");
    check(has_diagnostic(compile_material_graph(missing, options), material_graph_severity::error, "second"), "a missing function is an error on its call");
    auto broken = std::make_shared<material_graph>(make_gain_function());
    link(*broken, "scaled", "b", "nothing");
    material_graph_compile_options broken_options;
    broken_options.resolve_function = [&broken](const std::string&) -> std::shared_ptr<const material_graph>
    {
        return broken;
    };
    const auto broken_result = compile_material_graph(graph, broken_options);
    const bool names_inner = std::any_of(broken_result.diagnostics.begin(),
                                         broken_result.diagnostics.end(),
                                         [](const material_graph_diagnostic& diagnostic)
                                         {
                                             return diagnostic.node_id == "first" && contains(diagnostic.message, "scaled");
                                         });
    check(names_inner, "a problem inside a function is told on the call, naming the function's node");
    auto loop = std::make_shared<material_graph>(make_gain_function());
    add_node(*loop, "again", "material_function");
    set_material_graph_setting(loop->nodes.back(), "function", "app:/data/loop.mgraph");
    material_graph_compile_options loop_options;
    loop_options.resolve_function = [&loop](const std::string&) -> std::shared_ptr<const material_graph>
    {
        return loop;
    };
    auto calls_loop = make_graph_with_output();
    add_node(calls_loop, "outer", "material_function");
    set_material_graph_setting(calls_loop.nodes.back(), "function", "app:/data/loop.mgraph");
    link(calls_loop, "output", "base_color", "outer.out0");
    check(has_diagnostic(compile_material_graph(calls_loop, loop_options), material_graph_severity::error, "outer"), "a function calling itself is an error");
    auto nesting = std::make_shared<material_graph>();
    add_node(*nesting, "value", "function_input");
    add_node(*nesting, "inner", "material_function");
    set_material_graph_setting(nesting->nodes.back(), "function", "app:/data/gain.mgraph");
    link(*nesting, "inner", "in0", "value");
    add_node(*nesting, "result", "function_output");
    link(*nesting, "result", "in", "inner.out0");
    material_graph_compile_options nested_options;
    nested_options.resolve_function = [&](const std::string& key) -> std::shared_ptr<const material_graph>
    {
        return key == "app:/data/nest.mgraph" ? nesting : gain;
    };
    auto nested = make_graph_with_output();
    add_node(nested, "outer", "material_function");
    set_material_graph_setting(nested.nodes.back(), "function", "app:/data/nest.mgraph");
    link(nested, "output", "base_color", "outer.out0");
    result = compile_material_graph(nested, nested_options);
    check(!result.has_errors() && result.functions.size() == 2, "a function calls a function" + describe(result));
    const auto alone = compile_material_graph(*gain, options);
    check(!alone.has_errors() && contains(alone.shader, "s.base_color = "), "a function compiled alone shows its first output");
    // Copies in the graph material: the file at first, the copy once it is gone.
    const fs::path folder = fs::resolve_protocol("app:/data/material_graph_tests");
    fs::error_code ec;
    fs::create_directories(folder, ec);
    const std::string key = "app:/data/material_graph_tests/gain.mgraph";
    save_to_file(fs::resolve_protocol(key).string(), *gain);
    auto calls = make_graph_with_output();
    add_node(calls, "call", "material_function");
    set_material_graph_setting(calls.nodes.back(), "function", key);
    link(calls, "output", "base_color", "call.out0");
    graph_material mat;
    mat.set_graph(calls);
    check(mat.copy_functions() && mat.get_functions().count(key) == 1 && mat.find_parameter("Gain"), "a graph material copies the functions it calls");
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("material", mat));
    }
    fs::remove(fs::resolve_protocol(key), ec);
    graph_material loaded;
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("material", loaded));
    }
    check(loaded.get_functions().count(key) == 1 && loaded.find_parameter("Gain") != nullptr,
          "the compiled material keeps the copies, and binds with them once the file is gone");
}

void test_graph_material_values()
{
    std::printf("test_graph_material_values\n");
    graph_material mat;
    mat.set_graph(make_binding_graph());
    check(mat.get_parameters().size() == 3 && mat.find_parameter("shine") != nullptr, "the parameters are listed by name");
    check(!mat.set_parameter("nothing", math::vec4(1.0f)), "a name the graph lacks is refused");
    const auto revision = mat.get_instance_values_revision();
    check(mat.set_parameter("shine", math::vec4(0.5f, 9.0f, 9.0f, 9.0f)), "a scalar parameter takes a value");
    check(mat.get_parameter_registers()[1].x == 0.5f && mat.get_parameter_registers()[1].y == 0.25f,
          "a scalar writes its own component alone");
    check(mat.get_instance_values_revision() != revision, "a parameter value counts as a per-instance edit");
    check(mat.set_parameter("tint", math::vec4(1.0f, 0.0f, 0.0f, 1.0f)) &&
              mat.get_parameter_registers()[0] == math::vec4(1.0f, 0.0f, 0.0f, 1.0f),
          "a vector parameter takes its register");
    check(mat.get_parameter("shine") && mat.get_parameter("shine")->x == 0.5f, "a parameter reads the value it was given");
    check(mat.get_parameter("metal") && mat.get_parameter("metal")->x == 0.25f, "and its default otherwise");
    auto edited = make_binding_graph();
    set_value(edited, "metal", "value", math::vec4(0.6f));
    mat.set_graph(edited);
    check(mat.get_parameter_registers()[1].x == 0.5f && mat.get_parameter_registers()[1].y == 0.6f,
          "a new graph keeps the values given, and takes its new defaults");
    std::array<math::vec4, material::MAX_INSTANCE_CONSTANTS> constants{};
    check(mat.write_instance_constants(constants.data()) == 2 && constants[0] == math::vec4(1.0f, 0.0f, 0.0f, 1.0f),
          "the instance constants are the first registers");
    auto textured = make_binding_graph();
    add_node(textured, "leaf", "texture_sample");
    link(textured, "output", "emissive", "leaf.rgb");
    mat.set_graph(textured);
    asset_handle<gfx::texture> texture;
    texture.set_internal_ids({}, "engine:/data/textures/default_normal.dds");
    check(!mat.set_texture("nothing", texture), "a texture node the graph lacks is refused");
    check(mat.set_texture("leaf", texture) && mat.get_textures().size() == 1 && mat.get_textures()[0].texture == texture,
          "a texture node takes a texture by its id");
    mat.set_graph(textured);
    check(mat.get_texture("leaf") == texture, "and keeps it across a new graph");
    const auto shader = compile_material_graph(make_binding_graph()).shader;
    check(contains(shader, "instance_material   MATERIAL_GRAPH_INSTANCE_MATERIAL") &&
              contains(shader, "#define MG_PARAM1 i.instance_register1") && contains(shader, "#define MG_PARAM1 u_mg_params[1]") &&
              contains(shader, "MG_PARAM1.x"),
          "the generated shader reads the first registers per instance in its instance_material variant");
}

void test_graph_material_copy()
{
    std::printf("test_graph_material_copy\n");
    graph_material source;
    source.set_graph(make_binding_graph());
    source.set_parameter("shine", math::vec4(0.1f));
    asset_handle<gfx::shader> shader;
    shader.set_internal_ids({}, "Graph Material Copy Test");
    source.set_shader(shader);
    source.set_shader_hash("hash");
    graph_material target;
    const auto revision = target.get_revision();
    copy_material_values(source, target);
    check(target.get_graph() == source.get_graph() && target.get_parameter_registers() == source.get_parameter_registers(),
          "the graph and the values given are copied");
    check(target.get_shader() == shader && target.get_shader_hash() == "hash", "so are the shader and its hash");
    check(target.get_revision() != revision, "and count as edits of the copy");
    const auto copied = target.get_revision();
    copy_material_values(source, target);
    check(target.get_revision() == copied, "copying the same values again changes nothing");
    const auto defaults = compile_material_graph(make_binding_graph());
    graph_material plain;
    plain.set_graph(make_binding_graph());
    copy_material_values(plain, target);
    check(target.get_parameter_registers() == make_material_graph_parameter_registers(defaults) &&
              target.get_parameter_values().empty(),
          "a value the source no longer sets goes back to the graph's default on the copy");
}

/// A graph material registered under @p name, replacing an earlier one of that name.
auto make_shared_graph_material(asset_manager& am, const std::string& name, const material_graph& graph)
    -> asset_handle<material>
{
    const std::string key = "engine:/embedded/material_graph_test/" + name;
    am.unload_asset<material>(key);
    auto mat = std::make_shared<graph_material>();
    mat->set_graph(graph);
    return am.get_asset_from_instance<material>(key, mat);
}

template<typename OutputArchive, typename InputArchive>
auto round_trip_instance(const material_instance& saved, const asset_handle<material>& shared, std::ios::openmode mode)
    -> material_instance
{
    std::stringstream stream(std::ios::in | std::ios::out | mode);
    {
        OutputArchive ar(stream);
        try_save(ar, ser20::make_nvp("instance", saved));
    }
    material_instance loaded;
    {
        InputArchive ar(stream);
        try_load(ar, ser20::make_nvp("instance", loaded));
    }
    loaded.set_shared_material(shared);
    return loaded;
}

void test_graph_material_instances(asset_manager& am)
{
    std::printf("test_graph_material_instances\n");
    const auto shared = make_shared_graph_material(am, "instances", make_binding_graph());
    auto shared_graph = std::static_pointer_cast<graph_material>(shared.get());
    material_instance instance;
    instance.set_shared_material(shared);
    check(!instance.set_graph_parameter("nothing", math::vec4(1.0f)), "a parameter the graph lacks is refused");
    check(instance.get_graph_parameter("shine") && instance.get_graph_parameter("shine")->x == 0.75f,
          "a parameter reads the shared material's value");
    check(instance.set_graph_parameter("shine", math::vec4(0.3f)) && instance.has_overrides(), "a parameter is overridden");
    check(instance.shares_draw(), "a parameter in the first registers shares the draw");
    instance.update_resolved();
    auto resolved = safe_pointer_cast<graph_material>(instance.get_resolved());
    check(resolved && resolved->get_parameter_registers()[1].x == 0.3f, "the resolved material has the override");
    check(shared_graph->get_parameter_registers()[1].x == 0.75f, "the shared material keeps its own");
    auto edited = make_binding_graph();
    set_value(edited, "tint", "value", math::vec4(0.9f, 0.8f, 0.7f, 1.0f));
    shared_graph->set_graph(edited);
    asset_handle<gfx::shader> shader;
    shader.set_internal_ids({}, "Graph Material Instance Test");
    shared_graph->set_shader(shader);
    instance.update_resolved();
    check(instance.get_resolved() == resolved, "a shared material edit keeps the resolved material's identity");
    check(resolved && resolved->get_graph() == edited && resolved->get_parameter_registers()[0] == math::vec4(0.9f, 0.8f, 0.7f, 1.0f),
          "the shared material's new graph reaches the resolved material");
    check(resolved && resolved->get_shader() == shader, "and its new shader");
    check(resolved && resolved->get_parameter_registers()[1].x == 0.3f, "the override stays on top");
    const auto json = round_trip_instance<ser20::oarchive_associative_t, ser20::iarchive_associative_t>(instance, shared, {});
    check(json == instance, "the JSON round trip keeps the graph overrides");
    const auto binary = round_trip_instance<ser20::oarchive_binary_t, ser20::iarchive_binary_t>(instance, shared, std::ios::binary);
    check(binary == instance, "the binary round trip keeps them too");
    instance.revert_graph_parameter("shine");
    instance.update_resolved();
    check(!instance.has_overrides() && instance.get_resolved() == nullptr, "no overrides, no resolved material");
    auto textured = make_binding_graph();
    add_node(textured, "leaf", "texture_sample");
    link(textured, "output", "emissive", "leaf.rgb");
    shared_graph->set_graph(textured);
    const auto texture = am.get_asset<gfx::texture>("engine:/data/textures/default_normal.dds", load_flags::standard, load_mode::deferred);
    check(instance.set_graph_texture("leaf", texture) && instance.get_graph_texture("leaf") == texture, "a texture node is overridden");
    check(!instance.shares_draw(), "a texture override gives the slot a draw of its own");
    instance.update_resolved();
    resolved = safe_pointer_cast<graph_material>(instance.get_resolved());
    check(resolved && resolved->get_texture("leaf") == texture && !shared_graph->get_texture("leaf"),
          "the resolved material samples the override, the shared one its own");
    const auto textured_json = round_trip_instance<ser20::oarchive_associative_t, ser20::iarchive_associative_t>(instance, shared, {});
    check(textured_json == instance, "a texture override survives the JSON round trip");
    instance.revert_all();
    auto many = make_graph_with_output();
    std::string sum;
    for(int i = 0; i < 5; ++i)
    {
        const std::string id = "v" + std::to_string(i);
        add_node(many, id, "vector_parameter");
        if(sum.empty())
        {
            sum = make_material_graph_link(id, "xyz");
            continue;
        }
        add_node(many, "sum" + id, "add");
        link(many, "sum" + id, "a", sum);
        link(many, "sum" + id, "b", make_material_graph_link(id, "xyz"));
        sum = make_material_graph_link("sum" + id, "out");
    }
    link(many, "output", "emissive", sum);
    shared_graph->set_graph(many);
    check(instance.set_graph_parameter("v4", math::vec4(1.0f)) && !instance.shares_draw(),
          "a parameter past the first four registers gives the slot a draw of its own");
    instance.revert_all();
    auto masked = make_binding_graph();
    link(masked, "output", "opacity_mask", "shine");
    shared_graph->set_graph(masked);
    check(instance.set_graph_parameter("shine", math::vec4(0.3f)) && instance.shares_draw(),
          "a parameter of a graph whose mask clips keeps the shared draw: its shadow casters draw the resolved material");
    instance.revert_all();
    auto moving = make_binding_graph();
    set_value(moving, "output", "world_position_offset", math::vec4(0.0f, 0.1f, 0.0f, 0.0f));
    shared_graph->set_graph(moving);
    check(instance.set_graph_parameter("shine", math::vec4(0.3f)) && !instance.shares_draw(),
          "a parameter of a graph whose World Position Offset moves the vertices draws on its own: the vertex shader "
          "reads the shared values");
    am.unload_asset<material>(shared.id());
}

/// Registers @p path, a material, as the editor does when it finds the file.
void register_material(asset_manager& am, const fs::path& path)
{
    asset_meta meta;
    meta.uid = generate_uuid();
    meta.type = path.extension().string();
    save_to_file(path.string() + ".meta", meta);
    am.add_asset_info_for_path(path, meta, true);
}

/// Where the test compiles the material at @p path.
auto get_compiled_material_path(const fs::path& path) -> fs::path
{
    return path.parent_path() / "compiled" / (path.filename().string() + ".asset");
}

auto compile_material_file(asset_manager& am, const fs::path& path) -> std::shared_ptr<graph_material>
{
    const fs::path output = get_compiled_material_path(path);
    fs::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    std::shared_ptr<material> compiled;
    if(ac::compile<material>(am, fs::path(path.string() + ".meta"), output))
    {
        load_from_file_bin(output.string(), compiled);
    }
    return safe_pointer_cast<graph_material>(compiled);
}

void save_graph_material(const fs::path& path, const material_graph& graph)
{
    auto mat = std::make_shared<graph_material>();
    mat->set_graph(graph);
    save_to_file(path.string(), std::shared_ptr<material>(mat));
}

/// The files directly in @p folder, by name.
auto list_file_names(const fs::path& folder) -> std::set<std::string>
{
    std::set<std::string> names;
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(folder, ec))
    {
        if(entry.is_regular_file(ec))
        {
            names.insert(entry.path().filename().string());
        }
    }
    return names;
}

void test_graph_material_asset(asset_manager& am, bool can_compile)
{
    std::printf("test_graph_material_asset\n");
    const fs::path folder = fs::resolve_protocol("app:/") / SHADER_FOLDER / "materials";
    fs::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path path = folder / "Glow.mat";
    save_graph_material(path, make_binding_graph());
    const std::string source = read_text(path);
    check(contains(source, "\"polymorphic_name\": \"graph_material\"") && !contains(source, "shader"),
          "a graph material's source holds its graph alone");
    std::shared_ptr<material> loaded;
    load_from_file(path.string(), loaded);
    const auto* loaded_graph = loaded ? loaded->safe_cast<graph_material>() : nullptr;
    check(loaded_graph && save_to_string(loaded_graph->get_graph()) == save_to_string(make_binding_graph()),
          "and reads back as the same graph");
    register_material(am, path);
    const auto compiled = compile_material_file(am, path);
    check(compiled && !compiled->get_shader_hash().empty(), "the compiled material holds the hash of its code");
    check(list_file_names(folder) == std::set<std::string>{"Glow.mat", "Glow.mat.meta"},
          "compiling writes nothing beside the material");
    auto valued = make_binding_graph();
    set_value(valued, "shine", "value", math::vec4(0.1f));
    save_graph_material(path, valued);
    const auto revalued = compile_material_file(am, path);
    check(compiled && revalued && revalued->get_shader_hash() == compiled->get_shader_hash(), "a value edit keeps the hash");
    auto changed = make_binding_graph();
    add_node(changed, "pulse", "sine");
    link(changed, "pulse", "in", "shine");
    link(changed, "output", "emissive", "pulse");
    save_graph_material(path, changed);
    const auto recoded = compile_material_file(am, path);
    check(compiled && recoded && recoded->get_shader_hash() != compiled->get_shader_hash(), "a code edit changes it");
    if(!can_compile)
    {
        return;
    }
    const auto compiled_path = get_compiled_material_path(path).string();
    for(const auto& renderer : get_tested_renderers())
    {
        const fs::path output = get_graph_material_shader_file(compiled_path, renderer);
        check(ac::compile_graph_material_shader(path, output), "its shader compiles for " + renderer);
        compiled_graph_material_shader shader;
        const bool is_read = load_from_file_bin(output.string(), shader);
        const bool has_code = std::all_of(shader.variants.begin(),
                                          shader.variants.end(),
                                          [](const compiled_shader_variant& variant)
                                          {
                                              return !variant.bytes.empty();
                                          });
        check(is_read && recoded && shader.code_hash == recoded->get_shader_hash() && shader.variants.size() == PLAIN_VARIANT_COUNT && has_code,
              "beside the compiled material, marked with its hash, every variant (" + renderer + ")");
        check(ac::is_graph_material_shader_compiled(path, output), "and reads as current (" + renderer + ")");
    }
    const auto first = get_graph_material_shader_file(compiled_path, *gfx::get_renderer_platform_supported_filename_extensions().begin());
    // In a folder of its own: the check above lists this one's files.
    const fs::path twin_folder = folder.parent_path() / "materials_twin";
    fs::create_directories(twin_folder, ec);
    const fs::path twin_path = twin_folder / "GlowTwin.mat";
    auto twin = changed;
    set_value(twin, "shine", "value", math::vec4(0.7f));
    save_graph_material(twin_path, twin);
    const fs::path twin_output = get_graph_material_shader_file(get_compiled_material_path(twin_path).string(), fs::path(first).extension().string());
    fs::create_directories(twin_output.parent_path(), ec);
    compiled_graph_material_shader original_shader;
    compiled_graph_material_shader twin_shader;
    check(ac::compile_graph_material_shader(twin_path, twin_output) && ac::is_graph_material_shader_compiled(twin_path, twin_output) &&
              load_from_file_bin(first, original_shader) && load_from_file_bin(twin_output.string(), twin_shader) &&
              twin_shader.code_hash == original_shader.code_hash && twin_shader.variants.size() == original_shader.variants.size() &&
              std::equal(twin_shader.variants.begin(),
                         twin_shader.variants.end(),
                         original_shader.variants.begin(),
                         [](const compiled_shader_variant& a, const compiled_shader_variant& b)
                         {
                             return a.name == b.name && a.bytes == b.bytes;
                         }),
          "a material of the same code, other values, takes the shader compiled for another");
    auto revalued_changed = changed;
    set_value(revalued_changed, "shine", "value", math::vec4(0.9f));
    save_graph_material(path, revalued_changed);
    check(ac::is_graph_material_shader_compiled(path, first), "a value edit leaves the compiled shaders current");
    save_graph_material(path, make_binding_graph());
    check(!ac::is_graph_material_shader_compiled(path, first), "a code edit does not");
    save_graph_material(path, make_wave_offset_graph());
    const auto waving = compile_material_file(am, path);
    check(ac::compile_graph_material_shader(path, first), "a graph whose offset moves the vertices compiles");
    compiled_graph_material_shader shader;
    check(load_from_file_bin(first, shader) && waving && shader.code_hash == waving->get_shader_hash() &&
              shader.variants.size() == MASKED_VARIANT_COUNT && shader.vertex_variants.size() == VERTEX_VARIANT_COUNT,
          "its vertex shader is in the same file, every variant");
}

auto run_material_graph_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_links_and_ids();
    test_copy_and_paste();
    test_library();
    test_round_trip();
    test_types();
    test_components();
    test_vector_into_float();
    test_textures();
    test_conversions();
    test_errors();
    test_used_nodes_and_lines();
    test_parameters();
    test_graph_defaults();
    test_standard_material();
    test_specular_workflow();
    test_standard_property_resolution();
    test_graph_naming_and_estimates();
    test_legacy_pbr_material();
    test_conditions_and_discard();
    test_graph_upgrade();
    test_binding_limits();
    test_opacity_mask_shadows();
    test_world_position_offset();
    test_graph_material_binding();
    test_flow_nodes();
    test_material_functions();
    test_graph_material_values();
    test_graph_material_copy();
    test_graph_material_instances(ctx.get_cached<asset_manager>());
    test_node_code_choices();
    test_vertex_stage_outputs();
    test_texture_objects();
    test_material_attributes();
    test_quality_switch();
    test_compiled_quality_binding();
    const fs::path output_folder = fs::temp_directory_path() / "unravel-tests-material-graphs";
    fs::error_code ec;
    fs::create_directories(output_folder, ec);
    const bool can_compile = is_shaderc_present();
    if(!can_compile)
    {
        std::printf("  SKIP: shaderc is not built beside the tests, so no shader compiles\n");
    }
    test_examples(can_compile ? &ctx.get_cached<asset_manager>() : nullptr, output_folder);
    test_graph_material_asset(ctx.get_cached<asset_manager>(), can_compile);
    fs::remove_all(output_folder, ec);
    fs::remove_all(fs::resolve_protocol("app:/") / SHADER_FOLDER, ec);
    std::printf("material graphs: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}
} // namespace

REGISTER_TEST_SUITE("material graphs", run_material_graph_suite)
