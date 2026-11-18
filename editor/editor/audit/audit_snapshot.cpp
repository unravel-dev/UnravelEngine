#include "audit_snapshot.h"

#include <asset_compiler/asset_compiler.h>
#include <asset_compiler/texture_target.h>
#include <deploy/content.h>
#include <deploy/files.h>
#include <deploy/profile.h>
#include <dotnetpp/dotnet_jit.h>
#include <editor/deploy/deploy_preflight.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <serialization/serialization.h>
#include <version/version.h>

#include <algorithm>

namespace unravel
{
namespace
{

/// The analysis tool beside the editor.
constexpr const char* AUDIT_CODE_TOOL = "binary:/code_audit/Unravel.CodeAudit.dll";
/// The framework's reference assemblies the bridge keeps for script compiles, one folder per target framework.
constexpr const char* AUDIT_REFERENCE_ASSEMBLIES = "binary:/clrpp/ref";

/// A copy of import settings the run can read while the editor edits the asset's own.
auto clone_importer(const std::shared_ptr<asset_importer_meta>& importer) -> std::shared_ptr<const asset_importer_meta>
{
    if(!importer)
    {
        return nullptr;
    }
    if(const auto* texture = importer->safe_cast<texture_importer_meta>())
    {
        return std::make_shared<texture_importer_meta>(*texture);
    }
    if(const auto* audio = importer->safe_cast<audio_importer_meta>())
    {
        return std::make_shared<audio_importer_meta>(*audio);
    }
    if(const auto* mesh = importer->safe_cast<mesh_importer_meta>())
    {
        return std::make_shared<mesh_importer_meta>(*mesh);
    }
    return nullptr;
}

auto make_asset_record(const std::string& key, const hpp::uuid& uid, const asset_meta& meta, const fs::path& project_root)
    -> audit::asset_record
{
    audit::asset_record record;
    record.key = key;
    record.uid = uid;
    record.source_file = fs::resolve_protocol(key);
    record.importer = clone_importer(meta.importer);
    record.is_generated = meta.generated.is_generated();
    for(auto& path : deployment::get_compiled_paths(key, project_root))
    {
        fs::error_code ec;
        const auto size = fs::file_size(path, ec);
        if(ec)
        {
            continue;
        }
        record.compiled_size += size;
        record.compiled_files.push_back(std::move(path));
    }
    return record;
}

auto make_profile_record(rtti::context& ctx,
                         const settings_profile& profile,
                         const settings& base) -> audit::profile_record
{
    auto& pm = ctx.get_cached<project_manager>();
    audit::profile_record record;
    record.name = profile.name;
    record.platform = profile.platform;
    record.resolved = pm.get_profile_settings(profile.name);
    record.start = deployment::resolve_settings(base, deployment::make_profile(profile.name, profile.platform, base));
    record.deploy = pm.get_profile_deploy_settings(profile.name).value_or(deploy_settings{});
    record.deploy_problems = deployment::get_problems(ctx, record.deploy);
    record.startup_scene = record.resolved.standalone.startup_scene.id();
    record.settings_document = serialization::save_to_document(record.resolved, SETTINGS_DOCUMENT_ROOT);
    return record;
}

void add_script_messages(audit::input& in)
{
    const auto compilation = asset_compiler::get_last_script_compilation(deployment::CONTENT_PROTOCOL);
    in.scripts_compiled = !compilation.has_run || compilation.succeeded;
    for(const auto& entry : compilation.errors)
    {
        in.script_messages.push_back({entry.file, static_cast<std::uint32_t>(std::max(entry.line, 0)), entry.msg, true});
    }
    for(const auto& entry : compilation.warnings)
    {
        in.script_messages.push_back({entry.file, static_cast<std::uint32_t>(std::max(entry.line, 0)), entry.msg, false});
    }
}

auto find_reference_folders() -> std::vector<fs::path>
{
    std::vector<fs::path> folders;
    folders.push_back(fs::resolve_protocol(script_system::get_lib_compiled_key("engine")).parent_path());
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(fs::resolve_protocol(AUDIT_REFERENCE_ASSEMBLIES), ec))
    {
        if(entry.is_directory(ec))
        {
            folders.push_back(entry.path());
        }
    }
    return folders;
}

auto make_code_tool_paths(const fs::path& project_root) -> audit::code_tool_paths
{
    audit::code_tool_paths paths;
    // The dotnet host the script compiler runs on; the compile command names it first.
    paths.dotnet = dotnet::create_compile_command_detailed(dotnet::compiler_params{}).cmd;
    paths.tool = fs::resolve_protocol(AUDIT_CODE_TOOL);
    paths.assembly = fs::resolve_protocol(script_system::get_lib_compiled_key(deployment::CONTENT_PROTOCOL));
    paths.reference_folders = find_reference_folders();
    paths.project_root = project_root;
    return paths;
}

} // namespace

auto make_audit_input(rtti::context& ctx, bool analyze_code) -> audit::input
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& am = ctx.get_cached<asset_manager>();
    audit::input in;
    in.project_root = deployment::get_project_root();
    in.project_name = pm.get_name();
    in.engine_version = version::get_full();
    in.project_settings = pm.get_settings();
    in.source = deployment::make_project_content_source(am, in.project_settings);
    in.editor_texture_target = asset_compiler::get_editor_texture_target();
    for(const auto& [uid, entry] : in.source.database)
    {
        in.assets.push_back(make_asset_record(entry.location, uid, entry.meta, in.project_root));
    }
    std::sort(in.assets.begin(),
              in.assets.end(),
              [](const audit::asset_record& lhs, const audit::asset_record& rhs)
              {
                  return lhs.key < rhs.key;
              });
    in.deploy_profile = pm.get_deploy_profile();
    for(const auto& profile : pm.get_profiles().get_all())
    {
        in.profiles.push_back(make_profile_record(ctx, profile, in.project_settings));
    }
    add_script_messages(in);
    if(analyze_code)
    {
        in.code_tool = make_code_tool_paths(in.project_root);
    }
    return in;
}

auto list_audit_documents(const audit::input& in) -> std::vector<audit_document>
{
    std::vector<audit_document> documents;
    for(const auto& asset : in.assets)
    {
        const auto extension = fs::path(asset.key).extension().generic_string();
        if(ex::is_format<scene_prefab>(extension))
        {
            documents.push_back({asset.key, audit_document::kind::scene});
        }
        else if(ex::is_format<prefab>(extension))
        {
            documents.push_back({asset.key, audit_document::kind::prefab});
        }
        else if(ex::is_format<material>(extension))
        {
            documents.push_back({asset.key, audit_document::kind::material});
        }
        else if(ex::is_format<animation_controller>(extension))
        {
            documents.push_back({asset.key, audit_document::kind::controller});
        }
    }
    return documents;
}

} // namespace unravel
