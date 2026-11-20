#include "material_graph.h"
#include "material_graph_library.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>

namespace unravel
{

namespace
{
/// Graph-space distance under which two nodes count as placed on one spot.
constexpr float OVERLAP_DISTANCE = 1.0f;
constexpr char LINK_SEPARATOR = '.';
constexpr const char* OUTPUT_NODE_ID = "output";
constexpr const char* COMPONENT_LETTERS_XYZW = "xyzw";
constexpr const char* COMPONENT_LETTERS_RGBA = "rgba";
constexpr int MAX_COMPONENTS = 4;

auto is_shader_identifier_char(char c) -> bool
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

/// True when @p link reads an output of the node with @p id.
auto is_link_from(const std::string& link, hpp::string_view id) -> bool
{
    return !link.empty() && parse_material_graph_link(link).node == id;
}

void rename_links(material_graph& graph, hpp::string_view old_id, const std::string& new_id)
{
    for(auto& node : graph.nodes)
    {
        for(auto& input : node.inputs)
        {
            if(is_link_from(input.link, old_id))
            {
                const auto end = parse_material_graph_link(input.link);
                input.link = make_material_graph_link(new_id, end.pin, end.components);
            }
        }
    }
}
} // namespace

auto parse_material_graph_link(hpp::string_view link) -> material_graph_link_end
{
    material_graph_link_end end;
    const auto first = link.find(LINK_SEPARATOR);
    end.node = std::string(link.substr(0, first));
    if(first == hpp::string_view::npos)
    {
        return end;
    }
    const auto rest = link.substr(first + 1);
    const auto second = rest.find(LINK_SEPARATOR);
    end.pin = std::string(rest.substr(0, second));
    if(second != hpp::string_view::npos)
    {
        end.components = std::string(rest.substr(second + 1));
    }
    return end;
}

auto make_material_graph_link(hpp::string_view node, hpp::string_view pin, hpp::string_view components) -> std::string
{
    std::string link(node);
    if(!pin.empty())
    {
        link += LINK_SEPARATOR;
        link += pin;
        if(!components.empty())
        {
            link += LINK_SEPARATOR;
            link += components;
        }
    }
    return link;
}

auto get_material_component_index(char letter) -> int
{
    for(int i = 0; i < MAX_COMPONENTS; ++i)
    {
        if(letter == COMPONENT_LETTERS_XYZW[i] || letter == COMPONENT_LETTERS_RGBA[i])
        {
            return i;
        }
    }
    return -1;
}

auto parse_material_graph_pin(hpp::string_view entry) -> material_graph_pin_component
{
    const auto separator = entry.find(LINK_SEPARATOR);
    if(separator == hpp::string_view::npos || separator + 2 != entry.size())
    {
        return {std::string(entry), -1};
    }
    return {std::string(entry.substr(0, separator)), get_material_component_index(entry.back())};
}

auto make_material_graph_component_pin(hpp::string_view pin, int component, hpp::string_view letters) -> std::string
{
    std::string entry(pin);
    entry += LINK_SEPARATOR;
    entry += letters[size_t(component)];
    return entry;
}

auto find_material_graph_node(const material_graph& graph, hpp::string_view id) -> int
{
    const auto it = std::find_if(graph.nodes.begin(),
                                 graph.nodes.end(),
                                 [&](const material_graph_node& node)
                                 {
                                     return node.id == id;
                                 });
    return it == graph.nodes.end() ? -1 : int(it - graph.nodes.begin());
}

auto find_material_graph_output(const material_graph& graph) -> int
{
    const auto it = std::find_if(graph.nodes.begin(),
                                 graph.nodes.end(),
                                 [](const material_graph_node& node)
                                 {
                                     return is_material_graph_output_type(node.type);
                                 });
    return it == graph.nodes.end() ? -1 : int(it - graph.nodes.begin());
}

namespace
{
struct material_domain_names
{
    material_domain domain;
    const char* output_type;
    const char* key;
    const char* label;
};

constexpr std::array<material_domain_names, MATERIAL_DOMAIN_COUNT> MATERIAL_DOMAIN_NAMES = {{
    {material_domain::surface, MATERIAL_GRAPH_OUTPUT_TYPE, "surface", "Surface"},
    {material_domain::decal, MATERIAL_GRAPH_DECAL_OUTPUT_TYPE, "decal", "Decal"},
    {material_domain::post_process, MATERIAL_GRAPH_POST_PROCESS_OUTPUT_TYPE, "post_process", "Post Process"},
}};

struct material_post_process_location_names
{
    material_post_process_location location;
    const char* key;
    const char* label;
};

constexpr std::array<material_post_process_location_names, MATERIAL_POST_PROCESS_LOCATION_COUNT> MATERIAL_POST_PROCESS_LOCATION_NAMES = {{
    {material_post_process_location::before_transparent, "before_transparent", "Before Transparent"},
    {material_post_process_location::before_bloom, "before_bloom", "Before Bloom"},
    {material_post_process_location::after_tonemapping, "after_tonemapping", "After Tonemapping"},
}};

struct material_blend_mode_names
{
    material_blend_mode mode;
    const char* key;
    const char* label;
};

constexpr std::array<material_blend_mode_names, MATERIAL_BLEND_MODE_COUNT> MATERIAL_BLEND_MODE_NAMES = {{
    {material_blend_mode::opaque, "opaque", "Opaque"},
    {material_blend_mode::dithered, "dithered", "Dithered"},
    {material_blend_mode::translucent, "translucent", "Translucent"},
    {material_blend_mode::premultiplied, "premultiplied", "Premultiplied"},
    {material_blend_mode::additive, "additive", "Additive"},
    {material_blend_mode::modulate, "modulate", "Modulate"},
}};

struct material_shading_model_names
{
    material_shading_model model;
    const char* key;
    const char* label;
};

constexpr std::array<material_shading_model_names, MATERIAL_SHADING_MODEL_COUNT> MATERIAL_SHADING_MODEL_NAMES = {{
    {material_shading_model::default_lit, "default_lit", "Default Lit"},
    {material_shading_model::unlit, "unlit", "Unlit"},
}};

/// Whether @p output blends without sorting (Order Independent on a blend that takes it).
auto is_output_order_independent(const material_graph_node& output, material_blend_mode mode) -> bool
{
    const auto* value = find_material_graph_setting(output, MATERIAL_ORDER_INDEPENDENT_SETTING);
    return is_material_blend_mode_order_dependent(mode) && value != nullptr && *value == MATERIAL_ORDER_INDEPENDENT_ON;
}

/// The Surface Output's pins an Unlit surface reads (the translucency pass's refraction reads Normal besides).
auto is_unlit_surface_pin(hpp::string_view pin) -> bool
{
    return pin == "emissive" || pin == "opacity_mask" || pin == MATERIAL_OPACITY_MASK_CLIP_PIN || pin == "camera_fade_distance" ||
           pin == "world_position_offset" || pin == "material_attributes";
}

auto get_material_domain_names(material_domain domain) -> const material_domain_names&
{
    const auto index = size_t(domain);
    return MATERIAL_DOMAIN_NAMES[index < MATERIAL_DOMAIN_NAMES.size() ? index : 0];
}

/// The pin of the new output node an input of the old one moves to: its own name, or the opacity of the other domain.
auto get_domain_moved_pin(const std::string& pin) -> std::string
{
    if(pin == "opacity_mask")
    {
        return MATERIAL_OPACITY_PIN;
    }
    return pin;
}
} // namespace

auto get_material_graph_output_type(material_domain domain) -> const char*
{
    return get_material_domain_names(domain).output_type;
}

auto find_material_domain_of_output_type(hpp::string_view type) -> hpp::optional<material_domain>
{
    for(const auto& names : MATERIAL_DOMAIN_NAMES)
    {
        if(type == names.output_type)
        {
            return names.domain;
        }
    }
    return {};
}

auto is_material_graph_output_type(hpp::string_view type) -> bool
{
    return static_cast<bool>(find_material_domain_of_output_type(type));
}

auto get_material_domain_label(material_domain domain) -> const char*
{
    return get_material_domain_names(domain).label;
}

auto get_material_domain_key(material_domain domain) -> const char*
{
    return get_material_domain_names(domain).key;
}

auto find_material_domain(hpp::string_view key) -> hpp::optional<material_domain>
{
    for(const auto& names : MATERIAL_DOMAIN_NAMES)
    {
        if(key == names.key)
        {
            return names.domain;
        }
    }
    return {};
}

auto get_material_graph_domain(const material_graph& graph) -> material_domain
{
    const int output = find_material_graph_output(graph);
    if(output < 0)
    {
        return material_domain::surface;
    }
    return find_material_domain_of_output_type(graph.nodes[size_t(output)].type).value_or(material_domain::surface);
}

auto set_material_graph_domain(material_graph& graph, material_domain domain) -> bool
{
    const char* type = get_material_graph_output_type(domain);
    const int index = find_material_graph_output(graph);
    if(index < 0)
    {
        if(is_material_function_graph(graph))
        {
            return false;
        }
        material_graph_node output;
        output.id = make_unique_material_graph_node_id(graph, OUTPUT_NODE_ID);
        output.type = type;
        graph.nodes.push_back(std::move(output));
        return true;
    }
    auto& output = graph.nodes[size_t(index)];
    if(output.type == type)
    {
        return false;
    }
    const auto* def = find_material_node_def(type);
    std::vector<material_graph_input> inputs;
    for(const auto& input : output.inputs)
    {
        const auto entry = parse_material_graph_pin(input.pin);
        const std::string moved = get_domain_moved_pin(entry.pin);
        if(def == nullptr || find_material_pin_def(def->inputs, moved) == nullptr)
        {
            continue;
        }
        material_graph_input copy = input;
        copy.pin = entry.component < 0 ? moved : moved + input.pin.substr(input.pin.find('.'));
        inputs.push_back(std::move(copy));
    }
    std::vector<material_graph_setting> settings;
    for(const auto& setting : output.settings)
    {
        if(def != nullptr && find_material_setting_def(*def, setting.name) != nullptr)
        {
            settings.push_back(setting);
        }
    }
    output.type = type;
    output.inputs = std::move(inputs);
    output.settings = std::move(settings);
    output.split_inputs.clear();
    return true;
}

auto get_material_post_process_location_key(material_post_process_location location) -> const char*
{
    const auto index = size_t(location);
    return MATERIAL_POST_PROCESS_LOCATION_NAMES[index < MATERIAL_POST_PROCESS_LOCATION_NAMES.size() ? index : 0].key;
}

auto get_material_post_process_location_label(material_post_process_location location) -> const char*
{
    const auto index = size_t(location);
    return MATERIAL_POST_PROCESS_LOCATION_NAMES[index < MATERIAL_POST_PROCESS_LOCATION_NAMES.size() ? index : 0].label;
}

auto find_material_post_process_location(hpp::string_view key) -> hpp::optional<material_post_process_location>
{
    for(const auto& names : MATERIAL_POST_PROCESS_LOCATION_NAMES)
    {
        if(key == names.key)
        {
            return names.location;
        }
    }
    return {};
}

auto get_material_blend_mode_key(material_blend_mode mode) -> const char*
{
    const auto index = size_t(mode);
    return MATERIAL_BLEND_MODE_NAMES[index < MATERIAL_BLEND_MODE_NAMES.size() ? index : 0].key;
}

auto get_material_blend_mode_label(material_blend_mode mode) -> const char*
{
    const auto index = size_t(mode);
    return MATERIAL_BLEND_MODE_NAMES[index < MATERIAL_BLEND_MODE_NAMES.size() ? index : 0].label;
}

auto find_material_blend_mode(hpp::string_view key) -> hpp::optional<material_blend_mode>
{
    for(const auto& names : MATERIAL_BLEND_MODE_NAMES)
    {
        if(key == names.key)
        {
            return names.mode;
        }
    }
    return {};
}

auto is_material_blend_mode_order_dependent(material_blend_mode mode) -> bool
{
    return mode == material_blend_mode::translucent || mode == material_blend_mode::premultiplied;
}

auto is_material_blend_mode_forward(material_blend_mode mode) -> bool
{
    return mode != material_blend_mode::opaque && mode != material_blend_mode::dithered;
}

auto get_material_shading_model_key(material_shading_model model) -> const char*
{
    const auto index = size_t(model);
    return MATERIAL_SHADING_MODEL_NAMES[index < MATERIAL_SHADING_MODEL_NAMES.size() ? index : 0].key;
}

auto get_material_shading_model_label(material_shading_model model) -> const char*
{
    const auto index = size_t(model);
    return MATERIAL_SHADING_MODEL_NAMES[index < MATERIAL_SHADING_MODEL_NAMES.size() ? index : 0].label;
}

auto find_material_shading_model(hpp::string_view key) -> hpp::optional<material_shading_model>
{
    for(const auto& names : MATERIAL_SHADING_MODEL_NAMES)
    {
        if(key == names.key)
        {
            return names.model;
        }
    }
    return {};
}

auto find_material_graph_input(const material_graph_node& node, hpp::string_view pin) -> const material_graph_input*
{
    const auto it = std::find_if(node.inputs.begin(),
                                 node.inputs.end(),
                                 [&](const material_graph_input& input)
                                 {
                                     return input.pin == pin;
                                 });
    return it == node.inputs.end() ? nullptr : &*it;
}

auto find_material_graph_input(material_graph_node& node, hpp::string_view pin) -> material_graph_input*
{
    return const_cast<material_graph_input*>(find_material_graph_input(std::as_const(node), pin));
}

auto get_or_add_material_graph_input(material_graph_node& node, hpp::string_view pin, const math::vec4& default_value)
    -> material_graph_input&
{
    if(auto* input = find_material_graph_input(node, pin))
    {
        return *input;
    }
    auto& input = node.inputs.emplace_back();
    input.pin = std::string(pin);
    input.value = default_value;
    return input;
}

void upgrade_material_graph(material_graph& graph)
{
    const int index = find_material_graph_output(graph);
    if(index < 0)
    {
        return;
    }
    auto& output = graph.nodes[size_t(index)];
    const auto translucent = std::find_if(output.settings.begin(),
                                          output.settings.end(),
                                          [](const material_graph_setting& entry)
                                          {
                                              return entry.name == MATERIAL_TRANSLUCENT_SETTING;
                                          });
    if(translucent != output.settings.end())
    {
        const bool is_on = translucent->value == MATERIAL_TRANSLUCENT_ON;
        output.settings.erase(translucent);
        if(is_on && find_material_graph_setting(output, MATERIAL_BLEND_MODE_SETTING) == nullptr)
        {
            set_material_graph_setting(output, MATERIAL_BLEND_MODE_SETTING, get_material_blend_mode_key(k_default_translucent_blend_mode));
        }
    }
    const auto setting = std::find_if(output.settings.begin(),
                                      output.settings.end(),
                                      [](const material_graph_setting& entry)
                                      {
                                          return entry.name == MATERIAL_OPACITY_MASK_CLIP_PIN;
                                      });
    if(setting == output.settings.end())
    {
        return;
    }
    if(find_material_graph_input(output, MATERIAL_OPACITY_MASK_CLIP_PIN) == nullptr)
    {
        get_or_add_material_graph_input(output, MATERIAL_OPACITY_MASK_CLIP_PIN, {}).value =
            math::vec4(std::strtof(setting->value.c_str(), nullptr));
    }
    output.settings.erase(setting);
}

auto find_material_parameter_slider(const material_graph_node& node) -> hpp::optional<math::vec2>
{
    // A setting the node does not list has its default, 0.
    const auto read_setting = [&node](const char* name) -> float
    {
        const auto* value = find_material_graph_setting(node, name);
        return value != nullptr ? std::strtof(value->c_str(), nullptr) : 0.0f;
    };
    const math::vec2 range(read_setting(MATERIAL_PARAMETER_SLIDER_MIN_SETTING), read_setting(MATERIAL_PARAMETER_SLIDER_MAX_SETTING));
    if(!std::isfinite(range.x) || !std::isfinite(range.y) || range.y <= range.x)
    {
        return {};
    }
    return range;
}

auto set_material_graph_parameter_default(material_graph& graph, hpp::string_view node_id, const math::vec4& value) -> bool
{
    const int index = find_material_graph_node(graph, node_id);
    auto* node = index >= 0 ? &graph.nodes[size_t(index)] : nullptr;
    const auto* def = node != nullptr ? find_material_node_def(node->type) : nullptr;
    if(def == nullptr || def->code != material_node_code::parameter || def->inputs.empty())
    {
        return false;
    }
    const auto& pin = def->inputs.front();
    get_or_add_material_graph_input(*node, pin.name, pin.default_value).value = value;
    return true;
}

auto set_material_graph_node_texture(material_graph& graph, hpp::string_view node_id, const asset_handle<gfx::texture>& texture)
    -> bool
{
    const int index = find_material_graph_node(graph, node_id);
    auto* node = index >= 0 ? &graph.nodes[size_t(index)] : nullptr;
    if(node == nullptr || !holds_material_graph_texture(*node))
    {
        return false;
    }
    node->texture = texture;
    return true;
}

auto holds_material_graph_texture(const material_graph_node& node) -> bool
{
    const auto* def = find_material_node_def(node.type);
    if(def == nullptr)
    {
        return false;
    }
    if(def->code == material_node_code::function_input)
    {
        const auto* type = find_material_graph_setting(node, "type");
        return type != nullptr && (*type == "sampler2D" || *type == "samplerCube");
    }
    return is_material_texture_node(*def);
}

auto set_material_graph_static_switch(material_graph& graph, hpp::string_view node_id, bool value) -> bool
{
    const int index = find_material_graph_node(graph, node_id);
    auto* node = index >= 0 ? &graph.nodes[size_t(index)] : nullptr;
    const auto* def = node != nullptr ? find_material_node_def(node->type) : nullptr;
    if(def == nullptr || def->code != material_node_code::static_switch)
    {
        return false;
    }
    const std::string text = value ? MATERIAL_STATIC_SWITCH_TRUE : MATERIAL_STATIC_SWITCH_FALSE;
    const auto* current = find_material_graph_setting(*node, MATERIAL_STATIC_SWITCH_VALUE_SETTING);
    // An unset switch passes True (the library's default).
    if((current != nullptr ? *current : std::string(MATERIAL_STATIC_SWITCH_TRUE)) == text)
    {
        return false;
    }
    set_material_graph_setting(*node, MATERIAL_STATIC_SWITCH_VALUE_SETTING, text);
    return true;
}

auto find_material_graph_setting(const material_graph_node& node, hpp::string_view name) -> const std::string*
{
    const auto it = std::find_if(node.settings.begin(),
                                 node.settings.end(),
                                 [&](const material_graph_setting& setting)
                                 {
                                     return setting.name == name;
                                 });
    return it == node.settings.end() ? nullptr : &it->value;
}

void set_material_graph_setting(material_graph_node& node, hpp::string_view name, const std::string& value)
{
    const auto it = std::find_if(node.settings.begin(),
                                 node.settings.end(),
                                 [&](const material_graph_setting& setting)
                                 {
                                     return setting.name == name;
                                 });
    if(it != node.settings.end())
    {
        it->value = value;
        return;
    }
    node.settings.push_back({std::string(name), value});
}

auto is_material_specular_workflow(const material_graph_node& output) -> bool
{
    const auto* workflow = find_material_graph_setting(output, MATERIAL_WORKFLOW_SETTING);
    return workflow != nullptr && *workflow == MATERIAL_WORKFLOW_SPECULAR;
}

auto is_material_graph_specular_workflow(const material_graph& graph) -> bool
{
    const int output = find_material_graph_output(graph);
    return output >= 0 && is_material_specular_workflow(graph.nodes[size_t(output)]);
}

auto get_material_graph_workflow_label(const material_graph& graph) -> std::string
{
    const auto* output_def = find_material_node_def(MATERIAL_GRAPH_OUTPUT_TYPE);
    const auto* workflow = output_def != nullptr ? find_material_setting_def(*output_def, MATERIAL_WORKFLOW_SETTING) : nullptr;
    const std::string value = is_material_graph_specular_workflow(graph) ? MATERIAL_WORKFLOW_SPECULAR : MATERIAL_WORKFLOW_METALLIC;
    return workflow != nullptr ? get_material_setting_choice_label(*workflow, value) : value;
}

auto get_material_blend_mode(const material_graph_node& output) -> material_blend_mode
{
    const auto* value = find_material_graph_setting(output, MATERIAL_BLEND_MODE_SETTING);
    if(value == nullptr)
    {
        return material_blend_mode::opaque;
    }
    return find_material_blend_mode(*value).value_or(material_blend_mode::opaque);
}

auto get_material_graph_blend_mode(const material_graph& graph) -> material_blend_mode
{
    const int output = find_material_graph_output(graph);
    return output >= 0 ? get_material_blend_mode(graph.nodes[size_t(output)]) : material_blend_mode::opaque;
}

auto set_material_graph_blend_mode(material_graph& graph, material_blend_mode mode) -> bool
{
    const int output = find_material_graph_output(graph);
    if(output < 0 || get_material_blend_mode(graph.nodes[size_t(output)]) == mode)
    {
        return false;
    }
    set_material_graph_setting(graph.nodes[size_t(output)], MATERIAL_BLEND_MODE_SETTING, get_material_blend_mode_key(mode));
    return true;
}

auto get_material_shading_model(const material_graph_node& output) -> material_shading_model
{
    if(get_material_blend_mode(output) == material_blend_mode::modulate)
    {
        return material_shading_model::unlit;
    }
    const auto* value = find_material_graph_setting(output, MATERIAL_SHADING_MODEL_SETTING);
    if(value == nullptr)
    {
        return material_shading_model::default_lit;
    }
    return find_material_shading_model(*value).value_or(material_shading_model::default_lit);
}

auto get_material_graph_shading_model(const material_graph& graph) -> material_shading_model
{
    const int output = find_material_graph_output(graph);
    return output >= 0 ? get_material_shading_model(graph.nodes[size_t(output)]) : material_shading_model::default_lit;
}

auto is_material_output_pin_used(const material_graph_node& output, hpp::string_view pin) -> bool
{
    const material_blend_mode mode = get_material_blend_mode(output);
    const bool is_forward = is_material_blend_mode_forward(mode);
    if(pin == MATERIAL_OPACITY_MASK_CLIP_PIN)
    {
        return mode == material_blend_mode::opaque;
    }
    if(pin == MATERIAL_REFRACTION_PIN)
    {
        // The order-independent blend draws no refraction.
        return is_material_blend_mode_order_dependent(mode) && !is_output_order_independent(output, mode);
    }
    if(get_material_shading_model(output) == material_shading_model::unlit)
    {
        return is_unlit_surface_pin(pin) || (is_forward && pin == "normal");
    }
    if(is_forward && (pin == "subsurface_color" || pin == "subsurface_opacity"))
    {
        return false;
    }
    const bool is_metallic_pin = pin == MATERIAL_METALLIC_PIN || pin == MATERIAL_ROUGHNESS_PIN;
    const bool is_specular_pin = pin == MATERIAL_SPECULAR_PIN || pin == MATERIAL_GLOSSINESS_PIN;
    if(!is_metallic_pin && !is_specular_pin)
    {
        return true;
    }
    return is_specular_pin == is_material_specular_workflow(output);
}

namespace
{
/// Whether @p setting belongs to every surface of the translucency pass.
auto is_translucency_pass_setting(hpp::string_view setting) -> bool
{
    return setting == MATERIAL_TAA_RESPONSE_SETTING || setting == MATERIAL_CAST_SHADOW_SETTING ||
           setting == MATERIAL_WRITE_VELOCITY_SETTING;
}

/// Whether @p setting belongs to the order-dependent blends alone (Translucent, Premultiplied).
auto is_order_dependent_blend_setting(hpp::string_view setting) -> bool
{
    return setting == MATERIAL_ORDER_INDEPENDENT_SETTING || setting == MATERIAL_DEPTH_PREPASS_SETTING;
}

} // namespace

auto is_material_output_setting_shown(const material_graph_node& output, hpp::string_view setting) -> bool
{
    const material_blend_mode mode = get_material_blend_mode(output);
    if(setting == MATERIAL_PRESERVE_SPECULAR_SETTING)
    {
        return mode == material_blend_mode::translucent && get_material_shading_model(output) == material_shading_model::default_lit;
    }
    if(setting == MATERIAL_TAA_RESPONSE_SETTING || setting == MATERIAL_DEPTH_PREPASS_SETTING)
    {
        // An Order Independent surface marks TAA's reactive mask in the composite, at the default response, and has
        // no prepass.
        if(is_output_order_independent(output, mode))
        {
            return false;
        }
    }
    if(is_translucency_pass_setting(setting))
    {
        return is_material_blend_mode_forward(mode);
    }
    if(is_order_dependent_blend_setting(setting))
    {
        return is_material_blend_mode_order_dependent(mode);
    }
    if(setting == MATERIAL_SHADING_MODEL_SETTING)
    {
        return mode != material_blend_mode::modulate;
    }
    return true;
}

auto get_material_output_pin_label(const material_graph_node& output, hpp::string_view pin) -> const char*
{
    if(pin == "opacity_mask" && get_material_blend_mode(output) != material_blend_mode::opaque)
    {
        return "Opacity";
    }
    if(pin == "base_color" && is_material_specular_workflow(output))
    {
        return "Diffuse";
    }
    return nullptr;
}

auto sanitize_material_graph_identifier(hpp::string_view text) -> std::string
{
    std::string result;
    result.reserve(text.size());
    for(const char c : text)
    {
        const char lower = char(std::tolower(static_cast<unsigned char>(c)));
        const char kept = is_shader_identifier_char(lower) && static_cast<unsigned char>(lower) < 128 ? lower : '_';
        // GLSL reserves every name holding two underscores in a row.
        if(kept == '_' && !result.empty() && result.back() == '_')
        {
            continue;
        }
        result += kept;
    }
    while(!result.empty() && result.back() == '_')
    {
        result.pop_back();
    }
    if(result.empty() || result == "_")
    {
        return "node";
    }
    if(std::isdigit(static_cast<unsigned char>(result.front())) != 0 || result.front() == '_')
    {
        result.insert(result.begin(), 'n');
    }
    return result;
}

namespace
{
/// @p graph with only what generates code: no values (parameter defaults, texture nodes' textures) and no editor layout
/// (positions, split pins, folded swatches, comments).
auto strip_material_graph_to_code(material_graph graph) -> material_graph
{
    graph.comments.clear();
    for(auto& node : graph.nodes)
    {
        node.position = {};
        node.split_inputs.clear();
        node.split_outputs.clear();
        node.is_preview_hidden = false;
        node.texture = {};
        // A texture's sampler state is bound with the draw, never written into code.
        std::erase_if(node.settings,
                      [](const material_graph_setting& setting)
                      {
                          return setting.name == MATERIAL_TEXTURE_ADDRESS_SETTING || setting.name == MATERIAL_TEXTURE_FILTER_SETTING;
                      });
        const auto* def = find_material_node_def(node.type);
        if(def == nullptr || def->code != material_node_code::parameter || def->inputs.empty())
        {
            continue;
        }
        if(auto* input = find_material_graph_input(node, def->inputs.front().name))
        {
            input->value = {};
        }
    }
    return graph;
}
} // namespace

auto get_material_color_ramp_color_pin(uint32_t stop) -> std::string
{
    return "color" + std::to_string(stop);
}

auto get_material_color_ramp_position_pin(uint32_t stop) -> std::string
{
    return "position" + std::to_string(stop);
}

auto has_same_material_graph_code(const material_graph& a, const material_graph& b) -> bool
{
    // Graphs of other node counts differ in code without copying either.
    if(a.nodes.size() != b.nodes.size())
    {
        return false;
    }
    return strip_material_graph_to_code(a) == strip_material_graph_to_code(b);
}

auto make_unique_material_graph_node_id(const material_graph& graph, hpp::string_view id) -> std::string
{
    const std::string base = sanitize_material_graph_identifier(id);
    if(find_material_graph_node(graph, base) < 0)
    {
        return base;
    }
    for(int number = 1;; ++number)
    {
        std::string candidate = base + "_" + std::to_string(number);
        if(find_material_graph_node(graph, candidate) < 0)
        {
            return candidate;
        }
    }
}

void unlink_material_graph_node_outputs(material_graph& graph, hpp::string_view id)
{
    for(auto& node : graph.nodes)
    {
        for(auto& input : node.inputs)
        {
            if(is_link_from(input.link, id))
            {
                input.link.clear();
            }
        }
    }
}

auto remove_material_graph_node(material_graph& graph, hpp::string_view id) -> bool
{
    const int index = find_material_graph_node(graph, id);
    if(index < 0 || is_material_graph_output_type(graph.nodes[size_t(index)].type))
    {
        return false;
    }
    unlink_material_graph_node_outputs(graph, id);
    graph.nodes.erase(graph.nodes.begin() + index);
    return true;
}

auto rename_material_graph_node(material_graph& graph, hpp::string_view old_id, hpp::string_view new_id) -> std::string
{
    const int index = find_material_graph_node(graph, old_id);
    if(index < 0)
    {
        return {};
    }
    if(sanitize_material_graph_identifier(new_id) == old_id)
    {
        return std::string(old_id);
    }
    const std::string unique = make_unique_material_graph_node_id(graph, new_id);
    // The old id is copied first: it may view the node's own id, which the rename overwrites.
    const std::string previous(old_id);
    graph.nodes[size_t(index)].id = unique;
    rename_links(graph, previous, unique);
    return unique;
}

auto get_material_function_input_pin(uint32_t index) -> std::string
{
    return "in" + std::to_string(index);
}

auto get_material_function_output_pin(uint32_t index) -> std::string
{
    return "out" + std::to_string(index);
}

auto get_material_function_result_pin(uint32_t index) -> std::string
{
    return "result" + std::to_string(index);
}

auto is_material_function_graph(const material_graph& graph) -> bool
{
    const bool has_output = std::any_of(graph.nodes.begin(),
                                        graph.nodes.end(),
                                        [](const material_graph_node& node)
                                        {
                                            return node.type == MATERIAL_FUNCTION_OUTPUT_TYPE;
                                        });
    return has_output && find_material_graph_output(graph) < 0;
}

auto get_material_function_interface(const material_graph& function) -> material_function_interface
{
    material_function_interface result;
    for(const auto& node : function.nodes)
    {
        const bool is_input = node.type == MATERIAL_FUNCTION_INPUT_TYPE;
        if(!is_input && node.type != MATERIAL_FUNCTION_OUTPUT_TYPE)
        {
            continue;
        }
        const auto* name = find_material_graph_setting(node, MATERIAL_FUNCTION_NAME_SETTING);
        material_function_pin pin;
        pin.name = name != nullptr && !name->empty() ? *name : node.id;
        pin.node_id = node.id;
        if(is_input)
        {
            const auto* type = find_material_graph_setting(node, "type");
            pin.type = type != nullptr ? *type : "vec3";
        }
        (is_input ? result.inputs : result.outputs).push_back(std::move(pin));
    }
    return result;
}

namespace
{
void collect_material_function_keys(const material_graph& graph,
                                    const material_function_resolver& resolve,
                                    std::vector<std::string>& keys)
{
    for(const auto& node : graph.nodes)
    {
        const auto* key = node.type == MATERIAL_FUNCTION_CALL_TYPE ? find_material_graph_setting(node, MATERIAL_FUNCTION_KEY_SETTING) : nullptr;
        // A key listed already is followed already: a function calling itself ends here too.
        if(key == nullptr || key->empty() || std::find(keys.begin(), keys.end(), *key) != keys.end())
        {
            continue;
        }
        keys.push_back(*key);
        if(const auto function = resolve(*key))
        {
            collect_material_function_keys(*function, resolve, keys);
        }
    }
}
} // namespace

auto collect_material_function_keys(const material_graph& graph, const material_function_resolver& resolve)
    -> std::vector<std::string>
{
    std::vector<std::string> keys;
    collect_material_function_keys(graph, resolve, keys);
    return keys;
}

auto ensure_material_graph_output(material_graph& graph) -> bool
{
    if(find_material_graph_output(graph) >= 0 || is_material_function_graph(graph))
    {
        return false;
    }
    material_graph_node output;
    output.id = make_unique_material_graph_node_id(graph, OUTPUT_NODE_ID);
    output.type = MATERIAL_GRAPH_OUTPUT_TYPE;
    graph.nodes.push_back(std::move(output));
    return true;
}

auto is_material_graph_never_laid_out(const material_graph& graph) -> bool
{
    if(graph.nodes.size() < 2)
    {
        return false;
    }
    const math::vec2 first = graph.nodes.front().position;
    return std::all_of(graph.nodes.begin(),
                       graph.nodes.end(),
                       [&](const material_graph_node& node)
                       {
                           const math::vec2 delta = node.position - first;
                           return std::fabs(delta.x) < OVERLAP_DISTANCE && std::fabs(delta.y) < OVERLAP_DISTANCE;
                       });
}

auto is_material_graph_input_split(const material_graph_node& node, hpp::string_view pin) -> bool
{
    return std::find(node.split_inputs.begin(), node.split_inputs.end(), pin) != node.split_inputs.end();
}

auto is_material_graph_output_split(const material_graph_node& node, hpp::string_view pin) -> bool
{
    return std::find(node.split_outputs.begin(), node.split_outputs.end(), pin) != node.split_outputs.end();
}

void set_material_graph_pin_split(material_graph_node& node, hpp::string_view pin, bool is_output, bool split)
{
    auto& list = is_output ? node.split_outputs : node.split_inputs;
    const bool was_split = std::find(list.begin(), list.end(), pin) != list.end();
    if(was_split == split)
    {
        return;
    }
    if(split)
    {
        list.emplace_back(pin);
    }
    else
    {
        list.erase(std::remove(list.begin(), list.end(), pin), list.end());
    }
    if(is_output)
    {
        return;
    }
    if(split)
    {
        if(auto* input = find_material_graph_input(node, pin))
        {
            input->link.clear();
        }
        return;
    }
    node.inputs.erase(std::remove_if(node.inputs.begin(),
                                     node.inputs.end(),
                                     [&](const material_graph_input& input)
                                     {
                                         const auto parsed = parse_material_graph_pin(input.pin);
                                         return parsed.pin == pin && parsed.component >= 0;
                                     }),
                      node.inputs.end());
}

namespace
{
/// Marks the output a one-component link reads split, when the output has a fixed vector width.
auto split_read_output(material_graph& graph, const material_graph_link_end& end) -> bool
{
    const int source = find_material_graph_node(graph, end.node);
    const auto* def = source >= 0 ? find_material_node_def(graph.nodes[size_t(source)].type) : nullptr;
    if(end.components.size() != 1 || !def || def->outputs.empty())
    {
        return false;
    }
    const auto* output = end.pin.empty() ? &def->outputs.front() : find_material_pin_def(def->outputs, end.pin);
    auto& node = graph.nodes[size_t(source)];
    if(!output || !can_split_material_pin(*output) || is_material_graph_output_split(node, output->name))
    {
        return false;
    }
    node.split_outputs.push_back(output->name);
    return true;
}
} // namespace

auto split_material_graph_component_pins(material_graph& graph) -> bool
{
    bool changed = false;
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        for(size_t entry = 0; entry < graph.nodes[i].inputs.size(); ++entry)
        {
            const std::string link = graph.nodes[i].inputs[entry].link;
            const auto pin = parse_material_graph_pin(graph.nodes[i].inputs[entry].pin);
            auto& node = graph.nodes[i];
            if(pin.component >= 0 && !is_material_graph_input_split(node, pin.pin))
            {
                node.split_inputs.push_back(pin.pin);
                changed = true;
            }
            if(!link.empty())
            {
                changed = split_read_output(graph, parse_material_graph_link(link)) || changed;
            }
        }
    }
    return changed;
}

auto copy_material_graph_nodes(const material_graph& graph, const std::vector<std::string>& ids) -> material_graph
{
    material_graph fragment;
    for(const auto& node : graph.nodes)
    {
        if(!is_material_graph_output_type(node.type) && std::find(ids.begin(), ids.end(), node.id) != ids.end())
        {
            fragment.nodes.push_back(node);
        }
    }
    return fragment;
}

namespace
{
/// Points a pasted input's link at the pasted copy of its node, keeps it when the graph has the node, clears it
/// otherwise.
void remap_pasted_link(const material_graph& graph, const std::map<std::string, std::string>& renamed, material_graph_input& input)
{
    if(input.link.empty())
    {
        return;
    }
    const auto end = parse_material_graph_link(input.link);
    const auto it = renamed.find(end.node);
    if(it != renamed.end())
    {
        input.link = make_material_graph_link(it->second, end.pin, end.components);
    }
    else if(find_material_graph_node(graph, end.node) < 0)
    {
        input.link.clear();
    }
}

void merge_pasted_output(material_graph& graph,
                         const material_graph_node& pasted,
                         const std::map<std::string, std::string>& renamed)
{
    ensure_material_graph_output(graph);
    const int index = find_material_graph_output(graph);
    // A material function has no Surface Output to take the pasted one's links.
    if(index < 0)
    {
        return;
    }
    // A whole graph of another domain pasted over an output nothing feeds yet takes the pasted graph's domain.
    const auto pasted_domain = find_material_domain_of_output_type(pasted.type);
    if(pasted_domain && graph.nodes[size_t(index)].inputs.empty() && graph.nodes[size_t(index)].settings.empty())
    {
        set_material_graph_domain(graph, *pasted_domain);
    }
    auto& output = graph.nodes[size_t(index)];
    for(auto input : pasted.inputs)
    {
        remap_pasted_link(graph, renamed, input);
        get_or_add_material_graph_input(output, input.pin, input.value) = input;
    }
    for(const auto& setting : pasted.settings)
    {
        set_material_graph_setting(output, setting.name, setting.value);
    }
}
} // namespace

auto paste_material_graph_nodes(material_graph& graph, const material_graph& fragment, const math::vec2& offset)
    -> std::vector<std::string>
{
    std::map<std::string, std::string> renamed;
    std::vector<std::string> added;
    const size_t first = graph.nodes.size();
    for(const auto& node : fragment.nodes)
    {
        if(is_material_graph_output_type(node.type))
        {
            continue;
        }
        const std::string id = make_unique_material_graph_node_id(graph, node.id.empty() ? node.type : node.id);
        auto& copy = graph.nodes.emplace_back(node);
        copy.id = id;
        copy.position += offset;
        renamed[node.id] = id;
        added.push_back(id);
    }
    for(size_t i = first; i < graph.nodes.size(); ++i)
    {
        for(auto& input : graph.nodes[i].inputs)
        {
            remap_pasted_link(graph, renamed, input);
        }
    }
    for(const auto& node : fragment.nodes)
    {
        if(is_material_graph_output_type(node.type))
        {
            merge_pasted_output(graph, node, renamed);
        }
    }
    return added;
}

} // namespace unravel
