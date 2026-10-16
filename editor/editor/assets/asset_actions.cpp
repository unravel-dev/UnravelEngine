#include "asset_actions.h"

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/assets/impl/generated_asset.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/animation/animation_controller.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/physics/physics_material.h>
#include <engine/rendering/material.h>
#include <engine/scripting/script.h>

#include <filesystem/watcher.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace unravel::asset_actions
{

auto create_animation_controller(rtti::context& ctx, const fs::path& absolute_path)
    -> asset_handle<animation_controller>
{
    auto& am = ctx.get_cached<asset_manager>();
    auto key = fs::convert_to_protocol(absolute_path).generic_string();

    auto controller = std::make_shared<animation_controller>();
    auto& layer = controller->layers.emplace_back();
    layer.name = "Base";
    auto& state = layer.states.emplace_back();
    state.name = "Idle";
    layer.default_state = state.name;

    auto handle = am.get_asset_from_instance<animation_controller>(key, controller);
    if(!handle || !asset_writer::atomic_save_to_file(handle.id(), handle))
    {
        return {};
    }
    return handle;
}

auto create_physics_material(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<physics_material>
{
    auto& am = ctx.get_cached<asset_manager>();
    const auto key = fs::convert_to_protocol(absolute_path).generic_string();
    auto handle = am.get_asset_from_instance<physics_material>(key, std::make_shared<physics_material>());
    if(!handle || !asset_writer::atomic_save_to_file(handle.id(), handle))
    {
        return {};
    }
    return handle;
}

auto create_material(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<material>
{
    auto& am = ctx.get_cached<asset_manager>();
    const auto key = fs::convert_to_protocol(absolute_path).generic_string();
    auto handle = am.get_asset_from_instance<material>(key, std::make_shared<pbr_material>());
    if(!handle || !asset_writer::atomic_save_to_file(handle.id(), handle))
    {
        return {};
    }
    return handle;
}

auto resolve_asset_source_path(const std::string& asset_key) -> fs::path
{
    return fs::absolute(fs::resolve_protocol(asset_key).string());
}

auto resolve_asset_source_path(const fs::path& path) -> fs::path
{
    if(fs::has_known_protocol(path))
    {
        return resolve_asset_source_path(path.generic_string());
    }
    return fs::absolute(path);
}

auto can_reimport(const fs::path& absolute_path) -> bool
{
    fs::error_code ec;
    if(!fs::exists(absolute_path, ec) || !fs::is_regular_file(absolute_path, ec))
    {
        return false;
    }

    const auto extension = absolute_path.extension().string();
    for(const auto& formats : ex::get_all_formats())
    {
        for(const auto& format : formats)
        {
            if(format == extension)
            {
                return true;
            }
        }
    }

    return false;
}

void reimport_path(const fs::path& absolute_path)
{
    const fs::path resolved = resolve_asset_source_path(absolute_path);
    fs::error_code ec;
    if(!fs::exists(resolved, ec) || !fs::is_regular_file(resolved, ec))
    {
        APPLOG_WARNING("Reimport skipped: '{}' is not a regular file", resolved.generic_string());
        return;
    }

    fs::watcher::touch(resolved, false);
}

void reimport_key(const std::string& asset_key)
{
    reimport_path(resolve_asset_source_path(asset_key));
}

auto is_valid_csharp_identifier(const std::string& name) -> bool
{
    if(name.empty() || (std::isdigit(static_cast<unsigned char>(name.front())) != 0))
    {
        return false;
    }
    return std::all_of(name.begin(),
                       name.end(),
                       [](unsigned char c)
                       {
                           return std::isalnum(c) != 0 || c == '_';
                       });
}

namespace
{
auto report_error(std::string* error, const std::string& message) -> bool
{
    APPLOG_ERROR("{}", message);
    if(error)
    {
        *error = message;
    }
    return false;
}

auto copy_engine_template(const char* template_key, const fs::path& dst, std::string* error) -> bool
{
    fs::error_code ec;
    const auto source = fs::resolve_protocol(template_key);
    if(!fs::exists(source, ec))
    {
        return report_error(error, std::string("Template not found: ") + template_key);
    }
    const auto absolute = fs::absolute(dst);
    fs::create_directories(absolute.parent_path(), ec);
    ec.clear();
    if(!asset_writer::atomic_copy_file(source, absolute, ec) || ec)
    {
        return report_error(error,
                            "Copy failed: " + absolute.generic_string() + (ec ? " (" + ec.message() + ")" : ""));
    }
    return true;
}
} // namespace

auto read_text_file(const fs::path& path, std::string& out, std::string* error) -> bool
{
    std::ifstream input(path, std::ios::binary);
    if(!input.is_open())
    {
        if(error)
        {
            *error = "Failed to open file: " + path.generic_string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    out = buffer.str();
    return true;
}

auto write_text_file(const fs::path& dst, const std::string& content, std::string* error) -> bool
{
    fs::error_code err;
    const auto absolute = fs::absolute(dst);
    fs::create_directories(absolute.parent_path(), err);
    err.clear();

    try
    {
        asset_writer::atomic_write_file(
            absolute,
            [&](const fs::path& temp)
            {
                std::ofstream output(temp, std::ios::binary | std::ios::trunc);
                if(!output.is_open())
                {
                    throw std::runtime_error("Failed to open temp file for write: " + temp.generic_string());
                }
                output.write(content.data(), static_cast<std::streamsize>(content.size()));
                if(!output.good())
                {
                    throw std::runtime_error("Failed writing temp file: " + temp.generic_string());
                }
            },
            err);
    }
    catch(const std::exception& ex)
    {
        return report_error(error, ex.what());
    }

    if(err)
    {
        return report_error(error, "Atomic write failed: " + absolute.generic_string() + " (" + err.message() + ")");
    }
    return true;
}

auto create_ui_document(const fs::path& dst, std::string* error) -> bool
{
    return copy_engine_template("engine:/data/ui/template.rhtml", dst, error);
}

auto create_style_sheet(const fs::path& dst, std::string* error) -> bool
{
    return copy_engine_template("engine:/data/ui/template.rcss", dst, error);
}

auto create_script_from_template(const fs::path& template_path, const fs::path& dst, std::string* error) -> bool
{
    std::string content;
    if(!read_text_file(template_path, content))
    {
        return report_error(error, "Script template not found: " + template_path.generic_string());
    }
    string_utils::alterable::replace(content, "#SCRIPTNAME#", dst.stem().string());
    return write_text_file(dst, content, error);
}

auto get_free_copy_path(const fs::path& directory, const std::string& stem, const std::string& extension) -> fs::path
{
    int index = 0;
    fs::error_code ec;
    while(fs::exists(directory / (fmt::format("{} ({})", stem, index) + extension), ec))
    {
        ++index;
    }
    return directory / (fmt::format("{} ({})", stem, index) + extension);
}

void sync_script_class_name(const fs::path& script_path, const std::string& old_stem, const std::string& new_stem)
{
    // Only plain identifiers can be class names (and only they are safe in the pattern below).
    if(!is_valid_csharp_identifier(old_stem) || !is_valid_csharp_identifier(new_stem))
    {
        return;
    }
    std::string content;
    if(!read_text_file(script_path, content))
    {
        return;
    }
    const std::regex identifier(fmt::format("\\b{}\\b", old_stem));
    if(!std::regex_search(content, identifier))
    {
        return;
    }
    write_text_file(script_path, std::regex_replace(content, identifier, new_stem));
}

auto rename_entry(const fs::path& from, const fs::path& to, std::string* error) -> bool
{
    fs::error_code ec;
    if(fs::exists(to, ec))
    {
        return report_error(error, "Already exists: " + to.generic_string());
    }
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if(ec)
    {
        return report_error(error, "Could not move " + from.generic_string() + " to " + to.generic_string() + ": " + ec.message());
    }
    if(to.extension().generic_string() == ex::get_format<script>() && from.stem() != to.stem())
    {
        sync_script_class_name(to, from.stem().string(), to.stem().string());
    }
    return true;
}

auto duplicate_entry(const fs::path& absolute_path, std::string* error) -> fs::path
{
    fs::error_code ec;
    const bool is_directory = fs::is_directory(absolute_path, ec);
    const auto extension = is_directory ? std::string{} : absolute_path.extension().string();
    const auto copy = get_free_copy_path(absolute_path.parent_path(), absolute_path.stem().string(), extension);
    if(!is_directory)
    {
        fs::copy_file(absolute_path, copy, ec);
        if(ec)
        {
            report_error(error, "Could not copy " + absolute_path.generic_string() + ": " + ec.message());
            return {};
        }
        return copy;
    }
    const auto meta_format = ex::get_meta_format();
    for(fs::recursive_directory_iterator it(absolute_path, ec), end; it != end && !ec; it.increment(ec))
    {
        const auto target = copy / it->path().lexically_relative(absolute_path);
        fs::error_code entry_ec;
        if(it->is_directory(entry_ec))
        {
            fs::create_directories(target, entry_ec);
        }
        else if(it->path().extension().generic_string() != meta_format)
        {
            fs::create_directories(target.parent_path(), entry_ec);
            fs::copy_file(it->path(), target, entry_ec);
        }
        if(entry_ec)
        {
            report_error(error, "Could not copy " + it->path().generic_string() + ": " + entry_ec.message());
            return {};
        }
    }
    return copy;
}

namespace
{
auto meta_path_of(const fs::path& asset_path) -> fs::path
{
    return fs::path(asset_path.generic_string() + ex::get_meta_format());
}

auto move_path(const fs::path& from, const fs::path& to, std::string* error) -> bool
{
    fs::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    return !ec ||
           report_error(error, "Could not move " + from.generic_string() + " to " + to.generic_string() + ": " + ec.message());
}

/// Moves an asset with its .meta, or a folder with everything in it. An asset whose .meta cannot
/// follow goes back, so it never ends up without its id.
auto move_with_meta(const fs::path& from, const fs::path& to, std::string* error) -> bool
{
    fs::error_code ec;
    const auto meta = meta_path_of(from);
    const bool has_meta = !fs::is_directory(from, ec) && fs::exists(meta, ec);
    if(!move_path(from, to, error))
    {
        return false;
    }
    if(has_meta && !move_path(meta, meta_path_of(to), error))
    {
        std::string ignored;
        move_path(to, from, &ignored);
        return false;
    }
    return true;
}

auto get_data_directory() -> fs::path
{
    return fs::absolute(fs::resolve_protocol(ex::get_data_directory_no_slash("app:/")));
}

/// A batch name that sorts by time: yyyymmdd-hhmmss-mmm.
auto make_trash_batch_name() -> std::string
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::ostringstream name;
    name << std::put_time(&local_tm, "%Y%m%d-%H%M%S") << "-" << std::setw(3) << std::setfill('0') << milliseconds;
    return name.str();
}
} // namespace

auto get_trash_directory() -> fs::path
{
    return fs::absolute(fs::resolve_protocol("app:/editor/trash"));
}

auto move_to_trash(const std::vector<fs::path>& paths, std::string& batch, std::string* error) -> bool
{
    batch = make_trash_batch_name();
    const auto batch_root = get_trash_directory() / batch;
    const auto data_root = get_data_directory();
    // Paused, the watcher never meets an asset whose .meta has already left.
    fs::watcher::scoped_pause pause;
    for(const auto& path : paths)
    {
        if(!move_with_meta(path, batch_root / path.lexically_relative(data_root), error))
        {
            return false;
        }
    }
    return true;
}

auto restore_from_trash(const std::string& batch, std::vector<fs::path>& restored, std::string* error) -> bool
{
    const auto batch_root = get_trash_directory() / batch;
    fs::error_code ec;
    if(batch.empty() || batch.find_first_of("/\\") != std::string::npos || batch.find("..") != std::string::npos ||
       !fs::is_directory(batch_root, ec))
    {
        return report_error(error, "No trash batch " + batch);
    }
    std::vector<fs::path> files;
    std::vector<fs::path> folders;
    const auto meta_format = ex::get_meta_format();
    for(fs::recursive_directory_iterator it(batch_root, ec), end; it != end && !ec; it.increment(ec))
    {
        fs::error_code entry_ec;
        if(it->is_directory(entry_ec))
        {
            folders.push_back(it->path());
        }
        else if(it->path().extension().generic_string() != meta_format)
        {
            files.push_back(it->path());
        }
    }
    const auto data_root = get_data_directory();
    std::vector<std::string> taken;
    {
        fs::watcher::scoped_pause pause;
        for(const auto& folder : folders)
        {
            fs::create_directories(data_root / folder.lexically_relative(batch_root), ec);
        }
        for(const auto& file : files)
        {
            const auto target = data_root / file.lexically_relative(batch_root);
            fs::error_code exists_ec;
            std::string move_error;
            if(fs::exists(target, exists_ec) || !move_with_meta(file, target, &move_error))
            {
                taken.push_back(file.lexically_relative(batch_root).generic_string());
                continue;
            }
            restored.push_back(target);
        }
    }
    if(!taken.empty())
    {
        return report_error(error, fmt::format("Left in the trash, their place is taken: {}", fmt::join(taken, ", ")));
    }
    fs::remove_all(batch_root, ec);
    return true;
}

auto list_trash_batches() -> std::vector<std::string>
{
    std::vector<std::string> batches;
    fs::error_code ec;
    for(fs::directory_iterator it(get_trash_directory(), ec), end; it != end && !ec; it.increment(ec))
    {
        if(it->is_directory(ec))
        {
            batches.push_back(it->path().filename().string());
        }
    }
    std::sort(batches.rbegin(), batches.rend());
    return batches;
}

namespace
{
/// Whether a generated file was edited, as last worked out, with what it was worked out from.
struct edited_state
{
    bool is_known{};
    fs::file_time_type write_time{};
    std::string fingerprint;
    bool is_edited{};
};

/// Hashes the file only when its write time or its record changed since it was last hashed.
auto is_generated_file_edited(const hpp::uuid& uid, const fs::path& path, const asset_generated_info& generated) -> bool
{
    static std::unordered_map<hpp::uuid, edited_state> states;
    fs::error_code err;
    const auto write_time = fs::last_write_time(path, err);
    auto& state = states[uid];
    if(!state.is_known || state.write_time != write_time || state.fingerprint != generated.fingerprint)
    {
        state = {true, write_time, generated.fingerprint, generated_asset::is_edited(path, generated)};
    }
    return state.is_edited;
}

auto load_generated_info(const std::string& asset_key) -> asset_generated_info
{
    asset_meta meta;
    load_from_file(asset_writer::resolve_meta_file_for_key(asset_key).string(), meta);
    return meta.generated;
}
} // namespace

auto get_generated_status(asset_manager& am, const hpp::uuid& uid) -> generated_status
{
    generated_status status;
    if(uid.is_nil())
    {
        return status;
    }
    const auto info = am.get_metadata(uid);
    const auto& generated = info.meta.generated;
    if(!generated.is_generated())
    {
        return status;
    }
    status.is_generated = true;
    status.source_key = am.get_metadata(generated.source_uid).location;
    status.source_name = fs::path(status.source_key).filename().string();
    status.is_edited = is_generated_file_edited(uid, resolve_asset_source_path(info.location), generated);
    return status;
}

auto reset_to_imported(rtti::context& ctx, const std::string& asset_key, std::string* error) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    const auto generated = load_generated_info(asset_key);
    if(!generated.is_generated())
    {
        return report_error(error, fmt::format("{} was not generated by an import", asset_key));
    }
    const auto source_key = am.get_metadata(generated.source_uid).location;
    if(source_key.empty())
    {
        return report_error(error, fmt::format("The model {} was generated from is no longer in the project", asset_key));
    }
    if(!generated_asset::release_to_import(am, resolve_asset_source_path(asset_key)))
    {
        return report_error(error, fmt::format("Could not update the .meta of {}", asset_key));
    }
    reimport_key(source_key);
    return true;
}

} // namespace unravel::asset_actions
