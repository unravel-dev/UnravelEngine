#include "package_actions.h"
#include "asset_actions.h"

#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/scripting/ecs/systems/script_system.h>

#include <filesystem/watcher.h>
#include <logging/logging.h>

namespace unravel::package_actions
{
namespace
{
constexpr const char* PACKAGES_STAGE_FOLDER = "package-import";

auto get_project_root() -> fs::path
{
    return fs::resolve_protocol("app:/");
}

/// The script components the project and the engine define, with their files.
auto list_script_types(rtti::context& ctx) -> std::vector<packages::script_type_source>
{
    std::vector<packages::script_type_source> types;
    if(!ctx.has<script_system>())
    {
        return types;
    }
    const auto& scripts = ctx.get_cached<script_system>();
    for(const auto& type : scripts.get_all_scriptable_components())
    {
        const auto file = scripts.get_script_source_file(type);
        if(file.empty())
        {
            continue;
        }
        types.push_back({type.get_fullname(), fs::convert_to_protocol(file).generic_string()});
    }
    return types;
}

/// Tells the watcher about files written or removed while it was paused.
void touch_keys(const std::vector<std::string>& keys)
{
    for(const auto& key : keys)
    {
        const auto file = fs::resolve_protocol(key);
        fs::error_code ec;
        if(fs::exists(file, ec))
        {
            fs::watcher::touch(file, false);
        }
        else
        {
            fs::watcher::touch(file.parent_path(), false);
        }
    }
}

auto make_hooks() -> packages::install_hooks
{
    packages::install_hooks hooks;
    hooks.move_to_trash = [](const std::vector<fs::path>& files, std::string& batch, std::string& err) -> bool
    {
        return asset_actions::move_to_trash(files, batch, &err);
    };
    hooks.restore_from_trash = [](const std::string& batch, std::string& err) -> bool
    {
        std::vector<fs::path> restored;
        return asset_actions::restore_from_trash(batch, restored, &err);
    };
    return hooks;
}
} // namespace

auto get_packages_folder() -> fs::path
{
    return packages::get_packages_folder(get_project_root());
}

auto make_project_view(rtti::context& ctx) -> packages::project_view
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& pm = ctx.get_cached<project_manager>();
    packages::project_view view;
    view.project_root = get_project_root();
    view.project_guid = pm.get_project_info().project_guid;
    view.database = am.get_database_snapshot("app:/");
    for(const auto& [uid, meta] : am.get_database_snapshot("engine:/"))
    {
        view.engine_uids.insert(uid);
    }
    view.script_types = list_script_types(ctx);
    view.engine = version::get_current();
    view.registry = load_registry(ctx, view.registry_error);
    return view;
}

auto make_export_source(rtti::context& ctx) -> packages::export_source
{
    packages::export_source source;
    source.content = deployment::make_project_content_source(ctx.get_cached<asset_manager>(),
                                                             ctx.get_cached<project_manager>().get_settings());
    source.script_types = list_script_types(ctx);
    return source;
}

auto load_registry(rtti::context& ctx, std::string& err) -> packages::package_registry
{
    packages::package_registry registry;
    err.clear();
    if(!packages::load_registry(packages::get_registry_path(get_project_root()), registry, err))
    {
        APPLOG_ERROR("{}", err);
    }
    return registry;
}

auto install(rtti::context& ctx,
             const packages::package_archive& archive,
             const packages::import_plan& plan,
             const packages::project_view& project) -> packages::install_result
{
    packages::install_result result;
    const auto stage = fs::resolve_protocol("cache:/") / PACKAGES_STAGE_FOLDER;
    {
        // One batch, as an Import copies: the asset database sees every file with its .meta, never half of an
        // install, and the touches land before the scan that follows the pause.
        fs::watcher::scoped_pause pause;
        result = packages::install_package(archive, plan, project, stage, make_hooks());
        touch_keys(result.written_keys);
        touch_keys(result.removed_keys);
    }
    if(result.is_done)
    {
        APPLOG_INFO("Imported {} {}: {} file(s) written, {} kept, {} moved to the trash.",
                    plan.manifest.get_title(),
                    plan.manifest.version,
                    result.written_keys.size(),
                    result.kept_count,
                    result.removed_keys.size());
    }
    else
    {
        APPLOG_ERROR("Importing {} failed: {}", plan.manifest.get_title(), result.error);
    }
    return result;
}

auto remove(rtti::context& ctx, const packages::removal_plan& plan, const packages::project_view& project)
    -> packages::install_result
{
    packages::install_result result;
    {
        fs::watcher::scoped_pause pause;
        result = packages::remove_package(plan, project, make_hooks());
        touch_keys(result.removed_keys);
    }
    if(result.is_done)
    {
        APPLOG_INFO("Removed {}: {} file(s) moved to the trash, {} edited file(s) kept.",
                    plan.package.get_title(),
                    result.removed_keys.size(),
                    result.kept_count);
    }
    else
    {
        APPLOG_ERROR("Removing {} failed: {}", plan.package.get_title(), result.error);
    }
    return result;
}

auto save_definition(rtti::context& ctx, const std::string& root, const packages::package_manifest& definition, std::string& err)
    -> bool
{
    packages::package_manifest saved;
    saved.name = definition.name;
    saved.display_name = definition.display_name;
    saved.version = definition.version;
    saved.description = definition.description;
    saved.author = definition.author;
    saved.license = definition.license;
    saved.minimum_engine = definition.minimum_engine;
    saved.entry_scene = definition.entry_scene;
    saved.dependencies = definition.dependencies;
    saved.root = root;
    return packages::save_manifest(packages::get_definition_path(get_project_root(), root), saved, err);
}

auto is_package_file(const fs::path& file) -> bool
{
    return fs::make_path_key(file.extension().string()) == fs::make_path_key(packages::PACKAGE_EXTENSION);
}

} // namespace unravel::package_actions
