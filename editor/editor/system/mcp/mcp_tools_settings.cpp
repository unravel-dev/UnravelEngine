#include "mcp_meta_properties.h"
#include "mcp_profile_utils.h"
#include "mcp_tools_common.h"

#include <editor/system/project_manager.h>
#include <engine/layers/layer_mask.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/settings/settings.h>
#include <deploy/profile.h>
#include <reflection/reflection.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <hpp/string_view.hpp>
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
    hpp::string_view group;
    hpp::string_view name;
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
        {"graphics", "skinned_mesh_batching"},
        {"graphics", "shadow_map_caching"},
        {"graphics", "render_scale"},
        {"graphics", "limits"},
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
        {"resolution", "orientation"},
        {"global_illumination", "global_illumination_quality"},
        {"global_illumination", "reflection_quality"},
        {"global_illumination", "surface_cache_atlas_size"},
        {"global_illumination", "hi_res_reflection_pages"},
        {"global_illumination", "card_captures_per_frame"},
        {"global_illumination", "card_capture_atlas_size"},
        {"global_illumination", "card_capture_refresh_fraction"},
        {"global_illumination", "card_texel_density_scale"},
        {"global_illumination", "card_max_texel_density"},
        {"global_illumination", "card_max_resolution"},
        {"global_illumination", "card_min_resolution"},
        {"global_illumination", "mesh_cards_min_size"},
    };
    return rows;
}

/// A setting whose useful form differs from its stored one, so the reflection alone cannot
/// express it: a per-platform boot value, a choice by name from the project's own list, and an
/// array with engine-owned slots.
struct custom_row
{
    hpp::string_view group;
    hpp::string_view name;
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
         .schema = fmt::format(R"("type":"array","items":{{"type":"string"}},"description":"The 32 layer names physics )"
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
                 hpp::string_view name;
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

auto group_property(hpp::string_view group) -> entt::meta_data
{
    return find_meta_property(entt::resolve<settings>(), group);
}

/// Splits "group.name" or "group.name.field...": `field` is what follows the property's name, empty for none.
/// Returns false for a path without a group and a name, or with an empty part.
auto split_property_path(hpp::string_view path, hpp::string_view& group, hpp::string_view& name, hpp::string_view& field)
    -> bool
{
    const auto dot = path.find('.');
    if(dot == hpp::string_view::npos || dot == 0 || dot + 1 >= path.size() || path.back() == '.' ||
       path.find("..") != hpp::string_view::npos)
    {
        return false;
    }
    group = path.substr(0, dot);
    const auto rest = path.substr(dot + 1);
    const auto field_dot = rest.find('.');
    name = rest.substr(0, field_dot);
    field = field_dot == hpp::string_view::npos ? hpp::string_view{} : rest.substr(field_dot + 1);
    return true;
}

/**
 * @brief The values settings_set writes into one settings group, nested as the group's properties merge them:
 * "limits.bloom" and "limits.soft_shadows" become {"limits":{"bloom":..,"soft_shadows":..}}.
 */
class settings_value_tree
{
public:
    /// Adds the JSON `value` at the dotted `path`; false when a value and a field inside it are both set.
    auto insert(hpp::string_view path, std::string value) -> bool
    {
        if(path.empty())
        {
            if(!children_.empty())
            {
                return false;
            }
            value_ = std::move(value);
            return true;
        }
        if(!value_.empty())
        {
            return false;
        }
        const auto dot = path.find('.');
        auto& child = children_[std::string(path.substr(0, dot))];
        return child.insert(dot == hpp::string_view::npos ? hpp::string_view{} : path.substr(dot + 1), std::move(value));
    }

    auto to_json() const -> std::string
    {
        if(children_.empty())
        {
            return value_;
        }
        std::string json = "{";
        for(const auto& [key, child] : children_)
        {
            json += (json.size() > 1 ? "," : "") + make_json_string(key) + ":" + child.to_json();
        }
        return json + "}";
    }

private:
    std::string value_;
    std::map<std::string, settings_value_tree> children_;
};

auto find_reflected_row(hpp::string_view group, hpp::string_view name) -> const reflected_row*
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

auto reflected_names_in(hpp::string_view group) -> std::unordered_set<std::string>
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
    auto note = [&](hpp::string_view group)
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
    hpp::string_view single;
    if(args["group"].get_string().get(single) == simdjson::SUCCESS)
    {
        groups.emplace_back(single);
    }
    simdjson::dom::array items;
    if(args["groups"].get_array().get(items) == simdjson::SUCCESS)
    {
        for(auto item : items)
        {
            hpp::string_view value;
            if(item.get_string().get(value) == simdjson::SUCCESS)
            {
                groups.emplace_back(value);
            }
        }
    }
    return groups;
}

auto wants_group(const std::vector<std::string>& groups, hpp::string_view group) -> bool
{
    return groups.empty() || std::find(groups.begin(), groups.end(), group) != groups.end();
}


/// The paths of `paths` that the editor's preview profile overrides: a project settings write of them does not
/// show in the editor while it previews the profile.
auto find_previewed_overrides(const project_manager& pm, const std::vector<std::string>& paths) -> std::vector<std::string>
{
    const auto* previewed = pm.get_profiles().find(pm.get_preview_profile());
    std::vector<std::string> shadowed;
    if(previewed == nullptr)
    {
        return shadowed;
    }
    std::copy_if(paths.begin(),
                 paths.end(),
                 std::back_inserter(shadowed),
                 [previewed](const std::string& path)
                 {
                     return previewed->settings.is_related(to_profile_path(path));
                 });
    return shadowed;
}

} // namespace

void register_settings_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "settings_list_properties",
         .description =
             "List the project settings this server exposes: dotted path, type, range, enum values, description, "
             "whether it is read-only, whether a change needs an editor restart (cold), and whether a settings profile "
             "may override it (per_profile). Every profile shares the layers, the input actions, the physics backend and "
             "the Game view's resolutions. Optional group filter.",
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
             auto add_row = [&](hpp::string_view group, hpp::string_view name, bool cold, const std::string& fields)
             {
                 if(!first)
                 {
                     json += ",";
                 }
                 first = false;
                 const auto path = fmt::format("{}.{}", group, name);
                 json += fmt::format(R"({{"path":{},"cold":{},"per_profile":{},{}}})",
                                     make_json_string(path),
                                     bool_to_json(cold),
                                     bool_to_json(deployment::is_overridable_settings_path(to_profile_path(path))),
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
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "settings_get",
         .description = "Read exposed project settings, grouped by section. Optional group / groups "
                        "filter. See settings_list_properties for the schema. With profile, the settings that profile "
                        "runs with (the project settings with its overrides) and overridden: the paths it overrides.",
         .input_schema_json =
             R"json({"type":"object","properties":{"group":{"type":"string"},"groups":{"type":"array","items":{"type":"string"}},"profile":{"type":"string","description":"A settings profile (settings_profiles_list); omitted: the project settings"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             std::string profile;
             if(!read_profile_argument(*pm, args, "profile", profile, error))
             {
                 return {.text = error, .is_error = true};
             }
             auto current = profile.empty() ? pm->get_settings() : pm->get_profile_settings(profile);
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
             if(!profile.empty())
             {
                 const auto* found = pm->get_profiles().find(profile);
                 return {.text = fmt::format(R"({{"profile":{},"settings":{},"overridden":{}}})",
                                             make_json_string(profile),
                                             json,
                                             strings_to_json_array(to_dotted_paths(found->settings.get_paths()))),
                         .is_error = false};
             }
             return {.text = json, .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "settings_set",
         .description =
             "Write project settings and save them. `properties` maps dotted paths to values, e.g. "
             "{\"standalone.startup_scene\":\"app:/data/Main.spfb\",\"physics.solver_iterations\":6}; ranges and "
             "enum names as settings_list_properties lists them. A path may go into a property's fields, as "
             "settings_get lists them (\"graphics.limits.bloom\":false), or set a property whole with an object "
             "(\"graphics.limits\":{\"bloom\":false}): an object sets only the fields it names. All-or-nothing: "
             "nothing is written when any entry is rejected. Cold settings are stored but need an editor restart to "
             "take effect. With profile, the values become overrides of that profile (overridden lists them; "
             "settings_revert removes them), and settings every profile shares (per_profile false) are refused. "
             "Without profile, shadowed lists the paths the editor's preview profile overrides: the editor shows the "
             "profile's values for them until it previews the project settings again.",
         .input_schema_json =
             R"json({"type":"object","properties":{"properties":{"type":"object"},"profile":{"type":"string","description":"A settings profile to override the values in; omitted: the project settings"}},"required":["properties"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_editable_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::object properties;
             if(args["properties"].get_object().get(properties) != simdjson::SUCCESS)
             {
                 return {.text = "Pass properties as an object of dotted path -> value", .is_error = true};
             }

             std::string profile;
             if(!read_profile_argument(*pm, args, "profile", profile, error))
             {
                 return {.text = error, .is_error = true};
             }
             // Everything lands on a copy first: a half-applied settings file is worse than a
             // rejected call, and the caller can fix every entry in one go.
             settings candidate = profile.empty() ? pm->get_settings() : pm->get_profile_settings(profile);
             const auto custom = custom_rows(candidate);
             std::vector<std::string> applied;
             std::vector<std::string> unknown;
             std::vector<std::string> errors;
             std::vector<std::string> restart_required;
             std::map<std::string, settings_value_tree> per_group;
             std::map<std::string, std::vector<std::string>> group_paths;

             for(auto field : properties)
             {
                 const std::string path(field.key);
                 hpp::string_view group;
                 hpp::string_view name;
                 hpp::string_view field_path;
                 if(!split_property_path(path, group, name, field_path))
                 {
                     errors.push_back(path + ": expected a \"group.name\" path, or \"group.name.field\" for a field of a property.");
                     continue;
                 }
                 if(!profile.empty() && !deployment::is_overridable_settings_path(to_profile_path(path)))
                 {
                     errors.push_back(path + ": every profile shares this setting. Set it without profile.");
                     continue;
                 }
                 const auto custom_it = std::find_if(custom.begin(),
                                                     custom.end(),
                                                     [&](const custom_row& row)
                                                     {
                                                         return row.group == group && row.name == name;
                                                     });
                 if(custom_it != custom.end() && !field_path.empty())
                 {
                     errors.push_back(path + ": set " + std::string(group) + "." + std::string(name) + " whole.");
                     continue;
                 }
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
                 const std::string property_path =
                     field_path.empty() ? std::string(name) : std::string(name) + "." + std::string(field_path);
                 if(!per_group[std::string(group)].insert(property_path, std::string(simdjson::minify(field.value))))
                 {
                     errors.push_back(path + ": a property and a field inside it are both set; set one of them.");
                     continue;
                 }
                 group_paths[std::string(group)].push_back(path);
                 if(row->cold)
                 {
                     restart_required.push_back(path);
                 }
             }

             auto candidate_object = entt::forward_as_meta(candidate);
             for(const auto& [group, values] : per_group)
             {
                 simdjson::dom::parser parser;
                 simdjson::dom::object group_properties;
                 const std::string group_json = values.to_json();
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
                 if(result.ok)
                 {
                     const auto& paths = group_paths[group];
                     applied.insert(applied.end(), paths.begin(), paths.end());
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

             if(!profile.empty())
             {
                 const auto overridden = pm->override_profile_settings(ctx, profile, candidate);
                 if(!overridden.is_ok())
                 {
                     return {.text = overridden.error, .is_error = true};
                 }
                 const auto* found = pm->get_profiles().find(profile);
                 return {.text = fmt::format(R"({{"ok":true,"profile":{},"applied":{},"overridden":{},"restart_required":{}}})",
                                             make_json_string(profile),
                                             strings_to_json_array(applied),
                                             strings_to_json_array(to_dotted_paths(found->settings.get_paths())),
                                             strings_to_json_array(restart_required)),
                         .is_error = false};
             }
             pm->get_settings() = std::move(candidate);
             pm->save_project_settings(ctx);

             const auto shadowed = find_previewed_overrides(*pm, applied);
             return {.text = fmt::format(R"({{"ok":true,"applied":{},"restart_required":{},"shadowed":{}{}}})",
                                         strings_to_json_array(applied),
                                         strings_to_json_array(restart_required),
                                         strings_to_json_array(shadowed),
                                         shadowed.empty()
                                             ? std::string{}
                                             : fmt::format(R"(,"note":{})",
                                                           make_json_string(fmt::format(
                                                               "The editor previews profile {}, which overrides the shadowed "
                                                               "paths, so it shows that profile's values for them.",
                                                               pm->get_preview_profile())))),
                     .is_error = false};
         },
         .mutates_scene = true,
         .effect = tool_effect::destructive,
         .idempotent = true});

    registry.add(
        {.name = "settings_revert",
         .description = "Stop a settings profile overriding project settings: each dotted path (a property such as "
                        "\"graphics.particles\" or \"assets.texture\", or deeper) goes back to the project's value, with "
                        "everything inside it. A path inside an overridden list reverts the whole list. reverted lists the paths "
                        "reverted; paths the profile does not override come back in not_overridden, and nothing is written "
                        "for them.",
         .input_schema_json =
             R"json({"type":"object","properties":{"profile":{"type":"string"},"paths":{"type":"array","items":{"type":"string"}}},"required":["profile","paths"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_editable_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             std::string profile;
             if(!read_profile_argument(*pm, args, "profile", profile, error) || profile.empty())
             {
                 return {.text = error.empty() ? "Pass the profile to revert in." : error, .is_error = true};
             }
             simdjson::dom::element paths_element;
             std::vector<std::string> paths;
             if(args["paths"].get(paths_element) != simdjson::SUCCESS || !parse_string_array(paths_element, paths, error, "paths"))
             {
                 return {.text = error.empty() ? "Pass paths as an array of dotted paths" : error, .is_error = true};
             }
             std::vector<std::string> reverted;
             std::vector<std::string> not_overridden;
             for(const auto& path : paths)
             {
                 const auto profile_path = to_profile_path(path);
                 if(!pm->get_profiles().find(profile)->settings.is_related(profile_path))
                 {
                     not_overridden.push_back(path);
                     continue;
                 }
                 const auto result = pm->revert_profile_settings(ctx, profile, profile_path);
                 if(!result.is_ok())
                 {
                     return {.text = fmt::format(R"({{"ok":false,"error":{},"failed":{},"reverted":{}}})",
                                                 make_json_string(result.error),
                                                 make_json_string(path),
                                                 strings_to_json_array(reverted)),
                             .is_error = true};
                 }
                 reverted.push_back(path);
             }
             const auto* found = pm->get_profiles().find(profile);
             return {.text = fmt::format(R"({{"ok":true,"profile":{},"reverted":{},"not_overridden":{},"overridden":{}}})",
                                         make_json_string(profile),
                                         strings_to_json_array(reverted),
                                         strings_to_json_array(not_overridden),
                                         strings_to_json_array(to_dotted_paths(found->settings.get_paths()))),
                     .is_error = false};
         },
         .mutates_scene = true,
         .effect = tool_effect::destructive,
         .idempotent = true});
}

} // namespace unravel::mcp
