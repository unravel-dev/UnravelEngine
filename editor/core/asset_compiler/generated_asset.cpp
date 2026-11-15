#include "generated_asset.h"
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>

#include <engine/meta/assets/asset_database.hpp>

#include <filesystem/file_fingerprint.h>
#include <logging/logging.h>

#include <fstream>
#include <iterator>
#include <mutex>

namespace unravel
{
namespace generated_asset
{
namespace
{

/// Generated assets are text; line endings do not count, so a checkout that converts them is not an edit.
constexpr bool k_normalize_line_endings = true;

auto get_meta_path(const fs::path& path) -> fs::path
{
    return asset_writer::resolve_meta_file_for_key(fs::convert_to_protocol(path).generic_string());
}

auto get_file_fingerprint(const fs::path& path) -> std::string
{
    return fs::hash_file_fingerprint(path, k_normalize_line_endings);
}

/**
 * @brief Generated content, staged in the system temp folder where no asset watcher sees it. The file that is
 * finally written is a copy of it, so it holds exactly the bytes that were fingerprinted.
 */
class staged_content
{
public:
    explicit staged_content(const content_writer_t& write_content)
    {
        fs::error_code err;
        if(!asset_writer::make_temp_path(fs::temp_directory_path(err), path_, err))
        {
            APPLOG_ERROR("Generated asset: no temp file to stage content in: {}", err.message());
            return;
        }
        try
        {
            write_content(path_);
            fingerprint_ = get_file_fingerprint(path_);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Generated asset: producing content failed: {}", e.what());
        }
    }

    staged_content(const staged_content&) = delete;
    auto operator=(const staged_content&) -> staged_content& = delete;

    ~staged_content()
    {
        fs::error_code err;
        fs::remove(path_, err);
    }

    /// Empty when the content could not be produced.
    auto get_fingerprint() const -> const std::string&
    {
        return fingerprint_;
    }

    auto copy_to(const fs::path& destination, fs::error_code& err) const -> bool
    {
        return asset_writer::atomic_copy_file(path_, destination, err);
    }

private:
    fs::path path_;
    std::string fingerprint_;
};

/// Writes the .meta of @p path and updates the database copy with it.
auto save_meta(asset_manager& am, const fs::path& path, const asset_meta& meta) -> bool
{
    const auto meta_path = get_meta_path(path);
    fs::error_code err;
    asset_writer::atomic_write_file(meta_path,
                                    [&](const fs::path& temp)
                                    {
                                        save_to_file(temp.string(), meta);
                                    },
                                    err);
    if(err)
    {
        APPLOG_ERROR("Generated asset: failed to write {}: {}", meta_path.string(), err.message());
        return false;
    }
    am.add_asset_info_for_path(path, meta, true);
    return true;
}

/// Records @p source_uid as the source of @p meta's asset; false when it already was.
auto set_source(asset_meta& meta, const hpp::uuid& source_uid) -> bool
{
    if(meta.generated.source_uid == source_uid)
    {
        return false;
    }
    meta.generated.source_uid = source_uid;
    return true;
}

/// Whether the .meta holds the fingerprint of an import's write, rather than no record or only the source.
auto has_write_record(const asset_meta& meta) -> bool
{
    return !meta.generated.fingerprint.empty();
}

/// Whether the file whose current fingerprint is @p current still holds what its import last wrote.
auto is_owned_by_import(const asset_meta& meta, const std::string& current) -> bool
{
    return meta.generated.is_generated() && !meta.generated.fingerprint.empty()
           && meta.generated.fingerprint == current;
}

auto load_meta(const fs::path& path, const hpp::uuid& uid) -> asset_meta
{
    asset_meta meta;
    load_from_file(get_meta_path(path).string(), meta);
    if(meta.uid.is_nil())
    {
        meta.uid = uid;
    }
    if(meta.type.empty())
    {
        meta.type = path.extension().string();
    }
    return meta;
}

/// Records the staged content as the import's and writes it: the .meta first, then the file.
auto commit(asset_manager& am,
            const fs::path& path,
            asset_meta& meta,
            const hpp::uuid& source_uid,
            const staged_content& content) -> write_result
{
    meta.generated = {source_uid, content.get_fingerprint()};
    if(!save_meta(am, path, meta))
    {
        return write_result::failed;
    }
    fs::error_code err;
    if(!content.copy_to(path, err))
    {
        APPLOG_ERROR("Generated asset: failed to write {}: {}", path.string(), err.message());
        return write_result::failed;
    }
    return write_result::written;
}

/// The file already holds this import: only a missing or stale record is written.
auto record_unchanged(asset_manager& am,
                      const fs::path& path,
                      asset_meta& meta,
                      const hpp::uuid& source_uid,
                      const std::string& fingerprint) -> write_result
{
    const bool is_record_stale = set_source(meta, source_uid) || meta.generated.fingerprint != fingerprint;
    meta.generated.fingerprint = fingerprint;
    if(is_record_stale && !save_meta(am, path, meta))
    {
        return write_result::failed;
    }
    return write_result::unchanged;
}

/// The file was edited: it stays as it is, and only learns its source when it did not know it.
auto keep_edited(asset_manager& am, const fs::path& path, asset_meta& meta, const hpp::uuid& source_uid)
    -> write_result
{
    if(set_source(meta, source_uid) && !save_meta(am, path, meta))
    {
        return write_result::failed;
    }
    return write_result::kept_edited;
}

/// The text assets that can name another asset: by its id, or by its path for documents and scripts.
auto get_referencing_formats() -> const std::vector<std::string>&
{
    static const std::vector<std::string> formats = []
    {
        std::vector<std::string> result;
        for(const auto* list : {&ex::get_suported_formats<material>(),
                                &ex::get_suported_dependencies_formats<material>(),
                                &ex::get_suported_formats<prefab>(),
                                &ex::get_suported_formats<scene_prefab>(),
                                &ex::get_suported_formats<script>(),
                                &ex::get_suported_formats<ui_tree>(),
                                &ex::get_suported_formats<style_sheet>()})
        {
            result.insert(result.end(), list->begin(), list->end());
        }
        return result;
    }();
    return formats;
}

auto is_referencing_file(const fs::path& path) -> bool
{
    const auto& formats = get_referencing_formats();
    return std::find(formats.begin(), formats.end(), path.extension().string()) != formats.end();
}

/// A file to delete once no text asset names it, by either of the two ways one can.
struct unreferenced_candidate
{
    fs::path path;
    std::string uid;
    std::string file_name;
    bool is_referenced{};
};

auto make_candidate(const fs::path& path) -> unreferenced_candidate
{
    asset_meta meta;
    load_from_file(get_meta_path(path).string(), meta);
    return {path, meta.uid.is_nil() ? std::string() : hpp::to_string(meta.uid), path.filename().string()};
}

/// Marks the candidates @p text names; true while some are not named yet.
auto mark_referenced(const std::string& text, std::vector<unreferenced_candidate>& candidates) -> bool
{
    bool has_unreferenced = false;
    for(auto& candidate : candidates)
    {
        if(!candidate.is_referenced)
        {
            candidate.is_referenced = (!candidate.uid.empty() && text.find(candidate.uid) != std::string::npos) ||
                                      text.find(candidate.file_name) != std::string::npos;
            has_unreferenced = has_unreferenced || !candidate.is_referenced;
        }
    }
    return has_unreferenced;
}

/// Reads every text asset under @p root, until each candidate is named or none is left to read.
void mark_referenced_under(const fs::path& root, std::vector<unreferenced_candidate>& candidates)
{
    fs::error_code err;
    for(fs::recursive_directory_iterator it(root, err), end; it != end && !err; it.increment(err))
    {
        fs::error_code entry_err;
        if(!it->is_regular_file(entry_err) || !is_referencing_file(it->path()))
        {
            continue;
        }
        std::ifstream stream(it->path(), std::ios::binary);
        const std::string text(std::istreambuf_iterator<char>(stream), {});
        if(!mark_referenced(text, candidates))
        {
            return;
        }
    }
}

} // namespace

auto write_file(asset_manager& am,
                const fs::path& path,
                const hpp::uuid& uid,
                const hpp::uuid& source_uid,
                const content_writer_t& write_content,
                const older_import_check_t& is_older_import) -> write_result
{
    const staged_content content(write_content);
    if(content.get_fingerprint().empty())
    {
        return write_result::failed;
    }
    auto meta = load_meta(path, uid);
    fs::error_code err;
    if(!fs::exists(path, err))
    {
        return commit(am, path, meta, source_uid, content);
    }
    const auto current = get_file_fingerprint(path);
    if(current.empty())
    {
        // Unreadable (locked) is not known to be unedited, so it is left alone this time.
        APPLOG_WARNING("Generated asset: could not read {}; it was not updated", path.string());
        return write_result::failed;
    }
    if(current == content.get_fingerprint())
    {
        return record_unchanged(am, path, meta, source_uid, current);
    }
    if(is_owned_by_import(meta, current))
    {
        return commit(am, path, meta, source_uid, content);
    }
    if(has_write_record(meta) || !is_older_import || !is_older_import(path))
    {
        return keep_edited(am, path, meta, source_uid);
    }
    const auto result = commit(am, path, meta, source_uid, content);
    return result == write_result::written ? write_result::replaced_untracked : result;
}

auto adopt_former_file(asset_manager& am, const fs::path& former_path, const fs::path& path) -> bool
{
    // Imports compile in parallel, and two of them may claim the same former file.
    static std::mutex adopt_mutex;
    std::lock_guard<std::mutex> lock(adopt_mutex);
    fs::error_code err;
    if(former_path == path || fs::exists(path, err) || !fs::exists(former_path, err))
    {
        return false;
    }
    const auto former_meta_path = get_meta_path(former_path);
    const auto meta_path = get_meta_path(path);
    asset_meta meta;
    const bool has_meta = load_from_file(former_meta_path.string(), meta);
    if(!asset_writer::atomic_rename_file(former_path, path, err))
    {
        APPLOG_WARNING("Generated asset: could not rename {} to {}: {}", former_path.string(), path.string(), err.message());
        return false;
    }
    // The editor's meta syncer moves the .meta as well when it sees the rename; whichever is second finds
    // it gone, which is not a failure.
    if(has_meta && !asset_writer::atomic_rename_file(former_meta_path, meta_path, err) && !fs::exists(meta_path, err))
    {
        APPLOG_WARNING("Generated asset: could not move {} to {}", former_meta_path.string(), meta_path.string());
    }
    am.rename_asset_info_for_path(former_path, path);
    if(!meta.uid.is_nil())
    {
        am.add_asset_info_for_path(path, meta, false);
    }
    APPLOG_INFO("Renamed generated {} to {}", former_path.filename().string(), path.filename().string());
    return true;
}

auto is_edited(const fs::path& path, const asset_generated_info& generated) -> bool
{
    return generated.is_generated() && get_file_fingerprint(path) != generated.fingerprint;
}

auto release_to_import(asset_manager& am, const fs::path& path) -> bool
{
    asset_meta meta;
    if(!load_from_file(get_meta_path(path).string(), meta) || !meta.generated.is_generated())
    {
        return false;
    }
    meta.generated.fingerprint = get_file_fingerprint(path);
    return save_meta(am, path, meta);
}

auto remove_unreferenced_files(asset_manager& am, const fs::path& root, const std::vector<fs::path>& paths)
    -> std::vector<fs::path>
{
    std::vector<unreferenced_candidate> candidates;
    std::transform(paths.begin(), paths.end(), std::back_inserter(candidates), make_candidate);
    if(!candidates.empty())
    {
        mark_referenced_under(root, candidates);
    }
    std::vector<fs::path> removed;
    for(const auto& candidate : candidates)
    {
        fs::error_code err;
        if(candidate.is_referenced || !fs::remove(candidate.path, err))
        {
            continue;
        }
        fs::remove(get_meta_path(candidate.path), err);
        am.remove_asset_info_for_path(candidate.path);
        removed.push_back(candidate.path);
    }
    return removed;
}

} // namespace generated_asset
} // namespace unravel
