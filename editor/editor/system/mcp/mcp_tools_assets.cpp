#include "mcp_tools_common.h"

#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <filesystem/filesystem.h>

namespace unravel::mcp
{
namespace
{

auto protocol_to_group(const std::string& protocol) -> std::string
{
    if(protocol == "app" || protocol == "app:/")
    {
        return "app:/";
    }
    if(protocol == "engine" || protocol == "engine:/")
    {
        return "engine:/";
    }
    if(protocol == "editor" || protocol == "editor:/")
    {
        return "editor:/";
    }
    return {};
}

const std::vector<std::string>& embedded_primitives()
{
    static const std::vector<std::string> k_primitives = {
        "Cube",
        "Cube Rounded",
        "Sphere",
        "Plane",
        "Cylinder",
        "Capsule 2m",
        "Capsule 1m",
        "Cone",
        "Torus",
        "Teapot",
        "Icosahedron",
        "Dodecahedron",
        "Icosphere0",
        "Icosphere1",
        "Icosphere2",
        "Icosphere3",
        "Terrain Test",
    };
    return k_primitives;
}

} // namespace

void register_asset_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name="project_get_info",
         .description="Get the currently open project name/path, or empty if none.",
         .input_schema_json=empty_object_schema(),
         .handler=[](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             if(!ctx.has<project_manager>())
             {
                 return {.text=R"({"open":false})", .is_error=false};
             }
             auto& pm = ctx.get_cached<project_manager>();
             if(!pm.has_open_project())
             {
                 return {.text=R"({"open":false})", .is_error=false};
             }

             const auto& info = pm.get_project_info();
             const auto path = fs::resolve_protocol("app:/").generic_string();
             return {.text=fmt::format(R"({{"open":true,"name":{},"path":{},"guid":{}}})",
                                 make_json_string(pm.get_name()),
                                 make_json_string(path),
                                 make_json_string(info.project_guid)),
                     .is_error=false};
         },
         .mutates_scene=false});

    registry.add(
        {.name="assets_list",
         .description="List assets for a protocol group: app, engine, or editor. Optional type filter (mesh, material, prefab, ...).",
         .input_schema_json=R"({"type":"object","properties":{"protocol":{"type":"string","enum":["app","engine","editor"]},"type":{"type":"string"}},"required":["protocol"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string protocol;
             if(!read_string(args, "protocol", protocol))
             {
                 return {.text="Missing protocol", .is_error=true};
             }
             const auto group = protocol_to_group(protocol);
             if(group.empty())
             {
                 return {.text="Invalid protocol (use app|engine|editor)", .is_error=true};
             }

             std::string type_filter;
             read_string(args, "type", type_filter);

             auto& am = ctx.get_cached<asset_manager>();
             auto locations = am.get_all_assets(group);

             std::string json = "[";
             bool first = true;
             for(const auto& location : locations)
             {
                 auto meta = am.get_metadata_for_key(location);
                 if(!type_filter.empty() && meta.meta.type != type_filter)
                 {
                     continue;
                 }
                 if(!first)
                 {
                     json += ",";
                 }
                 first = false;
                 json += fmt::format(R"({{"uid":{},"location":{},"type":{}}})",
                                     make_json_string(hpp::to_string(meta.meta.uid)),
                                     make_json_string(location),
                                     make_json_string(meta.meta.type));
             }
             json += "]";
             return {.text=json, .is_error=false};
         },
         .mutates_scene=false});

    registry.add(
        {.name="assets_get",
         .description="Get a single asset by key (location) or uid.",
         .input_schema_json=R"({"type":"object","properties":{"key":{"type":"string"},"uid":{"type":"string"}}})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& am = ctx.get_cached<asset_manager>();
             std::string key;
             std::string uid_str;
             read_string(args, "key", key);
             read_string(args, "uid", uid_str);

             asset_database::meta meta{};
             if(!key.empty())
             {
                 meta = am.get_metadata_for_key(key);
             }
             else if(!uid_str.empty())
             {
                 auto uuid = hpp::uuid::from_string(uid_str);
                 if(!uuid)
                 {
                     return {.text="Invalid uid", .is_error=true};
                 }
                 meta = am.get_metadata(*uuid);
             }
             else
             {
                 return {.text="Provide key or uid", .is_error=true};
             }

             if(meta.location.empty() && meta.meta.type.empty())
             {
                 return {.text="Asset not found", .is_error=true};
             }

             return {.text=fmt::format(R"({{"uid":{},"location":{},"type":{}}})",
                                 make_json_string(hpp::to_string(meta.meta.uid)),
                                 make_json_string(meta.location.empty() ? key : meta.location),
                                 make_json_string(meta.meta.type)),
                     .is_error=false};
         },
         .mutates_scene=false});

    registry.add(
        {.name="assets_list_types",
         .description="List known registered asset type names.",
         .input_schema_json=empty_object_schema(),
         .handler=[](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             // asset_meta.type stores the file extension (e.g. ".emesh", ".prefab").
             static const char* types[] = {".sc",
                                          ".sh",
                                          ".etex",
                                          ".mat",
                                          ".emesh",
                                          ".anim",
                                          ".prefab",
                                          ".scene",
                                          ".phxmat",
                                          ".rml",
                                          ".rcss",
                                          ".eaudioclip",
                                          ".ttf",
                                          ".otf",
                                          ".cs"};
             std::string json = "[";
             for(size_t i = 0; i < std::size(types); ++i)
             {
                 if(i > 0)
                 {
                     json += ",";
                 }
                 json += make_json_string(types[i]);
             }
             json += "]";
             return {.text=json, .is_error=false};
         },
         .mutates_scene=false});

    registry.add(
        {.name="assets_list_embedded_primitives",
         .description="List embedded mesh primitive names usable with scene_create_primitive.",
         .input_schema_json=empty_object_schema(),
         .handler=[](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             std::string json = "[";
             const auto& names = embedded_primitives();
             for(size_t i = 0; i < names.size(); ++i)
             {
                 if(i > 0)
                 {
                     json += ",";
                 }
                 json += make_json_string(names[i]);
             }
             json += "]";
             return {.text=json, .is_error=false};
         },
         .mutates_scene=false});
}

} // namespace unravel::mcp
