#include "files.h"
#include "keys.h"

#include <engine/assets/impl/asset_extensions.h>
#include <engine/meta/assets/asset_database.hpp>
#include <graphics/graphics.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
constexpr const char* COMPILED_SUFFIX = ".asset";
/// What the build adds to the player's file name for its stamp.
constexpr const char* PLAYER_STAMP_SUFFIX = ".stamp";
constexpr player_wording DESKTOP_PLAYER_WORDING{"The player beside this editor", "Build the game target too."};

auto make_copy_error(const char* action, const fs::path& from, const fs::path& to, const fs::error_code& ec) -> std::runtime_error
{
    return std::runtime_error(
        fmt::format("{} {} to {} failed: {}", action, from.generic_string(), to.generic_string(), ec.message()));
}

auto get_data_prefix(const std::string& protocol) -> std::string
{
    return ex::get_data_directory(protocol) + "/";
}

using file_visitor = std::function<void(const fs::path& file, const fs::path& relative)>;

/// Calls `visit` with each regular file under `folder` and its path relative to `folder`; throws when the listing
/// fails.
void for_each_file_or_throw(const fs::path& folder, const file_visitor& visit)
{
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_regular_file(ec))
        {
            visit(it->path(), it->path().lexically_relative(folder));
        }
    }
    if(ec)
    {
        throw std::runtime_error(fmt::format("Listing {} failed: {}", folder.generic_string(), ec.message()));
    }
}

/// The cooked files `is_shipped` keeps that have no compiled file to stand in for, each named once: an earlier
/// folder's wins.
void add_cooked_only_inputs(const fs::path& compiled,
                            const cooked_folders& cooked,
                            const compiled_file_filter& is_shipped,
                            std::vector<fs::pack_input>& inputs)
{
    const auto compiled_folder = ex::get_compiled_directory_no_slash();
    std::set<std::string> added;
    for(const auto& folder : cooked)
    {
        if(!is_present(folder))
        {
            continue;
        }
        for_each_file_or_throw(folder,
                               [&](const fs::path& file, const fs::path& relative)
                               {
                                   if(is_loose_only_file(file) || !is_shipped(file, relative) ||
                                      is_present(compiled / relative) || !added.insert(relative.generic_string()).second)
                                   {
                                       return;
                                   }
                                   inputs.push_back({compiled_folder + "/" + relative.generic_string(), file});
                               });
    }
}
} // namespace

auto is_present(const fs::path& path) -> bool
{
    fs::error_code ec;
    return fs::exists(path, ec);
}

auto get_compiled_relative_path(const std::string& key) -> fs::path
{
    const auto protocol = fs::extract_protocol(fs::path(key)).generic_string();
    const auto data_prefix = get_data_prefix(protocol);
    if(protocol.empty() || key.compare(0, data_prefix.size(), data_prefix) != 0)
    {
        return {};
    }
    fs::path relative = key.substr(data_prefix.size());
    relative += COMPILED_SUFFIX;
    return relative;
}

auto get_compiled_paths(const std::string& key, const fs::path& protocol_root, const std::set<std::string>& shader_variants)
    -> std::vector<fs::path>
{
    const auto relative = get_compiled_relative_path(key);
    // Scripts ship as their library, not as assets.
    if(relative.empty() || is_asset_key<script>(key))
    {
        return {};
    }
    const auto compiled = protocol_root / ex::get_compiled_directory_no_slash() / relative;
    if(!is_asset_key<gfx::shader>(key))
    {
        return {compiled};
    }
    std::vector<fs::path> paths;
    const auto& variants = shader_variants.empty() ? gfx::get_renderer_platform_supported_filename_extensions() : shader_variants;
    for(const auto& renderer_extension : variants)
    {
        auto path = compiled;
        path.concat(renderer_extension);
        paths.push_back(std::move(path));
    }
    return paths;
}

auto is_other_shader_variant(const fs::path& file, const std::set<std::string>& shader_variants) -> bool
{
    if(shader_variants.empty())
    {
        return false;
    }
    const auto extension = file.extension().generic_string();
    if(shader_variants.contains(extension))
    {
        return false;
    }
    for(int type = bgfx::RendererType::Noop; type < bgfx::RendererType::Count; ++type)
    {
        if(gfx::get_renderer_filename_extension(bgfx::RendererType::Enum(type)) == extension)
        {
            return true;
        }
    }
    return false;
}

auto get_source_key(const std::string& protocol, const fs::path& compiled_relative) -> std::string
{
    return get_data_prefix(protocol) + ex::get_source_path_from_compiled(compiled_relative).generic_string();
}

auto is_debug_companion_file(const fs::path& file) -> bool
{
    const auto extension = string_utils::to_lower(file.extension().generic_string());
    if(extension == ".pdb" || extension == ".mdb")
    {
        return true;
    }
    if(extension != ".xml")
    {
        return false;
    }
    auto library = file;
    library.replace_extension(".dll");
    return is_present(library);
}

auto is_loose_only_file(const fs::path& file) -> bool
{
    return string_utils::to_lower(file.extension().generic_string()) == ".dll" || is_debug_companion_file(file);
}

auto is_shipped_compiled_file(const fs::path& file, bool with_debug_symbols) -> bool
{
    const auto extension = file.extension().generic_string();
    if(extension == ".manifest" || extension == ".temp")
    {
        return false;
    }
    if(is_debug_companion_file(file))
    {
        return with_debug_symbols;
    }
    return !ex::is_format<script>(ex::get_source_path_from_compiled(file).extension().generic_string());
}

auto get_shipped_variant(const cooked_folders& cooked, const fs::path& relative, const fs::path& compiled_file) -> fs::path
{
    for(const auto& folder : cooked)
    {
        if(folder.empty())
        {
            continue;
        }
        auto variant = folder / relative;
        if(is_present(variant))
        {
            return variant;
        }
    }
    return compiled_file;
}

auto collect_pack_inputs(const fs::path& compiled,
                         const cooked_folders& cooked,
                         const fs::path& index,
                         const compiled_file_filter& is_shipped) -> std::vector<fs::pack_input>
{
    std::vector<fs::pack_input> inputs;
    const auto compiled_folder = ex::get_compiled_directory_no_slash();
    for_each_file_or_throw(compiled,
                           [&](const fs::path& file, const fs::path& relative)
                           {
                               if(!is_loose_only_file(file) && is_shipped(file, relative))
                               {
                                   inputs.push_back({compiled_folder + "/" + relative.generic_string(),
                                                     get_shipped_variant(cooked, relative, file)});
                               }
                           });
    add_cooked_only_inputs(compiled, cooked, is_shipped, inputs);
    inputs.push_back({ex::get_asset_index_file_name(), index});
    std::sort(inputs.begin(),
              inputs.end(),
              [](const fs::pack_input& lhs, const fs::pack_input& rhs)
              {
                  return lhs.name < rhs.name;
              });
    return inputs;
}

void mirror_or_throw(const fs::path& from, const fs::path& to, const fs::mirror_filter& include)
{
    fs::error_code ec;
    fs::mirror_stats stats;
    if(!fs::mirror_directory(from, to, include, stats, ec))
    {
        throw make_copy_error("Copying", from, to, ec);
    }
    APPLOG_TRACE("Mirrored {} -> {}: {} copied, {} unchanged, {} removed",
                 from.generic_string(),
                 to.generic_string(),
                 stats.copied,
                 stats.unchanged,
                 stats.removed);
}

void copy_file_or_throw(const fs::path& from, const fs::path& to)
{
    fs::error_code ec;
    bool is_copied = false;
    if(!fs::copy_file_if_changed(from, to, is_copied, ec))
    {
        throw make_copy_error("Copying", from, to, ec);
    }
}

auto write_file(const fs::path& file, const void* data, std::size_t size) -> bool
{
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    stream.close();
    return !stream.fail();
}

void write_text_or_throw(const fs::path& file, const std::string& text)
{
    if(!write_file(file, text.data(), text.size()))
    {
        throw std::runtime_error(fmt::format("Writing {} failed.", file.generic_string()));
    }
}

void remove_file_or_throw(const fs::path& file)
{
    fs::error_code ec;
    fs::remove(file, ec);
    if(ec)
    {
        throw std::runtime_error(fmt::format("Removing {} failed: {}", file.generic_string(), ec.message()));
    }
}

auto read_stamp(const fs::path& file) -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> values;
    std::ifstream stream(file);
    std::string line;
    while(std::getline(stream, line))
    {
        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const auto separator = line.find('=');
        if(separator != std::string::npos)
        {
            values[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }
    return values;
}

auto get_version_problem(const std::string& player_version, const std::string& engine_version, const player_wording& wording)
    -> std::string
{
    if(player_version.empty())
    {
        return fmt::format("{} does not say which engine version built it. {}", wording.player, wording.advice);
    }
    if(player_version != engine_version)
    {
        return fmt::format("{} comes from engine version {}, and the editor is {}. {}",
                           wording.player,
                           player_version,
                           engine_version,
                           wording.advice);
    }
    return {};
}

auto get_player_version_problem(const fs::path& player, const std::string& engine_version) -> std::string
{
    auto stamp_file = player;
    stamp_file += PLAYER_STAMP_SUFFIX;
    return get_version_problem(read_stamp(stamp_file)["engine_version"], engine_version, DESKTOP_PLAYER_WORDING);
}

void copy_cooked_or_throw(const cooked_folders& cooked, const fs::path& destination, const compiled_file_filter& is_shipped)
{
    // The last folder first, so an earlier folder's variant is the one copied last.
    for(auto folder = cooked.rbegin(); folder != cooked.rend(); ++folder)
    {
        if(!is_present(*folder))
        {
            continue;
        }
        for_each_file_or_throw(*folder,
                               [&](const fs::path& file, const fs::path& relative)
                               {
                                   if(is_shipped(file, relative))
                                   {
                                       copy_file_or_throw(file, destination / relative);
                                   }
                               });
    }
}

void write_asset_index(const asset_database::database_t& database,
                       const fs::path& file,
                       const std::function<bool(const std::string& location)>& include)
{
    asset_database::database_t kept;
    for(const auto& [uid, meta] : database)
    {
        if(include(meta.location))
        {
            kept.emplace(uid, meta);
        }
    }
    asset_database index;
    index.set_database(kept);
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if(!save_database_to_file(file, index))
    {
        throw std::runtime_error(fmt::format("Writing the asset index {} failed", file.generic_string()));
    }
}

auto is_binary_file(const fs::path& file) -> bool
{
#ifdef _WIN32
    const auto extension = string_utils::to_lower(file.extension().generic_string());
    return extension == ".dll" || extension == ".exe";
#else
    // Shared libraries carry ".so", versioned or not; executables carry no extension and the exec bit.
    const auto name = file.filename().generic_string();
    if(name.ends_with(".so") || name.find(".so.") != std::string::npos)
    {
        return true;
    }
    fs::error_code ec;
    const auto permissions = fs::status(file, ec).permissions();
    return !file.has_extension() && !ec && (permissions & fs::perms::owner_exec) != fs::perms::none;
#endif
}

auto get_binary_key(const fs::path& file) -> std::string
{
    return fs::make_path_key(file.filename().generic_string());
}

void remove_stale_binaries(const fs::path& deploy_root, const std::set<std::string>& shipped)
{
    fs::error_code ec;
    std::vector<fs::path> stale;
    for(const auto& entry : fs::directory_iterator(deploy_root, ec))
    {
        if(entry.is_regular_file(ec) && is_binary_file(entry.path()) && !shipped.contains(get_binary_key(entry.path())))
        {
            stale.push_back(entry.path());
        }
    }
    for(const auto& file : stale)
    {
        APPLOG_TRACE("Removing {}, which this build no longer ships", file.generic_string());
        remove_file_or_throw(file);
    }
}

void prune_folder_or_throw(const fs::path& folder, const kept_entry_filter& is_kept)
{
    // Listed in full first: removing entries while a directory iterator walks them may skip some.
    std::vector<fs::path> removed;
    fs::error_code ec;
    for(fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        if(!is_kept(it->path().filename()))
        {
            removed.push_back(it->path());
        }
    }
    if(ec && ec != std::errc::no_such_file_or_directory)
    {
        throw std::runtime_error(fmt::format("Listing {} failed: {}", folder.generic_string(), ec.message()));
    }
    for(const auto& entry : removed)
    {
        fs::remove_all(entry, ec);
        if(ec)
        {
            throw std::runtime_error(fmt::format("Removing {} failed: {}", entry.generic_string(), ec.message()));
        }
    }
}

void remove_siblings(const fs::path& kept)
{
    prune_folder_or_throw(kept.parent_path(),
                          [&kept](const fs::path& name)
                          {
                              return name == kept.filename();
                          });
}

} // namespace unravel::deployment
