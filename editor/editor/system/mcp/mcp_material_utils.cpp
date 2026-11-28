#include "mcp_material_utils.h"
#include "mcp_asset_lookup.h"
#include "mcp_meta_properties.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include <editor/editing/thumbnail_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/meta/rendering/graph_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/rendering/material_graph/standard_material.h>

#include <functional>
#include <string_view>

namespace unravel::mcp
{
namespace
{

/// @p mat as its most derived reflected type (a graph_material), so the base's properties and its own are walked.
auto material_object(material& mat) -> entt::meta_any
{
    entt::meta_any object = entt::forward_as_meta(mat);
    entt::as_derived(object);
    return object;
}

auto get_standard_kind_name(standard_property::kind kind) -> const char*
{
    switch(kind)
    {
        case standard_property::kind::color:
            return "color";
        case standard_property::kind::scalar:
            return "number";
        case standard_property::kind::vector2:
            return "vector2";
        case standard_property::kind::texture:
            return "texture";
    }
    return "";
}

/// Up to @p max_count numbers from a number or a list of them; false when @p element is neither.
auto read_numbers(const simdjson::dom::element& element, size_t max_count, std::vector<float>& numbers) -> bool
{
    double number = 0.0;
    if(element.get(number) == simdjson::SUCCESS)
    {
        numbers = {float(number)};
        return true;
    }
    simdjson::dom::array list;
    if(element.get(list) != simdjson::SUCCESS || list.size() == 0 || list.size() > max_count)
    {
        return false;
    }
    numbers.clear();
    for(auto entry : list)
    {
        if(entry.get(number) != simdjson::SUCCESS)
        {
            return false;
        }
        numbers.push_back(float(number));
    }
    return true;
}

/// The longest list a Standard property's value is written as: a color's four numbers, the tiling's two, and the
/// dither threshold's two of a PBR material (its alpha threshold, then the distance the Standard graph keeps).
constexpr size_t MAX_COLOR_NUMBERS = 4;
constexpr size_t MAX_PAIR_NUMBERS = 2;
constexpr size_t MIN_COLOR_NUMBERS = 3;
constexpr const char* DITHER_THRESHOLD_KEY = "dither_threshold";

/// A Standard property's value as a tool writes it, as make_standard_parameter_value reads it.
auto read_standard_value(const standard_property& property, const simdjson::dom::element& element, math::vec4& value, std::string& error)
    -> bool
{
    std::vector<float> numbers;
    switch(property.type)
    {
        case standard_property::kind::color:
            if(!read_numbers(element, MAX_COLOR_NUMBERS, numbers) || numbers.size() < MIN_COLOR_NUMBERS)
            {
                error = "a color: a list of 3 or 4 numbers (sRGB, alpha last)";
                return false;
            }
            value = math::vec4(numbers[0], numbers[1], numbers[2], numbers.size() > MIN_COLOR_NUMBERS ? numbers[3] : 1.0f);
            return true;
        case standard_property::kind::vector2:
            if(!read_numbers(element, MAX_PAIR_NUMBERS, numbers) || numbers.size() != MAX_PAIR_NUMBERS)
            {
                error = "a list of 2 numbers";
                return false;
            }
            value = math::vec4(numbers[0], numbers[1], 0.0f, 0.0f);
            return true;
        case standard_property::kind::scalar:
            if(!read_numbers(element, MAX_PAIR_NUMBERS, numbers) ||
               (numbers.size() > 1 && hpp::string_view(property.key) != DITHER_THRESHOLD_KEY))
            {
                error = "a number";
                return false;
            }
            value = math::vec4(numbers.back());
            return true;
        case standard_property::kind::texture:
            break;
    }
    error = "a texture key";
    return false;
}

/// A texture as a tool names it: its key, or "" for the default texture.
auto read_texture_key(rtti::context& ctx, const simdjson::dom::element& element, asset_handle<gfx::texture>& texture, std::string& error)
    -> bool
{
    std::string_view key;
    if(element.get(key) != simdjson::SUCCESS)
    {
        error = "a texture key, or \"\" for the default texture";
        return false;
    }
    texture = {};
    // A key no asset has would register a phantom entry and sample white.
    return key.empty() || static_cast<bool>(texture = find_existing_asset<gfx::texture>(ctx, std::string(key), error));
}

/// Where an edit of a Standard property goes: a material's graph defaults, or an instance's overrides.
struct standard_value_target
{
    std::function<bool(const std::string& name, const math::vec4& value)> set_parameter;
    std::function<bool(const std::string& name, const asset_handle<gfx::texture>& texture)> set_texture;
};

/// Writes the Standard properties among @p properties to @p target; the other keys go to @p others.
void apply_standard_properties(rtti::context& ctx,
                               const simdjson::dom::object& properties,
                               const standard_value_target& target,
                               std::vector<std::string>& others,
                               component_apply_result& result)
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto* property = find_standard_property(key);
        if(property == nullptr)
        {
            others.push_back(key);
            continue;
        }
        std::string error;
        bool is_set = false;
        if(property->type == standard_property::kind::texture)
        {
            asset_handle<gfx::texture> texture;
            is_set = read_texture_key(ctx, field.value, texture, error) && target.set_texture(property->name, texture);
        }
        else
        {
            math::vec4 value{};
            is_set = read_standard_value(*property, field.value, value, error) &&
                     target.set_parameter(property->name, make_standard_parameter_value(*property, value));
        }
        if(!is_set)
        {
            result.ok = false;
            result.errors.push_back(
                fmt::format("{}: {}", key, error.empty() ? fmt::format("the material's graph has no \"{}\"", property->name) : error));
            continue;
        }
        result.applied.push_back(key);
    }
}

/// Writes property @p key of @p properties through the reflection of @p object (a material's cull type).
void apply_reflected_property(rtti::context& ctx,
                              entt::meta_any& object,
                              const simdjson::dom::object& properties,
                              const std::string& key,
                              component_apply_result& result)
{
    const auto prop = find_meta_property(object.type(), key);
    simdjson::dom::element value;
    if(!prop || properties[key].get(value) != simdjson::SUCCESS)
    {
        result.ok = false;
        result.errors.push_back(fmt::format("unknown property: {} (assets_list_properties lists the names)", key));
        return;
    }
    if(prop.is_const())
    {
        result.ok = false;
        result.errors.push_back(fmt::format("{}: read only; a material's sidedness is its graph's two_sided and cull_front_faces settings", key));
        return;
    }
    auto target = prop.get(object);
    std::string error;
    if(!meta_value_from_json(ctx, value, target, error) || !prop.set(object, target))
    {
        result.ok = false;
        result.errors.push_back(fmt::format("{}: {}", key, error.empty() ? "the value does not fit" : error));
        return;
    }
    result.applied.push_back(key);
}

/// Writes @p properties to graph material @p mat: the Standard ones into its graph's defaults, the others through
/// its reflection.
auto write_material_properties(rtti::context& ctx, graph_material& mat, const simdjson::dom::object& properties)
    -> component_apply_result
{
    component_apply_result result;
    material_graph edited = mat.get_graph();
    standard_value_target target;
    target.set_parameter = [&mat, &edited](const std::string& name, const math::vec4& value)
    {
        const auto* parameter = mat.find_parameter(name);
        return parameter != nullptr && set_material_graph_parameter_default(edited, parameter->node_id, value);
    };
    target.set_texture = [&mat, &edited](const std::string& name, const asset_handle<gfx::texture>& texture)
    {
        const auto* found = mat.find_texture(name);
        return found != nullptr && set_material_graph_node_texture(edited, found->node_id, texture);
    };
    std::vector<std::string> others;
    apply_standard_properties(ctx, properties, target, others, result);
    if(edited != mat.get_graph())
    {
        mat.set_graph(std::move(edited));
    }
    auto object = material_object(mat);
    for(const auto& key : others)
    {
        apply_reflected_property(ctx, object, properties, key, result);
    }
    return result;
}

/// A Standard property's value as JSON: a color's four numbers (sRGB), a number, the tiling's two numbers, a texture's
/// key ("" for none); empty when @p mat's graph has no such parameter or texture.
auto standard_value_to_json(const graph_material& mat, const standard_property& property) -> hpp::optional<std::string>
{
    if(property.type == standard_property::kind::texture)
    {
        if(mat.find_texture(property.name) == nullptr)
        {
            return std::nullopt;
        }
        return make_json_string(mat.get_texture(property.name).id());
    }
    const auto parameter = mat.get_parameter(property.name);
    if(!parameter)
    {
        return std::nullopt;
    }
    const auto value = make_standard_property_value(property, *parameter);
    switch(property.type)
    {
        case standard_property::kind::color:
            return fmt::format("[{},{},{},{}]", value.x, value.y, value.z, value.w);
        case standard_property::kind::vector2:
            return fmt::format("[{},{}]", value.x, value.y);
        default:
            return fmt::format("{}", value.x);
    }
}

} // namespace

auto list_material_property_schema_json() -> std::string
{
    std::string properties;
    for(const auto& property : get_standard_properties())
    {
        properties += fmt::format(R"({}{{"key":{},"name":{},"type":{}}})",
                                  properties.empty() ? "" : ",",
                                  make_json_string(property.key),
                                  make_json_string(property.name),
                                  make_json_string(get_standard_kind_name(property.type)));
    }
    const auto type = entt::resolve<graph_material>();
    return fmt::format(R"({{"material_type":{},"standard_properties":[{}],"note":{},{}}})",
                       make_json_string(entt::get_name(type)),
                       properties,
                       make_json_string("Every material is a material graph. The standard properties are the Standard "
                                        "graph's parameters and texture nodes, by their names; colors are sRGB. A graph "
                                        "without one refuses it. material_graph_get lists every parameter by name."),
                       meta_object_schema(type));
}

auto resolve_material_asset(rtti::context& ctx, const std::string& key, const std::string& uid, std::string& error)
    -> asset_handle<material>
{
    if(key.empty() && uid.empty())
    {
        error = "Provide key or uid";
        return {};
    }
    return find_existing_asset<material>(ctx, key.empty() ? uid : to_asset_key(key), error);
}

auto resolve_domain_material(rtti::context& ctx, const std::string& key, material_domain domain, std::string& error)
    -> asset_handle<material>
{
    auto mat = resolve_material_asset(ctx, key, {}, error);
    if(!mat)
    {
        return {};
    }
    const auto loaded = mat.get();
    if(asset_handle<material>::is_empty(loaded) || get_material_domain(*loaded) != domain)
    {
        error = fmt::format("{} is not a {} material: its graph's output node must be {} (material_graph_set).",
                            key,
                            get_material_domain_label(domain),
                            get_material_graph_output_type(domain));
        return {};
    }
    return mat;
}

auto material_type_name(material& mat) -> std::string
{
    return entt::get_name(material_object(mat).type());
}

auto material_to_json(rtti::context& ctx, material& mat) -> std::string
{
    auto object = material_object(mat);
    auto json = meta_properties_to_json(ctx, object);
    const auto* graph = mat.safe_cast<graph_material>();
    if(graph == nullptr)
    {
        return json;
    }
    json.pop_back();
    for(const auto& property : get_standard_properties())
    {
        if(const auto value = standard_value_to_json(*graph, property))
        {
            json += fmt::format("{}{}:{}", json.size() > 1 ? "," : "", make_json_string(property.key), *value);
        }
    }
    return json + "}";
}

auto apply_material_properties(rtti::context& ctx, material& mat, const simdjson::dom::object& properties)
    -> component_apply_result
{
    auto* graph = mat.safe_cast<graph_material>();
    if(graph == nullptr)
    {
        component_apply_result result;
        result.ok = false;
        result.errors.emplace_back("The material is not a material graph.");
        return result;
    }
    // Checked on a copy first: a rejected value leaves the material - shared by every renderer
    // using it - exactly as it was.
    auto candidate = mat.clone();
    auto result = write_material_properties(ctx, *candidate->safe_cast<graph_material>(), properties);
    if(!result.ok)
    {
        return result;
    }
    return write_material_properties(ctx, *graph, properties);
}

namespace
{
/// Writes @p properties to @p instance, over its loaded shared material @p shared.
auto write_material_instance_properties(rtti::context& ctx,
                                        material_instance& instance,
                                        const material& shared,
                                        const simdjson::dom::object& properties) -> component_apply_result
{
    component_apply_result result;
    standard_value_target target;
    target.set_parameter = [&instance](const std::string& name, const math::vec4& value)
    {
        return instance.set_graph_parameter(name, value);
    };
    target.set_texture = [&instance](const std::string& name, const asset_handle<gfx::texture>& texture)
    {
        return instance.set_graph_texture(name, texture);
    };
    std::vector<std::string> others;
    apply_standard_properties(ctx, properties, target, others, result);
    if(!result.ok || others.empty())
    {
        return result;
    }
    // Edited as the Inspector edits a slot: on the shared material with the overrides, and what then differs
    // (setter side effects included) becomes an override.
    auto values = make_instance_values(instance, shared);
    auto values_object = material_object(*values);
    for(const auto& key : others)
    {
        apply_reflected_property(ctx, values_object, properties, key, result);
    }
    if(!result.ok)
    {
        return result;
    }
    const auto changes = override_changed_values(instance, *values);
    for(const auto property : changes.refused)
    {
        const auto data = values_object.type().data(property);
        result.ok = false;
        result.errors.push_back(fmt::format("{}: a material instance cannot override it",
                                            data ? std::string(entt::get_name(data)) : std::to_string(property)));
    }
    return result;
}
} // namespace

auto apply_material_instance_properties(rtti::context& ctx,
                                        material_instance& instance,
                                        const simdjson::dom::object& properties) -> component_apply_result
{
    const auto shared = instance.get_shared_material().get();
    if(!shared || asset_handle<material>::is_empty(shared))
    {
        component_apply_result result;
        result.ok = false;
        result.errors.emplace_back("There is no shared material to override.");
        return result;
    }
    // Checked on a copy first: a rejected value leaves the slot exactly as it was.
    auto candidate = instance;
    auto result = write_material_instance_properties(ctx, candidate, *shared, properties);
    if(!result.ok)
    {
        return result;
    }
    return write_material_instance_properties(ctx, instance, *shared, properties);
}

namespace
{
/// A graph parameter's value as a tool writes it: a number (x), or a list of up to four numbers.
auto read_graph_parameter_value(const simdjson::dom::element& element, math::vec4& value) -> bool
{
    double number = 0.0;
    if(element.get(number) == simdjson::SUCCESS)
    {
        value = math::vec4(float(number), 0.0f, 0.0f, 0.0f);
        return true;
    }
    simdjson::dom::array list;
    if(element.get(list) != simdjson::SUCCESS || list.size() == 0 || list.size() > 4)
    {
        return false;
    }
    value = math::vec4(0.0f);
    int component = 0;
    for(auto entry : list)
    {
        if(entry.get(number) != simdjson::SUCCESS)
        {
            return false;
        }
        value[component++] = float(number);
    }
    return true;
}

void apply_graph_parameters(material_instance& instance, const simdjson::dom::object& parameters, component_apply_result& result)
{
    for(auto field : parameters)
    {
        const std::string name(field.key);
        if(field.value.is_null())
        {
            instance.revert_graph_parameter(name);
            result.applied.push_back("parameters." + name);
            continue;
        }
        math::vec4 value{};
        if(!read_graph_parameter_value(field.value, value))
        {
            result.ok = false;
            result.errors.push_back(fmt::format("parameters.{}: a number or a list of up to four numbers, or null to revert", name));
            continue;
        }
        if(!instance.set_graph_parameter(name, value))
        {
            result.ok = false;
            result.errors.push_back(fmt::format("parameters.{}: the shared material is not a material graph with this parameter", name));
            continue;
        }
        result.applied.push_back("parameters." + name);
    }
}

void apply_graph_textures(rtti::context& ctx, material_instance& instance, const simdjson::dom::object& textures, component_apply_result& result)
{
    for(auto field : textures)
    {
        const std::string name(field.key);
        if(field.value.is_null())
        {
            instance.revert_graph_texture(name);
            result.applied.push_back("textures." + name);
            continue;
        }
        std::string_view key;
        if(field.value.get(key) != simdjson::SUCCESS)
        {
            result.ok = false;
            result.errors.push_back(fmt::format("textures.{}: a texture key, \"\" for the default texture, or null to revert", name));
            continue;
        }
        asset_handle<gfx::texture> texture;
        if(!key.empty())
        {
            // A key no asset has would register a phantom entry and sample white.
            std::string error;
            texture = find_existing_asset<gfx::texture>(ctx, std::string(key), error);
            if(!texture)
            {
                result.ok = false;
                result.errors.push_back(fmt::format("textures.{}: {}", name, error));
                continue;
            }
        }
        if(!instance.set_graph_texture(name, texture))
        {
            result.ok = false;
            result.errors.push_back(fmt::format("textures.{}: the shared material is not a material graph with this texture node", name));
            continue;
        }
        result.applied.push_back("textures." + name);
    }
}
} // namespace

void apply_material_instance_graph_values(rtti::context& ctx,
                                          material_instance& instance,
                                          const simdjson::dom::object* parameters,
                                          const simdjson::dom::object* textures,
                                          component_apply_result& result)
{
    if(parameters != nullptr)
    {
        apply_graph_parameters(instance, *parameters, result);
    }
    if(textures != nullptr)
    {
        apply_graph_textures(ctx, instance, *textures, result);
    }
}

auto save_material_asset(rtti::context& ctx, const asset_handle<material>& handle, std::string& error) -> bool
{
    if(!handle)
    {
        error = "Invalid material handle";
        return false;
    }
    if(!asset_writer::atomic_save_to_file(handle.id(), handle))
    {
        error = "Failed to save material: " + handle.id();
        return false;
    }
    if(ctx.has<thumbnail_manager>())
    {
        ctx.get_cached<thumbnail_manager>().regenerate_thumbnail(handle.uid());
    }
    return true;
}

} // namespace unravel::mcp
