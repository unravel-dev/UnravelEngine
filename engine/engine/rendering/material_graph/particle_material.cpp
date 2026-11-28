#include "particle_material.h"
#include "graph_material.h"
#include "material_graph_library.h"

#include <engine/defaults/engine_asset_keys.h>

#include <algorithm>

namespace unravel
{

namespace
{
constexpr const char* PARTICLE_TEXTURE_ID = "texture";
constexpr const char* PARTICLE_COLOR_ID = "color";
constexpr const char* PARTICLE_EROSION_ID = "erosion";
constexpr const char* PARTICLE_OPACITY_ID = "opacity";
constexpr const char* PARTICLE_TEXTURE_HINT = "What each particle draws: one image, or a sheet whose frames the emitter's "
                                              "Sheet rows play.";
constexpr const char* PARTICLE_MASK_HINT = "A mask: its brightness is the opacity, and its colour, tinted by the "
                                           "particle's, shows as bright as the mask is.";
constexpr const char* PARTICLE_NORMAL_MAP_HINT = "A tangent-space normal map (green up) laid out like the Texture, sheet "
                                                 "frames included: the sun lights each pixel by it.";
/// The weights of a colour's channels in a mask's opacity.
const math::vec4 PARTICLE_LUMINANCE_WEIGHTS{0.299f, 0.587f, 0.114f, 0.0f};
/// An opacity no lower than this is divided out of an Additive mask's colour, which is added once, by that opacity.
constexpr float PARTICLE_MIN_MASK = 0.0001f;

auto add_particle_node(material_graph& graph, const char* id, const char* type) -> material_graph_node&
{
    auto& node = graph.nodes.emplace_back();
    node.id = id;
    node.type = type;
    return node;
}

void link_particle_input(material_graph& graph, const char* id, const char* pin, const std::string& source)
{
    auto& node = graph.nodes[size_t(find_material_graph_node(graph, id))];
    get_or_add_material_graph_input(node, pin, {}).link = source;
}

/// @p a times @p b, as node @p id.
void add_particle_product(material_graph& graph, const char* id, const std::string& a, const std::string& b)
{
    add_particle_node(graph, id, "multiply");
    link_particle_input(graph, id, "a", a);
    link_particle_input(graph, id, "b", b);
}

/// Whether @p surface lights: every Blend Mode but Modulate, which filters the scene.
auto is_particle_surface_lit(const particle_surface& surface) -> bool
{
    return surface.blend != material_blend_mode::modulate;
}

/// The Surface Output's settings: the Blend Mode of @p surface, a Shading Model the emitter's Lighting can light, drawn
/// on particles, with their motion for the emitters that write it.
void set_particle_output_settings(material_graph& graph, const particle_surface& surface)
{
    set_material_graph_blend_mode(graph, surface.blend);
    auto& output = graph.nodes[size_t(find_material_graph_output(graph))];
    const auto model = is_particle_surface_lit(surface) ? material_shading_model::default_lit : material_shading_model::unlit;
    set_material_graph_setting(output, MATERIAL_SHADING_MODEL_SETTING, get_material_shading_model_key(model));
    set_material_graph_setting(output, MATERIAL_PARTICLE_USE_SETTING, MATERIAL_SETTING_ON);
    set_material_graph_setting(output, MATERIAL_WRITE_VELOCITY_SETTING, MATERIAL_SETTING_ON);
}

/// The texture, read at the particle's sheet frame, and the particle's colour.
void add_particle_sources(material_graph& graph, const particle_surface& surface)
{
    auto& texture = add_particle_node(graph, PARTICLE_TEXTURE_ID, "flipbook_sample");
    set_material_graph_setting(texture, MATERIAL_PARAMETER_NAME_SETTING, PARTICLE_TEXTURE_NAME);
    set_material_graph_setting(texture, MATERIAL_PARAMETER_DESCRIPTION_SETTING, surface.is_mask ? PARTICLE_MASK_HINT : PARTICLE_TEXTURE_HINT);
    // A quad that faces the camera gains nothing from anisotropic filtering, which costs a read of its own.
    set_material_graph_setting(texture, MATERIAL_TEXTURE_FILTER_SETTING, MATERIAL_TEXTURE_FILTER_TRILINEAR);
    add_particle_node(graph, PARTICLE_COLOR_ID, "vertex_color");
}

/// The opacity before the emitter's fades: what the texture gives (@p alpha), dissolved by the emitter's erosion,
/// times the particle's own.
void add_particle_opacity(material_graph& graph, const std::string& alpha)
{
    add_particle_node(graph, PARTICLE_EROSION_ID, "particle_erosion");
    link_particle_input(graph, PARTICLE_EROSION_ID, "alpha", alpha);
    add_particle_product(graph, PARTICLE_OPACITY_ID, "erosion.out", "color.a");
}

/// A texture with a colour and an alpha; the link of the colour the surface shows.
auto add_multi_channel_color(material_graph& graph, const particle_surface& surface) -> std::string
{
    add_particle_opacity(graph, "texture.a");
    if(surface.blend != material_blend_mode::premultiplied)
    {
        add_particle_product(graph, "tinted", "texture.rgb", "color.rgb");
        return "tinted";
    }
    // A premultiplied texture's colour carries its alpha: it erodes and fades with it.
    add_particle_product(graph, "eroded", "texture.rgb", "erosion.scale");
    add_particle_product(graph, "tinted", "eroded", "color.rgb");
    add_particle_product(graph, "faded", "tinted", "color.a");
    return "faded";
}

/// A mask: its luminance is the opacity; the link of the colour the surface shows.
auto add_mask_color(material_graph& graph, const particle_surface& surface) -> std::string
{
    auto& weights = add_particle_node(graph, "weights", "vector3");
    get_or_add_material_graph_input(weights, "value", {}).value = PARTICLE_LUMINANCE_WEIGHTS;
    add_particle_node(graph, "luminance", "dot");
    link_particle_input(graph, "luminance", "a", "texture.rgb");
    link_particle_input(graph, "luminance", "b", "weights");
    add_particle_opacity(graph, "luminance");
    if(surface.blend == material_blend_mode::modulate)
    {
        // The mask says where the particle's colour filters the scene.
        return "color.rgb";
    }
    // The colour is as bright as the mask already: it takes the particle's opacity and the erosion, not the mask again.
    add_particle_product(graph, "tinted", "texture.rgb", "color.rgb");
    add_particle_product(graph, "eroded", "tinted", "erosion.scale");
    add_particle_product(graph, "faded", "eroded", "color.a");
    if(surface.blend != material_blend_mode::additive)
    {
        return "faded";
    }
    // An Additive surface adds its colour times the opacity it draws with, which here is the mask: that opacity is
    // divided out first, so the mask counts once and a mask brighter than 1 keeps its light.
    add_particle_node(graph, "coverage", "saturate");
    link_particle_input(graph, "coverage", "in", PARTICLE_OPACITY_ID);
    auto& floor = add_particle_node(graph, "mask_floor", "maximum");
    get_or_add_material_graph_input(floor, "b", {}).value = math::vec4(PARTICLE_MIN_MASK);
    link_particle_input(graph, "mask_floor", "a", "coverage");
    add_particle_node(graph, "added", "divide");
    link_particle_input(graph, "added", "a", "faded");
    link_particle_input(graph, "added", "b", "mask_floor");
    return "added";
}

void link_particle_normal_map(material_graph& graph)
{
    add_particle_node(graph, "frames", "particle_sub_uv");
    auto& normal = add_particle_node(graph, "normal_map", "normal_map");
    set_material_graph_setting(normal, MATERIAL_PARAMETER_NAME_SETTING, PARTICLE_NORMAL_MAP_NAME);
    set_material_graph_setting(normal, MATERIAL_PARAMETER_DESCRIPTION_SETTING, PARTICLE_NORMAL_MAP_HINT);
    set_material_graph_setting(normal, MATERIAL_TEXTURE_FILTER_SETTING, MATERIAL_TEXTURE_FILTER_TRILINEAR);
    link_particle_input(graph, "normal_map", "uv", "frames.uv");
    link_particle_input(graph, "output", "normal", "normal_map");
}

auto get_particle_blend_suffix(const particle_surface& surface) -> const char*
{
    switch(surface.blend)
    {
        case material_blend_mode::additive:
            return "_additive";
        case material_blend_mode::modulate:
            return "_modulate";
        case material_blend_mode::premultiplied:
            // A mask has one alpha-blended form, the plain one.
            return surface.is_mask ? "" : "_premultiplied";
        default:
            return "";
    }
}
} // namespace

auto get_shipped_particle_surface(const particle_surface& surface) -> particle_surface
{
    particle_surface shipped = surface;
    if(!is_material_blend_mode_forward(shipped.blend))
    {
        shipped.blend = material_blend_mode::translucent;
    }
    shipped.has_normal_map = shipped.has_normal_map && is_particle_surface_lit(shipped);
    if(shipped.is_mask && shipped.blend == material_blend_mode::translucent)
    {
        shipped.blend = material_blend_mode::premultiplied;
    }
    return shipped;
}

auto make_particle_material_graph(const particle_surface& surface) -> material_graph
{
    const auto shipped = get_shipped_particle_surface(surface);
    material_graph graph;
    ensure_material_graph_output(graph);
    set_particle_output_settings(graph, shipped);
    add_particle_sources(graph, shipped);
    const std::string color = shipped.is_mask ? add_mask_color(graph, shipped) : add_multi_channel_color(graph, shipped);
    // The Base Color of a surface the emitter's Lighting lights, or shows as it is; the Emissive of one that filters.
    link_particle_input(graph, "output", is_particle_surface_lit(shipped) ? "base_color" : "emissive", color);
    link_particle_input(graph, "output", "opacity_mask", PARTICLE_OPACITY_ID);
    if(shipped.has_normal_map)
    {
        link_particle_normal_map(graph);
    }
    return graph;
}

auto make_particle_material(const particle_surface& surface) -> std::shared_ptr<graph_material>
{
    auto mat = std::make_shared<graph_material>();
    mat->set_graph(make_particle_material_graph(surface));
    return mat;
}

auto get_particle_material_key(const particle_surface& surface) -> std::string
{
    const auto shipped = get_shipped_particle_surface(surface);
    std::string key = PARTICLE_MATERIAL_KEY_PREFIX;
    if(shipped.is_mask)
    {
        key += "_mask";
    }
    key += get_particle_blend_suffix(shipped);
    if(shipped.has_normal_map)
    {
        key += "_normal_map";
    }
    return key + ".mat";
}

auto get_shipped_particle_surfaces() -> std::vector<particle_surface>
{
    std::vector<particle_surface> surfaces;
    for(const bool is_mask : {false, true})
    {
        for(const auto blend : {material_blend_mode::translucent,
                                material_blend_mode::premultiplied,
                                material_blend_mode::additive,
                                material_blend_mode::modulate})
        {
            for(const bool has_normal_map : {false, true})
            {
                const auto shipped = get_shipped_particle_surface({blend, is_mask, has_normal_map});
                if(std::find(surfaces.begin(), surfaces.end(), shipped) == surfaces.end())
                {
                    surfaces.push_back(shipped);
                }
            }
        }
    }
    return surfaces;
}

auto find_shipped_particle_surface(const std::string& key) -> hpp::optional<particle_surface>
{
    // Translucent before Premultiplied: a mask's one material answers as the plain blend.
    for(const bool is_mask : {false, true})
    {
        for(const auto blend : {material_blend_mode::translucent,
                                material_blend_mode::premultiplied,
                                material_blend_mode::additive,
                                material_blend_mode::modulate})
        {
            for(const bool has_normal_map : {false, true})
            {
                const particle_surface surface{blend, is_mask, has_normal_map};
                if(get_particle_material_key(surface) == key)
                {
                    return surface;
                }
            }
        }
    }
    return {};
}

} // namespace unravel
