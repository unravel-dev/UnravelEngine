#include "dynamic_geometry_material.h"
#include "material_graph_library.h"

namespace unravel
{

namespace
{
constexpr const char* STRIP_COLOR_ID = "color";
/// Rough enough that the strip does not mirror the sky.
constexpr float STRIP_ROUGHNESS = 0.8f;
} // namespace

auto make_strip_material_graph() -> material_graph
{
    material_graph graph;
    ensure_material_graph_output(graph);
    set_material_graph_blend_mode(graph, material_blend_mode::dithered);
    auto& color = graph.nodes.emplace_back();
    color.id = STRIP_COLOR_ID;
    color.type = "vertex_color";
    auto& output = graph.nodes[size_t(find_material_graph_output(graph))];
    set_material_graph_setting(output, MATERIAL_TWO_SIDED_SETTING, MATERIAL_SETTING_ON);
    get_or_add_material_graph_input(output, MATERIAL_ROUGHNESS_PIN, {}).value = math::vec4(STRIP_ROUGHNESS);
    get_or_add_material_graph_input(output, "base_color", {}).link = "color.rgb";
    get_or_add_material_graph_input(output, "opacity_mask", {}).link = "color.a";
    return graph;
}

} // namespace unravel
