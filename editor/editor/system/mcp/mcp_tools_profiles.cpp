#include "mcp_profile_utils.h"
#include "mcp_tools_common.h"

#include <deploy/profile.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>

#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{
/// The profile's files that could not be read: they override nothing until mended, and edits are refused.
auto get_unreadable_files(const settings_profile& profile) -> std::vector<std::string>
{
    std::vector<std::string> files;
    if(profile.settings.is_unreadable())
    {
        files.emplace_back("settings.cfg");
    }
    if(profile.deploy.is_unreadable())
    {
        files.emplace_back("deploy.cfg");
    }
    return files;
}

auto profile_to_json(const settings_profile& profile) -> std::string
{
    return fmt::format(R"({{"name":{},"platform":{},"settings_overrides":{},"deploy_overrides":{},"unreadable_files":{}}})",
                       make_json_string(profile.name),
                       make_json_string(get_platform_name(profile.platform)),
                       strings_to_json_array(to_dotted_paths(profile.settings.get_paths())),
                       strings_to_json_array(to_deploy_argument_names(profile.deploy.get_paths())),
                       strings_to_json_array(get_unreadable_files(profile)));
}

auto profiles_to_json(const project_manager& pm) -> std::string
{
    std::string profiles = "[";
    for(const auto& profile : pm.get_profiles().get_all())
    {
        profiles += (profiles.size() > 1 ? "," : "") + profile_to_json(profile);
    }
    profiles += "]";
    return fmt::format(R"({{"profiles":{},"deploy_profile":{},"preview_profile":{}}})",
                       profiles,
                       make_json_string(pm.get_deploy_profile()),
                       pm.get_preview_profile().empty() ? "null" : make_json_string(pm.get_preview_profile()));
}
} // namespace

void register_profile_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "settings_profiles_list",
         .description =
             "The project's settings profiles. Each is a target with a platform (desktop or android) that changes only "
             "what differs from the project: settings_overrides are project settings paths (settings_get and "
             "settings_set with profile), deploy_overrides are deploy settings as deploy_set_settings names them. "
             "unreadable_files lists a profile's files that could not be read: they override nothing, and edits are "
             "refused until the file is mended. deploy_profile is the profile deploys build (deploy_set_settings "
             "deploy_profile, or project_deploy profile, changes it); preview_profile is the one the editor runs "
             "with (editor_set_preview_profile; null for the project settings).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             return {.text = profiles_to_json(*pm), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "settings_profiles_create",
         .description =
             "Create a settings profile; it builds into <deploy location>/<name>. With copy_from it starts as a copy of "
             "that profile, on its platform. Otherwise platform (desktop or android, default desktop) picks the "
             "platform, and the profile starts with that platform's starting values, stored as overrides. An Android "
             "profile starts with textures of at most 1024, a smaller global illumination cache, half resolution "
             "particles, and global illumination, screen space reflections, screen space indirect lighting, ambient "
             "occlusion, soft and contact shadows turned off, anti-aliasing at most FXAA and shadow maps of at most "
             "1024 (graphics.limits).",
         .input_schema_json =
             R"json({"type":"object","properties":{"name":{"type":"string"},"platform":{"type":"string","enum":["desktop","android"]},"copy_from":{"type":"string","description":"A profile to copy, platform included"}},"required":["name"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_editable_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             std::string name;
             read_string(args, "name", name);
             std::string copy_from;
             if(!read_profile_argument(*pm, args, "copy_from", copy_from, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto* source = pm->get_profiles().find(copy_from);
             target_platform platform = source != nullptr ? source->platform : target_platform::desktop;
             std::string platform_name;
             if(read_string(args, "platform", platform_name) && !parse_platform_name(platform_name, platform))
             {
                 return {.text = fmt::format("platform must be desktop or android, not '{}'.", platform_name), .is_error = true};
             }
             if(source != nullptr && source->platform != platform)
             {
                 return {.text = fmt::format("Profile {} is a {} profile, and a copy keeps its platform. Leave platform out, "
                                             "or create the profile without copy_from.",
                                             copy_from,
                                             get_platform_name(source->platform)),
                         .is_error = true};
             }
             const auto created = pm->create_profile(ctx, name, platform, copy_from);
             if(!created.is_ok())
             {
                 return {.text = created.error, .is_error = true};
             }
             return {.text = profiles_to_json(*pm), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::changes});

    registry.add(
        {.name = "settings_profiles_rename",
         .description = "Rename a settings profile. Its kept releases and texture cache move with it, and it stays the "
                        "deploy or preview profile where it was one. Its builds in the deploy folder keep the old name.",
         .input_schema_json =
             R"json({"type":"object","properties":{"name":{"type":"string"},"new_name":{"type":"string"}},"required":["name","new_name"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_editable_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             std::string name;
             std::string new_name;
             read_string(args, "name", name);
             read_string(args, "new_name", new_name);
             const auto renamed = pm->rename_profile(ctx, name, new_name);
             if(!renamed.is_ok())
             {
                 return {.text = renamed.error, .is_error = true};
             }
             return {.text = profiles_to_json(*pm), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "settings_profiles_delete",
         .description = "Delete a settings profile with its kept releases and texture cache. Its builds in the deploy "
                        "folder stay. The last profile cannot be deleted.",
         .input_schema_json = R"json({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_editable_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             std::string name;
             read_string(args, "name", name);
             const auto removed = pm->remove_profile(ctx, name);
             if(!removed.is_ok())
             {
                 return {.text = removed.error, .is_error = true};
             }
             return {.text = profiles_to_json(*pm), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "editor_set_preview_profile",
         .description =
             "Run the editor with a settings profile's settings, or with the project settings for null or an empty "
             "name. The Scene and Game views render with its graphics, lighting and particle settings at once (the "
             "render scale applies where a view's toolbar picks Project Setting, the Game view's default). The "
             "editor keeps compiling assets with the project's "
             "settings, and settings applied at start (the renderer backend) stay the editor's. Refused in play mode.",
         .input_schema_json = R"json({"type":"object","properties":{"name":{"type":["string","null"]}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_editable_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             std::string name;
             read_string(args, "name", name);
             const auto previewed = pm->set_preview_profile(ctx, name);
             if(!previewed.is_ok())
             {
                 return {.text = previewed.error, .is_error = true};
             }
             return {.text = profiles_to_json(*pm), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::changes,
         .idempotent = true});
}

} // namespace unravel::mcp
