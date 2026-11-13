#include "package_install.h"

#include <engine/assets/impl/asset_writer.h>
#include <uuid/uuid.h>
#include <version/version.h>

#include <logging/logging.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace unravel::packages
{
namespace
{
constexpr const char* INSTALL_META_SUFFIX = ".meta";
constexpr const char* INSTALL_BACKUP_FOLDER = "backup";

/// One file the install writes.
struct file_write
{
    std::size_t index{};
    std::string key;
    hpp::uuid uid{};
    fs::path staged_file;
    fs::path staged_meta;
    std::string written_hash;
    /// The destination held a file, which is overwritten in place (its backup is kept until the install is done).
    bool is_overwrite{};
};

auto get_disk_path(const project_view& project, const std::string& key) -> fs::path
{
    return project.project_root / "data" / fs::path(get_data_path(key));
}

auto get_meta_path(const fs::path& file) -> fs::path
{
    return fs::path(file.string() + INSTALL_META_SUFFIX);
}

auto write_bytes(const fs::path& file, const fs::byte_array_t& bytes) -> bool
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    stream.close();
    return stream.good();
}

/// Replaces every id of the map that the text holds.
void rewrite_uids(fs::byte_array_t& bytes, const std::vector<std::pair<std::string, std::string>>& rewrites)
{
    if(rewrites.empty() || !is_text(bytes))
    {
        return;
    }
    std::string text(bytes.begin(), bytes.end());
    for(const auto& [from, to] : rewrites)
    {
        for(auto position = text.find(from); position != std::string::npos; position = text.find(from, position + to.size()))
        {
            text.replace(position, from.size(), to);
        }
    }
    bytes.assign(text.begin(), text.end());
}

/// The id a planned file has in the project once the install is done; nil when the package does not track it.
auto resolve_final_uid(const import_plan& plan, const planned_file& planned, const project_view& project) -> hpp::uuid
{
    const auto& file = plan.manifest.files[planned.index];
    switch(planned.action)
    {
        case file_action::write:
            return planned.status == file_status::added ? file.uid : planned.existing_uid;
        case file_action::write_copy:
        {
            if(planned.status == file_status::path_clash)
            {
                return file.uid;
            }
            auto uid = make_copy_uid(project.project_guid, plan.manifest.name, file.uid);
            // Taken (a copy of a copy kept beside an edited file): an id of its own.
            if(project.database.count(uid) != 0 || uid == planned.existing_uid)
            {
                uid = generate_uuid();
            }
            return uid;
        }
        case file_action::replace:
        case file_action::keep_mine:
            return planned.existing_uid;
        case file_action::adopt:
            return file.uid;
        case file_action::skip:
            // A file the package installed earlier stays its, written or not.
            return plan.installed && plan.installed->find_by_source(file.uid) != nullptr ? planned.existing_uid : hpp::uuid{};
    }
    return {};
}

/// The id every planned file ends with, resolved once: a taken copy id falls back to a random one, and the files and
/// the record must get the same.
auto resolve_final_uids(const import_plan& plan, const project_view& project) -> std::vector<hpp::uuid>
{
    std::vector<hpp::uuid> uids;
    uids.reserve(plan.files.size());
    for(const auto& planned : plan.files)
    {
        uids.push_back(resolve_final_uid(plan, planned, project));
    }
    return uids;
}

/// Puts back the files of a trash batch the install or removal made, also one a move that stopped part way left
/// half filled.
void restore_trash_batch(const install_hooks& hooks, const std::string& batch)
{
    if(batch.empty() || !hooks.restore_from_trash)
    {
        return;
    }
    std::string err;
    if(!hooks.restore_from_trash(batch, err) && !err.empty())
    {
        APPLOG_WARNING("Putting back the files of trash batch {} failed: {}", batch, err);
    }
}

/// Puts the project back as it was before the commit started.
class commit_rollback
{
public:
    commit_rollback(const install_hooks& hooks) : hooks_(hooks)
    {
    }

    void add_created(const fs::path& file)
    {
        created_.push_back(file);
    }

    void add_backup(const fs::path& backup, const fs::path& original)
    {
        backups_.emplace_back(backup, original);
    }

    void set_trash_batch(const std::string& batch)
    {
        trash_batch_ = batch;
    }

    void run()
    {
        fs::error_code ec;
        for(const auto& file : created_)
        {
            fs::remove(file, ec);
            fs::remove(get_meta_path(file), ec);
        }
        for(const auto& [backup, original] : backups_)
        {
            asset_writer::atomic_copy_file(get_meta_path(backup), get_meta_path(original), ec);
            asset_writer::atomic_copy_file(backup, original, ec);
        }
        if(!trash_batch_.empty() && hooks_.restore_from_trash)
        {
            std::string err;
            hooks_.restore_from_trash(trash_batch_, err);
        }
    }

private:
    const install_hooks& hooks_;
    std::vector<fs::path> created_;
    std::vector<std::pair<fs::path, fs::path>> backups_;
    std::string trash_batch_;
};

/// Reads, checks and rewrites every file the install writes, into the stage. Touches nothing in the project.
auto stage_writes(const package_archive& archive,
                  const import_plan& plan,
                  const project_view& project,
                  const std::vector<hpp::uuid>& final_uids,
                  const fs::path& stage_dir,
                  std::vector<file_write>& writes,
                  std::string& err) -> bool
{
    std::vector<std::pair<std::string, std::string>> rewrites;
    for(std::size_t index = 0; index < plan.files.size(); ++index)
    {
        const auto& source_uid = plan.manifest.files[plan.files[index].index].uid;
        if(!final_uids[index].is_nil() && final_uids[index] != source_uid)
        {
            rewrites.emplace_back(hpp::to_string(source_uid), hpp::to_string(final_uids[index]));
        }
    }
    for(std::size_t index = 0; index < plan.files.size(); ++index)
    {
        const auto& planned = plan.files[index];
        const auto destination = get_destination(planned);
        if(destination.empty())
        {
            continue;
        }
        const auto& file = plan.manifest.files[planned.index];
        fs::byte_array_t bytes;
        fs::byte_array_t meta_bytes;
        if(!archive.read_file(file, bytes, err) || !archive.read_meta(file, meta_bytes, err))
        {
            return false;
        }
        rewrite_uids(bytes, rewrites);
        rewrite_uids(meta_bytes, rewrites);
        file_write write;
        write.index = index;
        write.key = destination;
        write.uid = final_uids[index];
        write.staged_file = stage_dir / fmt::format("{}.file", index);
        write.staged_meta = stage_dir / fmt::format("{}.meta", index);
        write.written_hash = fingerprint_bytes(bytes);
        if(!write_bytes(write.staged_file, bytes) || !write_bytes(write.staged_meta, meta_bytes))
        {
            err = "Could not stage the package's files in " + stage_dir.generic_string() + ".";
            return false;
        }
        fs::error_code ec;
        write.is_overwrite = planned.action == file_action::write && fs::exists(get_disk_path(project, destination), ec);
        writes.push_back(std::move(write));
    }
    return true;
}

/// The record of the package once the plan is applied.
auto make_record(const package_archive& archive,
                 const import_plan& plan,
                 const std::vector<hpp::uuid>& final_uids,
                 const std::vector<file_write>& writes) -> installed_package
{
    std::unordered_map<std::size_t, const file_write*> write_by_index;
    for(const auto& write : writes)
    {
        write_by_index.emplace(write.index, &write);
    }
    installed_package record;
    record.name = plan.manifest.name;
    record.display_name = plan.manifest.display_name;
    record.version = plan.manifest.version;
    record.root = plan.manifest.root;
    record.description = plan.manifest.description;
    record.author = plan.manifest.author;
    record.source_file = archive.get_location().filename().string();
    record.source_hash = archive.get_hash();
    record.installed_with = version::get_current().to_string();
    for(std::size_t index = 0; index < plan.files.size(); ++index)
    {
        const auto& planned = plan.files[index];
        const auto& file = plan.manifest.files[planned.index];
        const auto& uid = final_uids[index];
        if(uid.is_nil())
        {
            continue;
        }
        const auto* earlier = plan.installed ? plan.installed->find_by_source(file.uid) : nullptr;
        installed_file entry;
        entry.uid = uid;
        entry.source_uid = file.uid;
        entry.path = file.path;
        if(const auto found = write_by_index.find(index); found != write_by_index.end())
        {
            entry.hash = found->second->written_hash;
            entry.meta_hash = file.meta_hash;
        }
        else if(planned.status == file_status::unchanged || planned.status == file_status::present)
        {
            entry.hash = file.hash;
            entry.meta_hash = earlier != nullptr ? earlier->meta_hash : file.meta_hash;
        }
        else if(earlier != nullptr)
        {
            entry.hash = earlier->hash;
            entry.meta_hash = earlier->meta_hash;
        }
        record.files.push_back(std::move(entry));
    }
    return record;
}

/// Writes the staged files into the project; the trash takes what the plan replaces or removes first.
auto commit_writes(const import_plan& plan,
                   const project_view& project,
                   const fs::path& stage_dir,
                   const std::vector<file_write>& writes,
                   commit_rollback& rollback,
                   install_result& result,
                   const install_hooks& hooks) -> bool
{
    fs::error_code ec;
    for(const auto& write : writes)
    {
        if(!write.is_overwrite)
        {
            continue;
        }
        const auto original = get_disk_path(project, write.key);
        const auto backup = stage_dir / INSTALL_BACKUP_FOLDER / fmt::format("{}.file", write.index);
        fs::create_directories(backup.parent_path(), ec);
        if(!asset_writer::atomic_copy_file(original, backup, ec))
        {
            result.error = "Could not keep a copy of " + write.key + " before overwriting it.";
            return false;
        }
        asset_writer::atomic_copy_file(get_meta_path(original), get_meta_path(backup), ec);
        rollback.add_backup(backup, original);
    }
    std::vector<fs::path> trashed;
    for(const auto& planned : plan.files)
    {
        if(planned.action == file_action::replace)
        {
            trashed.push_back(get_disk_path(project, planned.existing_key));
            result.removed_keys.push_back(planned.existing_key);
        }
    }
    for(const auto& removal : plan.removals)
    {
        if(removal.is_removed)
        {
            trashed.push_back(get_disk_path(project, removal.key));
            result.removed_keys.push_back(removal.key);
        }
        else
        {
            ++result.kept_count;
        }
    }
    if(!trashed.empty())
    {
        const bool is_trashed = hooks.move_to_trash && hooks.move_to_trash(trashed, result.trash_batch, result.error);
        // The trash stops at the first file it cannot move, with the earlier ones moved: the rollback puts them back.
        rollback.set_trash_batch(result.trash_batch);
        if(!is_trashed)
        {
            if(result.error.empty())
            {
                result.error = "Could not move the replaced files to the trash.";
            }
            return false;
        }
    }
    for(const auto& write : writes)
    {
        const auto destination = get_disk_path(project, write.key);
        fs::create_directories(destination.parent_path(), ec);
        const bool existed = fs::exists(destination, ec);
        // The .meta first: the editor gives a file that shows up without one a new id.
        if(!asset_writer::atomic_copy_file(write.staged_meta, get_meta_path(destination), ec) ||
           !asset_writer::atomic_copy_file(write.staged_file, destination, ec))
        {
            result.error = "Could not write " + write.key + ": " + ec.message();
            if(!existed)
            {
                rollback.add_created(destination);
            }
            return false;
        }
        if(!existed)
        {
            rollback.add_created(destination);
        }
        result.written_keys.push_back(write.key);
    }
    return true;
}
} // namespace

auto install_package(const package_archive& archive,
                     const import_plan& plan,
                     const project_view& project,
                     const fs::path& stage_dir,
                     const install_hooks& hooks) -> install_result
{
    install_result result;
    if(!plan.problems.empty())
    {
        result.error = plan.problems.front();
        return result;
    }
    if(const auto clashes = find_script_clashes(plan, project); !clashes.empty())
    {
        result.error = clashes.front();
        return result;
    }
    if(!project.registry_error.empty())
    {
        result.error = "The project's record of its packages does not read: " + project.registry_error;
        return result;
    }
    fs::error_code ec;
    fs::remove_all(stage_dir, ec);
    fs::create_directories(stage_dir, ec);
    const auto final_uids = resolve_final_uids(plan, project);
    std::vector<file_write> writes;
    if(!stage_writes(archive, plan, project, final_uids, stage_dir, writes, result.error))
    {
        fs::remove_all(stage_dir, ec);
        return result;
    }
    commit_rollback rollback(hooks);
    // The project goes back as it was; the result keeps only the error.
    const auto undo = [&]() -> install_result
    {
        rollback.run();
        result.written_keys.clear();
        result.removed_keys.clear();
        result.trash_batch.clear();
        result.record = {};
        fs::remove_all(stage_dir, ec);
        return result;
    };
    if(!commit_writes(plan, project, stage_dir, writes, rollback, result, hooks))
    {
        return undo();
    }
    result.record = make_record(archive, plan, final_uids, writes);
    auto registry = project.registry;
    registry.put(result.record);
    if(!save_registry(get_registry_path(project.project_root), registry, result.error))
    {
        return undo();
    }
    fs::remove_all(stage_dir, ec);
    result.is_done = true;
    return result;
}

auto plan_removal(const project_view& project, const std::string& name) -> removal_plan
{
    removal_plan plan;
    const auto* installed = project.registry.find(name);
    if(installed == nullptr)
    {
        return plan;
    }
    plan.package = *installed;
    for(const auto& file : installed->files)
    {
        const auto found = project.database.find(file.uid);
        if(found == project.database.end())
        {
            continue;
        }
        planned_removal removal;
        removal.uid = file.uid;
        removal.key = found->second.location;
        removal.is_edited = fingerprint_file(get_disk_path(project, removal.key)) != file.hash;
        removal.is_removed = !removal.is_edited;
        plan.files.push_back(std::move(removal));
    }
    std::sort(plan.files.begin(),
              plan.files.end(),
              [](const planned_removal& lhs, const planned_removal& rhs)
              {
                  return lhs.key < rhs.key;
              });
    return plan;
}

auto remove_package(const removal_plan& plan, const project_view& project, const install_hooks& hooks) -> install_result
{
    install_result result;
    if(!project.registry_error.empty())
    {
        result.error = "The project's record of its packages does not read: " + project.registry_error;
        return result;
    }
    if(project.registry.find(plan.package.name) == nullptr)
    {
        result.error = plan.package.get_title() + " is not installed.";
        return result;
    }
    std::vector<fs::path> trashed;
    for(const auto& file : plan.files)
    {
        if(file.is_removed)
        {
            trashed.push_back(get_disk_path(project, file.key));
            result.removed_keys.push_back(file.key);
        }
        else
        {
            ++result.kept_count;
        }
    }
    if(!trashed.empty() && (!hooks.move_to_trash || !hooks.move_to_trash(trashed, result.trash_batch, result.error)))
    {
        if(result.error.empty())
        {
            result.error = "Could not move the package's files to the trash.";
        }
        restore_trash_batch(hooks, result.trash_batch);
        result.trash_batch.clear();
        result.removed_keys.clear();
        return result;
    }
    auto registry = project.registry;
    registry.remove(plan.package.name);
    if(!save_registry(get_registry_path(project.project_root), registry, result.error))
    {
        restore_trash_batch(hooks, result.trash_batch);
        result.trash_batch.clear();
        result.removed_keys.clear();
        return result;
    }
    result.is_done = true;
    return result;
}

} // namespace unravel::packages
