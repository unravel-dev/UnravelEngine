#include "mcp_meta_properties.h"
#include "mcp_tools_common.h"

#include <editor/system/project_manager.h>
#include <engine/layers/layer_mask.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/settings/settings.h>
#include <reflection/reflection.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace unravel::mcp
{
namespace
{

/// A project setting exposed through the reflection: the value, its range, its enum names and
/// its description all come from the settings reflection the Project Settings window draws.
struct reflected_row
{
    std::string_view group;
    std::string_view name;
    /// Stored immediately, but only applied at process start.
    bool cold{false};
};

/// The curated part of the surface: which settings an agent sees. Not how they are read or
/// written - that is the reflection's job.
auto reflected_rows() -> const std::vector<reflected_row>&
{
    static const std::vector<reflected_row> rows{
        {"standalone", "startup_scene"},
        {"app", "company"},
        {"app", "product"},
        {"app", "version"},
        {"graphics", "static_mesh_batching"},
        {"graphics", "particles"},
        {"physics", "backend", true},
        {"physics", "fixed_timestep"},
        {"physics", "max_fixed_steps"},
        {"physics", "solver_iterations"},
        {"splash", "enabled"},
        {"splash", "show_made_with"},
        {"splash", "fade_in_sec"},
        {"splash", "fade_out_sec"},
        {"assets", "texture"},
    };
    return rows;
}

/// A setting whose useful form differs from its stored one, so the reflection alone cannot
/// express it: a per-platform boot value, a choice by name from the project's own list, and an
/// array with engine-owned slots.
struct custom_row
{
    std::string_view group;
    std::string_view name;
    std::string schema;
    bool cold{false};
    std::function<std::string(rtti::context&, const settings&)> get;
    /// Empty for read-only rows.
    std::function<bool(settings&, const simdjson::dom::element&, std::string&)> set;
};

auto custom_rows(const settings& current) -> std::vector<custom_row>
{
    std::vector<custom_row> rows;

    rows.push_back({.group = "graphics",
                    .name = "renderer",
                    .schema = R"("type":"string","readOnly":true,"description":"Rendering backend preferred on this )"
                              R"(platform. A per-platform boot setting, changed in the editor's Project Settings.")",
                    .cold = true,
                    .get = [](rtti::context&, const settings& s) -> std::string
                    {
                        return make_json_string(
                            std::string(preferred_renderer_to_string(s.graphics.renderer.get_for_current_platform())));
                    }});

    std::vector<std::string> resolution_names;
    for(const auto& entry : current.resolution.resolutions)
    {
        resolution_names.push_back(entry.name);
    }
    rows.push_back({.group = "resolution",
                    .name = "current",
                    .schema = fmt::format(R"("type":"string","enum":{},"description":"Named resolution or )"
                                          R"(aspect the game window targets.")",
                                          strings_to_json_array(resolution_names)),
                    .get = [](rtti::context&, const settings& s) -> std::string
                    {
                        return make_json_string(s.resolution.current_resolution.name);
                    },
                    .set = [](settings& s, const simdjson::dom::element& value, std::string& error) -> bool
                    {
                        std::string name;
                        if(!parse_string(value, name, error))
                        {
                            return false;
                        }
                        const auto& list = s.resolution.resolutions;
                        const auto it = std::find_if(list.begin(),
                                                     list.end(),
                                                     [&](const settings::resolution_settings::resolution& entry)
                                                     {
                                                         return entry.name == name;
                                                     });
                        if(it == list.end())
                        {
                            error = "unknown resolution name: " + name;
                            return false;
                        }
                        s.resolution.set_current_resolution_index(static_cast<size_t>(std::distance(list.begin(), it)));
                        return true;
                    }});

    const auto reserved_count = get_reserved_layers().size();
    rows.push_back(
        {.group = "layer",
         .name = "layers",
         .schema = fmt::format(R"("type":"array","items":"string","description":"The 32 layer names physics )"
                               R"(queries and rendering filter on. The first {} are engine-owned and cannot be )"
                               R"(renamed; a shorter array leaves later slots untouched.")",
                               reserved_count),
         .get = [](rtti::context&, const settings& s) -> std::string
         {
             return strings_to_json_array(std::vector<std::string>(s.layer.layers.begin(), s.layer.layers.end()));
         },
         .set = [reserved_count](settings& s, const simdjson::dom::element& value, std::string& error) -> bool
         {
             simdjson::dom::array items;
             if(value.get_array().get(items) != simdjson::SUCCESS)
             {
                 error = "expected an array of layer names";
                 return false;
             }
             if(items.size() > s.layer.layers.size())
             {
                 error = fmt::format("at most {} layer names", s.layer.layers.size());
                 return false;
             }
             size_t index = 0;
             for(auto item : items)
             {
                 std::string_view name;
                 if(item.get_string().get(name) != simdjson::SUCCESS)
                 {
                     error = fmt::format("layer {} is not a string", index);
                     return false;
                 }
                 if(index < reserved_count && name != s.layer.layers[index])
                 {
                     error = fmt::format("layer {} (\"{}\") is engine-owned and cannot be renamed",
                                         index,
                                         s.layer.layers[index]);
                     return false;
                 }
                 s.layer.layers[index] = std::string(name);
                 ++index;
             }
             return true;
         }});

    return rows;
}

auto group_property(std::string_view group) -> entt::meta_data
{
    return find_meta_property(entt::resolve<settings>(), group);
}

/// Splits "group.name". Returns false when there is not exactly one dot.
auto split_property_path(std::string_view path, std::string_view& group, std::string_view& name) -> bool
{
    const auto dot = path.find('.');
    if(dot == std::string_view::npos || dot == 0 || dot + 1 >= path.size())
    {
        return false;
    }
    group = path.substr(0, dot);
    name = path.substr(dot + 1);
    return name.find('.') == std::string_view::npos;
}

auto find_reflected_row(std::string_view group, std::string_view name) -> const reflected_row*
{
    for(const auto& row : reflected_rows())
    {
        if(row.group == group && row.name == name)
        {
            return &row;
        }
    }
    return nullptr;
}

auto reflected_names_in(std::string_view group) -> std::unordered_set<std::string>
{
    std::unordered_set<std::string> names;
    for(const auto& row : reflected_rows())
    {
        if(row.group == group)
        {
            names.emplace(row.name);
        }
    }
    return names;
}

/// Groups in first-appearance order across both row kinds, so settings_get reads naturally.
auto group_order(const std::vector<custom_row>& custom) -> std::vector<std::string>
{
    std::vector<std::string> order;
    auto note = [&](std::string_view group)
    {
        if(std::find(order.begin(), order.end(), group) == order.end())
        {
            order.emplace_back(group);
        }
    };
    for(const auto& row : reflected_rows())
    {
        note(row.group);
    }
    for(const auto& row : custom)
    {
        note(row.group);
    }
    return order;
}

auto collect_groups(const simdjson::dom::object& args) -> std::vector<std::string>
{
    std::vector<std::string> groups;
    std::string_view single;
    if(args["group"].get_string().get(single) == simdjson::SUCCESS)
    {
        groups.emplace_back(single);
    }
    simdjson::dom::array items;
    if(args["groups"].get_array().get(items) == simdjson::SUCCESS)
    {
        for(auto item : items)
        {
            std::string_view value;
            if(item.get_string().get(value) == simdjson::SUCCESS)
            {
                groups.emplace_back(value);
            }
        }
    }
    return groups;
}

auto wants_group(const std::vector<std::string>& groups, std::string_view group) -> bool
{
    return groups.empty() || std::find(groups.begin(), groups.end(), group) != groups.end();
}

} // namespace

void register_settings_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "settings_list_properties",
         .description =
             "List the project settings this server exposes: dotted path, type, range, enum values, "
             "description (from the settings reflection the Project Settings window draws), whether "
             "it is read-only, and whether a change needs an editor restart (cold). Optional group "
             "filter (standalone, app, graphics, physics, splash, assets, resolution, layer).",
         .input_schema_json =
             R"json({"type":"object","properties":{"group":{"type":"string"},"groups":{"type":"array","items":{"type":"string"}}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             const auto groups = collect_groups(args);
             std::string json = "[";
             bool first = true;
             auto add_row = [&](std::string_view group, std::string_view name, bool cold, const std::string& fields)
             {
                 if(!first)
                 {
                     json += ",";
                 }
                 first = false;
                 json += fmt::format(R"({{"path":{},"cold":{},{}}})",
                                     make_json_string(fmt::format("{}.{}", group, name)),
                                     bool_to_json(cold),
                                     fields);
             };
             for(const auto& row : reflected_rows())
             {
                 if(!wants_group(groups, row.group))
                 {
                     continue;
                 }
                 const auto prop = find_meta_property(group_property(row.group).type(), row.name);
                 if(prop)
                 {
                     add_row(row.group, row.name, row.cold, meta_property_schema(prop));
                 }
             }
             for(const auto& row : custom_rows(pm->get_settings()))
             {
                 if(wants_group(groups, row.group))
                 {
                     add_row(row.group, row.name, row.cold, row.schema);
                 }
             }
             json += "]";
             return {.text = fmt::format(R"({{"properties":{},"cold_note":"A cold setting is stored )"
                                         R"(immediately but only takes effect after an editor restart."}})",
                                         json),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "settings_get",
         .description = "Read exposed project settings, grouped by section. Optional group / groups "
                        "filter. See settings_list_properties for the schema.",
         .input_schema_json =
             R"json({"type":"object","properties":{"group":{"type":"string"},"groups":{"type":"array","items":{"type":"string"}}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             auto& current = pm->get_settings();
             auto settings_object = entt::forward_as_meta(current);
             const auto groups = collect_groups(args);
             const auto custom = custom_rows(current);

             std::string json = "{";
             bool first_group = true;
             for(const auto& group : group_order(custom))
             {
                 if(!wants_group(groups, group))
                 {
                     continue;
                 }
                 std::string group_json = "{}";
                 const auto names = reflected_names_in(group);
                 if(!names.empty())
                 {
                     auto group_object = group_property(group).get(settings_object);
                     group_json = meta_properties_to_json(ctx, group_object, &names);
                 }
                 for(const auto& row : custom)
                 {
                     if(row.group != group)
                     {
                         continue;
                     }
                     const auto entry = make_json_string(std::string(row.name)) + ":" + row.get(ctx, current);
                     group_json = group_json.size() <= 2 ? "{" + entry + "}"
                                                         : group_json.substr(0, group_json.size() - 1) + "," + entry + "}";
                 }
                 if(!first_group)
                 {
                     json += ",";
                 }
                 first_group = false;
                 json += make_json_string(group) + ":" + group_json;
             }
             json += "}";
             return {.text = json, .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "settings_set",
         .description =
             "Write project settings and save them. `properties` maps dotted paths to values, e.g. "
             "{\"standalone.startup_scene\":\"app:/data/Main.spfb\",\"physics.solver_iterations\":6}. "
             "Values go through the same reflection the Project Settings window uses, so its ranges "
             "and enum names apply. All-or-nothing: nothing is written when any entry is rejected. "
             "Cold settings are stored but need an editor restart to take effect.",
         .input_schema_json =
             R"json({"type":"object","properties":{"properties":{"type":"object"}},"required":["properties"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::object properties;
             if(args["properties"].get_object().get(properties) != simdjson::SUCCESS)
             {
                 return {.text = "Pass properties as an object of dotted path -> value", .is_error = true};
             }

             // Everything lands on a copy first: a half-applied settings file is worse than a
             // rejected call, and the caller can fix every entry in one go.
             settings candidate = pm->get_settings();
             const auto custom = custom_rows(candidate);
             std::vector<std::string> applied;
             std::vector<std::string> unknown;
             std::vector<std::string> errors;
             std::vector<std::string> restart_required;
             std::map<std::string, std::string> per_group;

             for(auto field : properties)
             {
                 const std::string path(field.key);
                 std::string_view group;
                 std::string_view name;
                 if(!split_property_path(path, group, name))
                 {
                     errors.push_back(path + ": expected a \"group.name\" path");
                     continue;
                 }
                 const auto custom_it = std::find_if(custom.begin(),
                                                     custom.end(),
                                                     [&](const custom_row& row)
                                                     {
                                                         return row.group == group && row.name == name;
                                                     });
                 if(custom_it != custom.end())
                 {
                     std::string field_error;
                     if(!custom_it->set)
                     {
                         errors.push_back(path + ": read-only");
                     }
                     else if(!custom_it->set(candidate, field.value, field_error))
                     {
                         errors.push_back(path + ": " + field_error);
                     }
                     else
                     {
                         applied.push_back(path);
                         if(custom_it->cold)
                         {
                             restart_required.push_back(path);
                         }
                     }
                     continue;
                 }
                 const auto* row = find_reflected_row(group, name);
                 if(row == nullptr)
                 {
                     unknown.push_back(path);
                     continue;
                 }
                 auto& entries = per_group[std::string(group)];
                 entries += (entries.empty() ? "" : ",") + make_json_string(std::string(name)) + ":" +
                            std::string(simdjson::minify(field.value));
                 if(row->cold)
                 {
                     restart_required.push_back(path);
                 }
             }

             auto candidate_object = entt::forward_as_meta(candidate);
             for(const auto& [group, entries] : per_group)
             {
                 simdjson::dom::parser parser;
                 simdjson::dom::object group_properties;
                 const std::string group_json = "{" + entries + "}";
                 if(parser.parse(group_json).get(group_properties))
                 {
                     errors.push_back(group + ": could not re-read the values");
                     continue;
                 }
                 const auto prop = group_property(group);
                 auto group_object = prop.get(candidate_object);
                 const auto allowed = reflected_names_in(group);
                 component_apply_result result;
                 apply_meta_properties(ctx, group_object, group_properties, result, &allowed);
                 for(const auto& message : result.errors)
                 {
                     if(message.rfind("unknown properties:", 0) != 0)
                     {
                         errors.push_back(group + "." + message);
                     }
                 }
                 for(const auto& key : result.unknown)
                 {
                     unknown.push_back(group + "." + key);
                 }
                 if(result.ok && !prop.set(candidate_object, group_object))
                 {
                     errors.push_back(group + ": the settings rejected the group");
                 }
                 for(const auto& key : result.applied)
                 {
                     applied.push_back(group + "." + key);
                 }
             }

             if(!errors.empty() || !unknown.empty())
             {
                 return {.text = fmt::format(
                             R"({{"ok":false,"applied":[],"would_apply":{},"unknown":{},"errors":{},)"
                             R"("note":"Nothing was written. Fix the entries in errors/unknown and resend; )"
                             R"(would_apply lists the entries that were already valid."}})",
                             strings_to_json_array(applied),
                             strings_to_json_array(unknown),
                             strings_to_json_array(errors)),
                         .is_error = true};
             }

             pm->get_settings() = std::move(candidate);
             pm->save_project_settings(ctx);

             return {.text = fmt::format(R"({{"ok":true,"applied":{},"restart_required":{}}})",
                                         strings_to_json_array(applied),
                                         strings_to_json_array(restart_required)),
                     .is_error = false};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
