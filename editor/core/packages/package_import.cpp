#include "package_import.h"
#include "package_version.h"

#include <engine/meta/ecs/entity.hpp>
#include <uuid/uuid.h>

#include <logging/logging.h>

#include <algorithm>
#include <unordered_map>

namespace unravel::packages
{
namespace
{
constexpr const char* IMPORT_SCENE_TYPE = ".spfb";
constexpr const char* IMPORT_SCRIPT_TYPE = ".cs";
/// How many numbered names a free name is looked for among.
constexpr int IMPORT_MAX_COPY_NUMBER = 1000;

/// The project as an import looks things up in it.
class project_index
{
public:
    explicit project_index(const project_view& project) : project_(project)
    {
        for(const auto& [uid, meta] : project.database)
        {
            uid_by_key_.emplace(fs::make_path_key(meta.location), uid);
        }
    }

    /// The key of the project file with the id; empty when there is none.
    auto find_key(const hpp::uuid& uid) const -> std::string
    {
        const auto found = project_.database.find(uid);
        return found == project_.database.end() ? std::string{} : found->second.location;
    }

    /// Whether something already is at the key: a known asset, a file on disk, or a file the plan puts there.
    auto is_taken(const std::string& key) const -> bool
    {
        const auto path_key = fs::make_path_key(key);
        if(uid_by_key_.count(path_key) != 0 || claimed_.count(path_key) != 0)
        {
            return true;
        }
        fs::error_code ec;
        return fs::exists(get_disk_path(key), ec);
    }

    void claim(const std::string& key)
    {
        claimed_.insert(fs::make_path_key(key));
    }

    /// A free key beside `key`: "Name (1).ext", "Name (2).ext", ...
    auto make_free_key(const std::string& key) -> std::string
    {
        const fs::path path(key);
        const auto stem = path.stem().string();
        const auto extension = path.extension().string();
        const auto parent = path.parent_path().generic_string();
        for(int number = 1; number < IMPORT_MAX_COPY_NUMBER; ++number)
        {
            const auto candidate = fmt::format("{}/{} ({}){}", parent, stem, number, extension);
            if(!is_taken(candidate))
            {
                claim(candidate);
                return candidate;
            }
        }
        return {};
    }

    auto get_disk_path(const std::string& key) const -> fs::path
    {
        const auto path = get_data_path(key);
        return project_.project_root / "data" / fs::path(path);
    }

    /// The fingerprint of the project file at `key`; empty when it cannot be read.
    auto fingerprint(const std::string& key) const -> std::string
    {
        return fingerprint_file(get_disk_path(key));
    }

private:
    const project_view& project_;
    std::unordered_map<std::string, hpp::uuid> uid_by_key_;
    std::unordered_set<std::string> claimed_;
};

auto is_readable_document(const package_file& file) -> bool
{
    if(file.document_version == 0)
    {
        return true;
    }
    return file.type == IMPORT_SCENE_TYPE ? is_readable_scene_version(file.document_version)
                                          : is_readable_prefab_version(file.document_version);
}

void set_choices(planned_file& planned, std::vector<file_action> choices)
{
    planned.choices = std::move(choices);
    planned.action = planned.choices.empty() ? file_action::skip : planned.choices.front();
}

/// A file an earlier version installed.
void plan_installed_file(const package_file& file,
                         const installed_file& record,
                         project_index& index,
                         planned_file& planned)
{
    planned.existing_uid = record.uid;
    planned.existing_key = index.find_key(record.uid);
    if(planned.existing_key.empty())
    {
        planned.status = file_status::deleted;
        if(index.is_taken(planned.target_key))
        {
            planned.target_key = index.make_free_key(planned.target_key);
        }
        index.claim(planned.target_key);
        set_choices(planned, {file_action::skip, file_action::write});
        return;
    }
    // Written where the installed file is now: the user may have moved it.
    planned.target_key = planned.existing_key;
    const auto current = index.fingerprint(planned.existing_key);
    if(current == file.hash)
    {
        planned.status = file_status::unchanged;
        // A new .meta (other import settings) is written over the project's; the same one keeps the project's.
        set_choices(planned, file.meta_hash != record.meta_hash ? std::vector<file_action>{file_action::write} : std::vector<file_action>{});
        return;
    }
    if(current == record.hash)
    {
        planned.status = file_status::updated;
        set_choices(planned, {file_action::write, file_action::skip});
        return;
    }
    planned.status = file_status::edited;
    planned.copy_key = index.make_free_key(planned.existing_key);
    set_choices(planned, {file_action::keep_mine, file_action::replace, file_action::write_copy});
}

/// A file the project did not get from this package.
void plan_new_file(const package_file& file, project_index& index, planned_file& planned)
{
    planned.existing_key = index.find_key(file.uid);
    if(!planned.existing_key.empty())
    {
        planned.existing_uid = file.uid;
        if(index.fingerprint(planned.existing_key) == file.hash)
        {
            planned.status = file_status::present;
            set_choices(planned, {file_action::adopt});
            return;
        }
        planned.status = file_status::uid_clash;
        planned.copy_key = index.is_taken(planned.target_key) ? index.make_free_key(planned.target_key) : planned.target_key;
        index.claim(planned.copy_key);
        set_choices(planned, {file_action::write_copy, file_action::replace, file_action::skip});
        return;
    }
    if(index.is_taken(planned.target_key))
    {
        planned.status = file_status::path_clash;
        planned.copy_key = index.make_free_key(planned.target_key);
        set_choices(planned, {file_action::write_copy, file_action::skip});
        return;
    }
    planned.status = file_status::added;
    index.claim(planned.target_key);
    set_choices(planned, {file_action::write, file_action::skip});
}

void check_engine(const package_manifest& manifest, const project_view& project, import_plan& plan)
{
    const auto exported = version::parse(manifest.exported_with);
    if(exported && project.engine < *exported)
    {
        plan.warnings.push_back(fmt::format("It was made with a newer engine ({}) than this one ({}). Scenes and prefabs "
                                            "in a layout this engine does not read are left out.",
                                            exported->to_string(),
                                            project.engine.to_string()));
    }
    const auto minimum = version::parse(manifest.minimum_engine);
    if(minimum && project.engine < *minimum)
    {
        plan.problems.push_back(fmt::format("It needs engine {} or newer, and this one is {}.",
                                            minimum->to_string(),
                                            project.engine.to_string()));
    }
}

void check_dependencies(const package_manifest& manifest, const project_view& project, import_plan& plan)
{
    for(const auto& dependency : manifest.dependencies)
    {
        dependency_check check;
        check.dependency = dependency;
        if(const auto* installed = project.registry.find(dependency.name))
        {
            check.installed_version = installed->version;
            check.is_satisfied = dependency.range.empty() || is_in_range(installed->version, dependency.range);
        }
        if(!check.is_satisfied)
        {
            plan.warnings.push_back(check.installed_version.empty()
                                        ? fmt::format("It needs the package {} ({}), which is not installed. Its files may "
                                                      "miss references until it is.",
                                                      dependency.name,
                                                      dependency.range.empty() ? "any version" : dependency.range)
                                        : fmt::format("It needs {} {}, and {} is installed.",
                                                      dependency.name,
                                                      dependency.range,
                                                      check.installed_version));
        }
        plan.dependencies.push_back(std::move(check));
    }
}

void check_engine_assets(const package_manifest& manifest, const project_view& project, import_plan& plan)
{
    for(const auto& asset : manifest.engine_assets)
    {
        if(project.engine_uids.count(asset.uid) == 0)
        {
            plan.missing_engine_assets.push_back(asset.key);
        }
    }
    if(!plan.missing_engine_assets.empty())
    {
        plan.warnings.push_back(fmt::format("It references {} engine asset(s) this engine does not have, such as {}.",
                                            plan.missing_engine_assets.size(),
                                            plan.missing_engine_assets.front()));
    }
}

/// A key as the review names it: its path below the data folder, or the key itself for an engine file.
auto describe_key(const std::string& key) -> std::string
{
    const auto path = get_data_path(key);
    return path.empty() ? key : path;
}

/// The script types a package file defines. A package from before files listed their types: any of its scripts may
/// define any of the types it lists.
auto get_file_script_types(const package_manifest& manifest, const package_file& file) -> const std::vector<std::string>&
{
    return file.script_types.empty() && file.type == IMPORT_SCRIPT_TYPE ? manifest.script_types : file.script_types;
}

/// Drops the choices that need a name none was found for; a file left with nothing to write is skipped.
void drop_unplaceable_choices(import_plan& plan)
{
    for(auto& planned : plan.files)
    {
        const auto is_unplaceable = [&planned](file_action action)
        {
            return (action == file_action::write_copy && planned.copy_key.empty()) ||
                   (action == file_action::write && planned.target_key.empty());
        };
        auto choices = planned.choices;
        const auto kept_end = std::remove_if(choices.begin(), choices.end(), is_unplaceable);
        if(kept_end == choices.end())
        {
            continue;
        }
        choices.erase(kept_end, choices.end());
        set_choices(planned, std::move(choices));
        plan.warnings.push_back(fmt::format("No free name was found next to {} in the project, so the package's file "
                                            "cannot be added under another name.",
                                            plan.manifest.files[planned.index].path));
    }
}

auto is_same_planned_file(const planned_file& lhs, const planned_file& rhs) -> bool
{
    return lhs.index == rhs.index && lhs.status == rhs.status && lhs.target_key == rhs.target_key &&
           lhs.copy_key == rhs.copy_key && lhs.existing_key == rhs.existing_key && lhs.existing_uid == rhs.existing_uid &&
           lhs.choices == rhs.choices;
}

auto is_same_removal(const planned_removal& lhs, const planned_removal& rhs) -> bool
{
    return lhs.key == rhs.key && lhs.uid == rhs.uid && lhs.is_edited == rhs.is_edited;
}

void plan_removals(project_index& index, import_plan& plan)
{
    std::unordered_set<hpp::uuid> kept_sources;
    for(const auto& file : plan.manifest.files)
    {
        kept_sources.insert(file.uid);
    }
    for(const auto& record : plan.installed->files)
    {
        if(kept_sources.count(record.source_uid) != 0)
        {
            continue;
        }
        planned_removal removal;
        removal.uid = record.uid;
        removal.key = index.find_key(record.uid);
        if(removal.key.empty())
        {
            continue;
        }
        removal.is_edited = index.fingerprint(removal.key) != record.hash;
        removal.is_removed = !removal.is_edited;
        plan.removals.push_back(std::move(removal));
    }
}
} // namespace

auto import_plan::is_downgrade() const -> bool
{
    if(!installed)
    {
        return false;
    }
    const auto installed_version = parse_version(installed->version);
    const auto package_version = parse_version(manifest.version);
    return installed_version && package_version && *package_version < *installed_version;
}

auto import_plan::can_apply() const -> bool
{
    return problems.empty() && clashes.empty() && has_changes();
}

auto import_plan::has_changes() const -> bool
{
    const bool writes = std::any_of(files.begin(),
                                    files.end(),
                                    [](const planned_file& file)
                                    {
                                        return file.action != file_action::skip;
                                    });
    const bool removes = std::any_of(removals.begin(),
                                     removals.end(),
                                     [](const planned_removal& removal)
                                     {
                                         return removal.is_removed;
                                     });
    // Recording the package is a change too: a first import of files the project already holds.
    return writes || removes || !installed || installed->version != manifest.version || installed->source_hash != package_hash;
}

auto import_plan::count(file_status status) const -> std::size_t
{
    return std::size_t(std::count_if(files.begin(),
                                     files.end(),
                                     [status](const planned_file& file)
                                     {
                                         return file.status == status;
                                     }));
}

auto make_copy_uid(const std::string& project_guid, const std::string& package_name, const hpp::uuid& source_uid) -> hpp::uuid
{
    return generate_uuid(fmt::format("package-copy/{}/{}/{}", project_guid, package_name, hpp::to_string(source_uid)));
}

auto plan_import(const package_archive& archive, const project_view& project) -> import_plan
{
    import_plan plan;
    plan.manifest = archive.get_manifest();
    plan.package_hash = archive.get_hash();
    plan.package_file_name = archive.get_location().filename().string();
    if(const auto* installed = project.registry.find(plan.manifest.name))
    {
        plan.installed = *installed;
    }
    if(!project.registry_error.empty())
    {
        plan.problems.push_back("The project's record of its packages does not read, and an import would write over it. " +
                                project.registry_error);
    }
    check_engine(plan.manifest, project, plan);
    check_dependencies(plan.manifest, project, plan);
    check_engine_assets(plan.manifest, project, plan);
    if(plan.is_downgrade())
    {
        plan.warnings.push_back(fmt::format("Version {} is installed, which is newer than this package's {}.",
                                            plan.installed->version,
                                            plan.manifest.version));
    }
    project_index index(project);
    for(std::size_t file_index = 0; file_index < plan.manifest.files.size(); ++file_index)
    {
        const auto& file = plan.manifest.files[file_index];
        planned_file planned;
        planned.index = file_index;
        planned.target_key = get_data_key(file.path);
        if(!is_readable_document(file))
        {
            planned.status = file_status::incompatible;
            set_choices(planned, {});
        }
        else if(const auto* record = plan.installed ? plan.installed->find_by_source(file.uid) : nullptr)
        {
            plan_installed_file(file, *record, index, planned);
        }
        else
        {
            plan_new_file(file, index, planned);
        }
        plan.files.push_back(std::move(planned));
    }
    if(plan.installed)
    {
        plan_removals(index, plan);
    }
    drop_unplaceable_choices(plan);
    update_script_clashes(plan, project);
    if(const auto incompatible = plan.count(file_status::incompatible); incompatible > 0)
    {
        plan.warnings.push_back(fmt::format("{} scene or prefab file(s) are in a layout this engine does not read and stay out.",
                                            incompatible));
    }
    return plan;
}

auto get_destination(const planned_file& planned) -> std::string
{
    switch(planned.action)
    {
        case file_action::write:
            return planned.target_key;
        case file_action::write_copy:
            return planned.copy_key;
        case file_action::replace:
            return planned.existing_key;
        default:
            return {};
    }
}

auto find_script_clashes(const import_plan& plan, const project_view& project) -> std::vector<std::string>
{
    // The project files the install moves to the trash or writes over, and the types each written script defines.
    std::unordered_set<std::string> gone_keys;
    std::vector<std::pair<std::string, std::string>> written_types;
    for(const auto& planned : plan.files)
    {
        if(planned.action == file_action::replace && !planned.existing_key.empty())
        {
            gone_keys.insert(fs::make_path_key(planned.existing_key));
        }
        const auto destination = get_destination(planned);
        if(destination.empty())
        {
            continue;
        }
        gone_keys.insert(fs::make_path_key(destination));
        for(const auto& type : get_file_script_types(plan.manifest, plan.manifest.files[planned.index]))
        {
            written_types.emplace_back(type, destination);
        }
    }
    for(const auto& removal : plan.removals)
    {
        if(removal.is_removed)
        {
            gone_keys.insert(fs::make_path_key(removal.key));
        }
    }
    std::vector<std::string> clashes;
    std::unordered_set<std::string> reported;
    for(const auto& type : project.script_types)
    {
        if(gone_keys.count(fs::make_path_key(type.key)) != 0)
        {
            continue;
        }
        for(const auto& [name, destination] : written_types)
        {
            if(name != type.full_name || fs::make_path_key(destination) == fs::make_path_key(type.key) ||
               !reported.insert(name).second)
            {
                continue;
            }
            clashes.push_back(fmt::format("Both {} and {} would define the script type {}, and the scripts would no longer "
                                          "build. Pick another action for {}, or rename the type in one of them.",
                                          describe_key(type.key),
                                          describe_key(destination),
                                          name,
                                          describe_key(destination)));
        }
    }
    return clashes;
}

void update_script_clashes(import_plan& plan, const project_view& project)
{
    plan.clashes = find_script_clashes(plan, project);
}

auto is_same_plan(const import_plan& lhs, const import_plan& rhs) -> bool
{
    return lhs.package_hash == rhs.package_hash && lhs.problems == rhs.problems &&
           std::equal(lhs.files.begin(), lhs.files.end(), rhs.files.begin(), rhs.files.end(), is_same_planned_file) &&
           std::equal(lhs.removals.begin(), lhs.removals.end(), rhs.removals.begin(), rhs.removals.end(), is_same_removal);
}

void carry_actions(const import_plan& picked, import_plan& plan)
{
    for(auto& planned : plan.files)
    {
        const auto found = std::find_if(picked.files.begin(),
                                        picked.files.end(),
                                        [&planned](const planned_file& candidate)
                                        {
                                            return is_same_planned_file(candidate, planned);
                                        });
        if(found != picked.files.end())
        {
            planned.action = found->action;
        }
    }
    for(auto& removal : plan.removals)
    {
        const auto found = std::find_if(picked.removals.begin(),
                                        picked.removals.end(),
                                        [&removal](const planned_removal& candidate)
                                        {
                                            return is_same_removal(candidate, removal);
                                        });
        if(found != picked.removals.end())
        {
            removal.is_removed = found->is_removed;
        }
    }
}

auto get_file_status_name(file_status status) -> const char*
{
    switch(status)
    {
        case file_status::added:
            return "New";
        case file_status::updated:
            return "Update";
        case file_status::unchanged:
            return "Unchanged";
        case file_status::edited:
            return "Edited by you";
        case file_status::deleted:
            return "Deleted by you";
        case file_status::present:
            return "Already here";
        case file_status::uid_clash:
            return "Id in use";
        case file_status::path_clash:
            return "Name in use";
        case file_status::incompatible:
            return "Not readable";
    }
    return "";
}

auto get_file_action_name(file_action action) -> const char*
{
    switch(action)
    {
        case file_action::skip:
            return "Skip";
        case file_action::write:
            return "Write";
        case file_action::write_copy:
            return "Add as copy";
        case file_action::keep_mine:
            return "Keep mine";
        case file_action::replace:
            return "Replace mine";
        case file_action::adopt:
            return "Use mine";
    }
    return "";
}

auto describe_file_action(file_action action) -> const char*
{
    switch(action)
    {
        case file_action::skip:
            return "The project's file stays as it is and the package's is not written.";
        case file_action::write:
            return "The package's file is written, with the id the package gives it.";
        case file_action::write_copy:
            return "The package's file is added beside the project's, under a free name and with an id of its own. "
                   "The project's file stays as it is.";
        case file_action::keep_mine:
            return "Your edited file stays as it is and remains part of the package.";
        case file_action::replace:
            return "Your file goes to the trash and the package's takes its place and its id.";
        case file_action::adopt:
            return "The project already holds this file, and it becomes part of the package.";
    }
    return "";
}

} // namespace unravel::packages
