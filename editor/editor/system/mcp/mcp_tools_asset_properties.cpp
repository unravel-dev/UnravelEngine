#include "mcp_asset_lookup.h"
#include "mcp_asset_wait.h"
#include "mcp_material_utils.h"
#include "mcp_meta_properties.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include <editor/assets/asset_actions.h>
#include <editor/system/mcp_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/meta/animation/animation_controller.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/physics/physics_material.h>
#include <engine/rendering/material.h>
#include <filesystem/filesystem.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{

/// Material assets: written through their setters, so renderers using them see the change at once.
struct material_kind
{
    using asset_type = material;
    static constexpr const char* type = "material";

    static auto schema() -> std::string
    {
        return list_material_property_schema_json();
    }

    static auto read(rtti::context& ctx, const asset_handle<material>& handle) -> std::string
    {
        return material_to_json(ctx, *handle.get());
    }

    /// Checks @p properties on a copy of the asset (or of a new material when there is none yet).
    static void check(rtti::context& ctx,
                      const asset_handle<material>& handle,
                      const simdjson::dom::object& properties,
                      component_apply_result& result)
    {
        auto candidate = handle ? handle.get()->clone() : material::sptr(std::make_shared<pbr_material>());
        result = apply_material_properties(ctx, *candidate, properties);
    }

    static void write(rtti::context& ctx,
                      const asset_handle<material>& handle,
                      const simdjson::dom::object& properties,
                      bool save,
                      component_apply_result& result)
    {
        result = apply_material_properties(ctx, *handle.get(), properties);
        std::string error;
        if(result.ok && save && !save_material_asset(ctx, handle, error))
        {
            result.ok = false;
            result.errors.push_back(error);
        }
    }

    static auto create(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<material>
    {
        return asset_actions::create_material(ctx, absolute_path);
    }
};

/// Physics material assets: plain values, written whole once checked, so a body built from one never
/// sees half an edit.
struct physics_material_kind
{
    using asset_type = physics_material;
    static constexpr const char* type = "physics_material";

    static auto schema() -> std::string
    {
        return "{" + meta_object_schema(entt::resolve<physics_material>()) + "}";
    }

    static auto read(rtti::context& ctx, const asset_handle<physics_material>& handle) -> std::string
    {
        auto object = entt::forward_as_meta(*handle.get());
        return meta_properties_to_json(ctx, object);
    }

    static void check(rtti::context& ctx,
                      const asset_handle<physics_material>& handle,
                      const simdjson::dom::object& properties,
                      component_apply_result& result)
    {
        auto candidate = handle ? *handle.get() : physics_material{};
        auto object = entt::forward_as_meta(candidate);
        apply_meta_properties(ctx, object, properties, result);
    }

    static void write(rtti::context& ctx,
                      const asset_handle<physics_material>& handle,
                      const simdjson::dom::object& properties,
                      bool save,
                      component_apply_result& result)
    {
        auto candidate = *handle.get();
        auto object = entt::forward_as_meta(candidate);
        apply_meta_properties(ctx, object, properties, result);
        if(!result.ok)
        {
            return;
        }
        const auto previous = *handle.get();
        *handle.get() = candidate;
        if(save && !asset_writer::atomic_save_to_file(handle.id(), handle))
        {
            *handle.get() = previous;
            result.ok = false;
            result.errors.push_back("Failed to save " + handle.id());
        }
    }

    static auto create(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<physics_material>
    {
        return asset_actions::create_physics_material(ctx, absolute_path);
    }
};

/// Animation controllers: written whole once checked; running instances rebuild on the bumped
/// revision, keeping parameter values and current states by name.
struct animation_controller_kind
{
    using asset_type = animation_controller;
    static constexpr const char* type = "animation_controller";

    static auto schema() -> std::string
    {
        return "{" + meta_object_schema(entt::resolve<animation_controller>()) + "}";
    }

    static auto read(rtti::context& ctx, const asset_handle<animation_controller>& handle) -> std::string
    {
        auto object = entt::forward_as_meta(*handle.get());
        return meta_properties_to_json(ctx, object);
    }

    static void check(rtti::context& ctx,
                      const asset_handle<animation_controller>& handle,
                      const simdjson::dom::object& properties,
                      component_apply_result& result)
    {
        auto candidate = handle ? *handle.get() : animation_controller{};
        auto object = entt::forward_as_meta(candidate);
        apply_meta_properties(ctx, object, properties, result);
    }

    static void write(rtti::context& ctx,
                      const asset_handle<animation_controller>& handle,
                      const simdjson::dom::object& properties,
                      bool save,
                      component_apply_result& result)
    {
        auto candidate = *handle.get();
        auto object = entt::forward_as_meta(candidate);
        apply_meta_properties(ctx, object, properties, result);
        if(!result.ok)
        {
            return;
        }
        const auto previous = *handle.get();
        candidate.revision = previous.revision + 1;
        *handle.get() = candidate;
        if(save && !asset_writer::atomic_save_to_file(handle.id(), handle))
        {
            *handle.get() = previous;
            handle.get()->revision = candidate.revision + 1;
            result.ok = false;
            result.errors.push_back("Failed to save " + handle.id());
        }
    }

    static auto create(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<animation_controller>
    {
        return asset_actions::create_animation_controller(ctx, absolute_path);
    }
};

template<typename... Kinds>
struct kind_list
{
};

using editable_kinds = kind_list<material_kind, physics_material_kind, animation_controller_kind>;

/// Whether the asset at @p key is of @p Kind: by its extension, or - for a key without one, such as an
/// embedded asset - by whether the database knows it as that type.
template<typename Kind>
auto is_kind_of(rtti::context& ctx, const std::string& key) -> bool
{
    using asset_type = typename Kind::asset_type;
    const auto extension = fs::path(key).extension().generic_string();
    if(!extension.empty())
    {
        return matches_asset_extension<asset_type>(extension);
    }
    std::string ignored;
    return static_cast<bool>(find_existing_asset<asset_type>(ctx, key, ignored));
}

/// Calls @p visit with the first kind @p is_match accepts; false when none does.
template<typename Match, typename Visitor, typename Kind, typename... Rest>
auto visit_first_kind(Match&& is_match, kind_list<Kind, Rest...>, Visitor&& visit) -> bool
{
    if(is_match.template operator()<Kind>())
    {
        visit.template operator()<Kind>();
        return true;
    }
    if constexpr(sizeof...(Rest) > 0)
    {
        return visit_first_kind(std::forward<Match>(is_match), kind_list<Rest...>{}, std::forward<Visitor>(visit));
    }
    return false;
}

/// Calls @p visit with the kind named @p type; false when no kind has that name.
template<typename Visitor>
auto visit_kind_named(const std::string& type, Visitor&& visit) -> bool
{
    return visit_first_kind(
        [&type]<typename Kind>()
        {
            return type == Kind::type;
        },
        editable_kinds{},
        std::forward<Visitor>(visit));
}

/// Calls @p visit with the kind of the asset at @p key; false when it is no editable kind.
template<typename Visitor>
auto visit_kind_of(rtti::context& ctx, const std::string& key, Visitor&& visit) -> bool
{
    return visit_first_kind(
        [&ctx, &key]<typename Kind>()
        {
            return is_kind_of<Kind>(ctx, key);
        },
        editable_kinds{},
        std::forward<Visitor>(visit));
}

auto kind_names() -> std::string
{
    return std::string(material_kind::type) + ", " + physics_material_kind::type + ", " + animation_controller_kind::type;
}

/// The asset key an argument names: a uid is looked up, a key or relative path is spelled as a key.
auto asset_reference_key(rtti::context& ctx, const std::string& reference) -> std::string
{
    if(const auto uid = hpp::uuid::from_string(reference))
    {
        return ctx.get_cached<asset_manager>().get_metadata(*uid).location;
    }
    return to_asset_key(reference);
}

/// What a get or set reports for one asset reference that is not an editable asset.
auto unknown_kind_error(const std::string& reference) -> std::string
{
    return "'" + reference + "' is not an asset whose properties MCP edits (" + kind_names() + ")";
}

/// One assets_set_properties_batch item, checked before any is written.
struct set_item
{
    std::string reference;
    std::string key;
    simdjson::dom::object properties;
    /// Why the item cannot be written: a lookup error as a JSON string, or a rejected check as its result.
    std::string error_json;
};

} // namespace

void register_asset_property_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "assets_list_properties",
         .description =
             "The asset types whose properties assets_get/set_properties_batch read and write (material, "
             "physics_material, animation_controller), or with type, that type's properties from its reflection: names, types, "
             "enum values, ranges.",
         .input_schema_json =
             R"json({"type":"object","properties":{"type":{"type":"string","enum":["material","physics_material","animation_controller"]}}})json",
         .handler =
             [](rtti::context&, const simdjson::dom::object& args) -> tool_result
         {
             std::string type;
             read_string(args, "type", type);
             if(type.empty())
             {
                 return {.text = R"({"types":["material","physics_material","animation_controller"]})", .is_error = false};
             }
             std::string schema;
             if(!visit_kind_named(type,
                                  [&schema]<typename Kind>()
                                  {
                                      schema = Kind::schema();
                                  }))
             {
                 return {.text = "type must be one of " + kind_names(), .is_error = true};
             }
             return {.text = schema, .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "assets_get_properties_batch",
         .description = "Read the properties of material, physics material and animation controller assets, by key (or a path "
                        "relative to app:/data) or uid.",
         .input_schema_json =
             R"json({"type":"object","properties":{"assets":{"type":"array","items":{"type":"string"}}},"required":["assets"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::vector<std::string> references;
             std::string error;
             if(!read_string_array(args, "assets", references, error) || references.empty())
             {
                 return {.text = error.empty() ? "Pass assets, a list of keys or uids" : error, .is_error = true};
             }
             std::string results = "[";
             size_t ok_count = 0;
             for(const auto& reference : references)
             {
                 results += results.size() > 1 ? "," : "";
                 const auto key = asset_reference_key(ctx, reference);
                 std::string item_error = unknown_kind_error(reference);
                 std::string item_json;
                 visit_kind_of(ctx,
                               key,
                               [&]<typename Kind>()
                               {
                                auto handle = find_existing_asset<typename Kind::asset_type>(ctx, key, item_error);
                                if(handle && handle.get())
                                {
                                    item_json = fmt::format(R"({{"ok":true,"asset":{},"uid":{},"type":"{}","properties":{}}})",
                                                            make_json_string(handle.id()),
                                                            make_json_string(hpp::to_string(handle.uid())),
                                                            Kind::type,
                                                            Kind::read(ctx, handle));
                                }
                            });
                 if(item_json.empty())
                 {
                     results += fmt::format(R"({{"ok":false,"asset":{},"error":{}}})",
                                            make_json_string(reference),
                                            make_json_string(item_error));
                     continue;
                 }
                 ++ok_count;
                 results += item_json;
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, references.size()),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "assets_set_properties_batch",
         .description =
             "Set properties of the project's material, physics material and animation controller assets (app:/data/..., by key "
             "or uid), as assets_list_properties lists them. Every item is checked before any is written: "
             "one rejected value writes nothing. The assets are shared - every renderer or body using one "
             "changes. Saves each file unless save:false (in memory only).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"asset":{"type":"string"},"properties":{"type":"object"}},"required":["asset","properties"]}},"save":{"type":"boolean","default":true}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             simdjson::dom::array items_arr;
             if(!require_not_play_mode(ctx, error) || !read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             bool save = true;
             read_bool(args, "save", save);
             std::vector<set_item> items;
             for(auto element : items_arr)
             {
                 auto& item = items.emplace_back();
                 simdjson::dom::object fields;
                 if(!read_object(element, fields, error) || !read_string(fields, "asset", item.reference) ||
                    fields["properties"].get_object().get(item.properties) != simdjson::SUCCESS)
                 {
                     item.error_json = make_json_string("Each item needs asset and a properties object");
                     continue;
                 }
                 item.key = asset_reference_key(ctx, item.reference);
                 std::string lookup_error = unknown_kind_error(item.reference);
                 visit_kind_of(ctx,
                               item.key,
                               [&]<typename Kind>()
                               {
                                   auto handle = find_existing_asset<typename Kind::asset_type>(ctx, item.key, lookup_error);
                                   if(!handle || !require_project_data_asset(handle.id(), lookup_error))
                                   {
                                       return;
                                   }
                                   lookup_error.clear();
                                   item.key = handle.id();
                                   component_apply_result check;
                                   Kind::check(ctx, handle, item.properties, check);
                                   item.error_json = check.ok ? std::string{} : component_apply_result_to_json(check);
                               });
                 if(!lookup_error.empty())
                 {
                     item.error_json = make_json_string(lookup_error);
                 }
             }
             const bool is_rejected = std::any_of(items.begin(),
                                                  items.end(),
                                                  [](const set_item& item)
                                                  {
                                                      return !item.error_json.empty();
                                                  });
             std::string results = "[";
             for(const auto& item : items)
             {
                 component_apply_result written;
                 if(!is_rejected)
                 {
                     visit_kind_of(ctx,
                                   item.key,
                                   [&]<typename Kind>()
                                   {
                                       std::string ignored;
                                       Kind::write(ctx,
                                                   find_existing_asset<typename Kind::asset_type>(ctx, item.key, ignored),
                                                   item.properties,
                                                   save,
                                                   written);
                                   });
                 }
                 // An item that passed its check is not written when another one was rejected.
                 const auto result_json = !item.error_json.empty() ? item.error_json
                                          : is_rejected            ? std::string(R"("not written: another item was rejected")")
                                                                   : component_apply_result_to_json(written);
                 results += fmt::format(R"({}{{"asset":{},"ok":{},"result":{}}})",
                                        results.size() > 1 ? "," : "",
                                        make_json_string(item.key.empty() ? item.reference : item.key),
                                        bool_to_json(!is_rejected && written.ok),
                                        result_json);
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"written":{},"saved":{}}})",
                                         results,
                                         bool_to_json(!is_rejected),
                                         bool_to_json(!is_rejected && save)),
                     .is_error = is_rejected};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive,
         .idempotent = true});

    registry.add(
        {.name = "assets_create_batch",
         .description =
             "Create material, physics material or animation controller assets in the project's data folder. Each item: type, path (the "
             "extension is added) or folder + name, optional properties - checked first: a rejected value "
             "creates nothing. Waits up to wait_ms (default 15000; 0: no wait) for them to load, each with a "
             "state. Assign a material with scene_set_model_materials_batch, a physics material with the Physics "
             "property material.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string","enum":["material","physics_material","animation_controller"]},"path":{"type":"string"},"folder":{"type":"string"},"name":{"type":"string"},"properties":{"type":"object"}},"required":["type"]}},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 15000);
             std::string error;
             simdjson::dom::array items_arr;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             struct create_item
             {
                 std::string type;
                 std::string requested;
                 std::string properties_json;
             };
             std::vector<create_item> items;
             for(auto element : items_arr)
             {
                 simdjson::dom::object fields;
                 if(!read_object(element, fields, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto& item = items.emplace_back();
                 read_string(fields, "type", item.type);
                 std::string folder;
                 std::string name;
                 read_string(fields, "path", item.requested);
                 read_string(fields, "folder", folder);
                 read_string(fields, "name", name);
                 if(item.requested.empty() && !folder.empty() && !name.empty())
                 {
                     item.requested = folder + "/" + name;
                 }
                 simdjson::dom::object properties;
                 item.properties_json = fields["properties"].get_object().get(properties) == simdjson::SUCCESS
                                            ? std::string(simdjson::minify(properties))
                                            : std::string("{}");
             }
             struct outcome_t
             {
                 std::string key;
                 std::string uid;
                 std::string error;
             };
             auto outcomes = mcp.invoke_on_main(
                 [&ctx, items]() -> std::vector<outcome_t>
                 {
                     std::vector<outcome_t> created;
                     for(const auto& item : items)
                     {
                         auto& outcome = created.emplace_back();
                         outcome.error = "type must be one of " + kind_names();
                         visit_kind_named(item.type,
                                          [&]<typename Kind>()
                                          {
                                        using asset_type = typename Kind::asset_type;
                                        outcome.error.clear();
                                        if(item.requested.empty())
                                        {
                                            outcome.error = "Provide path, or folder + name";
                                            return;
                                        }
                                        if(!to_project_data_file_key(ctx,
                                                                     item.requested,
                                                                     ex::get_suported_formats<asset_type>(),
                                                                     outcome.key,
                                                                     outcome.error))
                                        {
                                            return;
                                        }
                                        fs::error_code ec;
                                        const auto absolute = fs::absolute(fs::resolve_protocol(outcome.key));
                                        if(fs::exists(absolute, ec))
                                        {
                                            outcome.error = "Already exists: " + outcome.key + " (assets_set_properties_batch edits it)";
                                            return;
                                        }
                                        simdjson::dom::parser parser;
                                        simdjson::dom::object properties;
                                        (void)parser.parse(item.properties_json).get(properties);
                                        component_apply_result check;
                                        Kind::check(ctx, asset_handle<asset_type>{}, properties, check);
                                        if(!check.ok)
                                        {
                                            outcome.error = component_apply_result_to_json(check);
                                            return;
                                        }
                                        fs::create_directories(absolute.parent_path(), ec);
                                        auto handle = Kind::create(ctx, absolute);
                                        if(!handle)
                                        {
                                            outcome.error = "Failed to create " + outcome.key;
                                            return;
                                        }
                                        component_apply_result written;
                                        Kind::write(ctx, handle, properties, true, written);
                                        outcome.uid = hpp::to_string(handle.uid());
                                        outcome.error = written.ok ? std::string{} : component_apply_result_to_json(written);
                                    });
                     }
                     return created;
                 });
             if(!outcomes)
             {
                 return {.text = "Timed out creating assets on the main thread", .is_error = true};
             }
             std::vector<std::string> keys;
             std::vector<bool> is_created;
             for(const auto& outcome : *outcomes)
             {
                 keys.push_back(outcome.key);
                 is_created.push_back(outcome.error.empty());
             }
             const auto report = wait_for_assets(ctx, keys, is_created, wait_ms);
             std::string results = "[";
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 const auto& outcome = (*outcomes)[i];
                 results += i > 0 ? "," : "";
                 if(!is_created[i])
                 {
                     results += fmt::format(R"({{"ok":false,"asset":{},"error":{}}})",
                                            make_json_string(outcome.key.empty() ? items[i].requested : outcome.key),
                                            make_json_string(outcome.error));
                     continue;
                 }
                 results += fmt::format(R"({{"ok":true,"asset":{},"uid":{},"type":{}{}}})",
                                        make_json_string(outcome.key),
                                        make_json_string(outcome.uid),
                                        make_json_string(items[i].type),
                                        report.state_fields[i]);
             }
             results += "]";
             const auto ok_count = static_cast<size_t>(std::count(is_created.begin(), is_created.end(), true));
             return {.text = fmt::format(R"({{"results":{},"count":{},"ready":{},"requested":{}}})",
                                         results,
                                         ok_count,
                                         report.ready_count,
                                         keys.size()),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = false,
         .requires_main_thread = false});
}

} // namespace unravel::mcp
