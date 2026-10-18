#include "files.h"

#include <engine/assets/impl/asset_extensions.h>
#include <engine/meta/assets/asset_database.hpp>
#include <graphics/graphics.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
constexpr const char* COMPILED_SUFFIX = ".asset";

auto make_copy_error(const char* action, const fs::path& from, const fs::path& to, const fs::error_code& ec) -> std::runtime_error
{
    return std::runtime_error(
        fmt::format("{} {} to {} failed: {}", action, from.generic_string(), to.generic_string(), ec.message()));
}

auto get_data_prefix(const std::string& protocol) -> std::string
{
    return ex::get_data_directory(protocol) + "/";
}
} // namespace

auto is_script_key(const std::string& key) -> bool
{
    return ex::is_format<script>(fs::path(key).extension().generic_string());
}

auto get_compiled_paths(const std::string& key, const fs::path& protocol_root, const std::set<std::string>& shader_variants)
    -> std::vector<fs::path>
{
    const auto protocol = fs::extract_protocol(fs::path(key)).generic_string();
    const auto data_prefix = get_data_prefix(protocol);
    if(protocol.empty() || key.compare(0, data_prefix.size(), data_prefix) != 0 || is_script_key(key))
    {
        return {};
    }
    auto compiled = protocol_root / ex::get_compiled_directory_no_slash() / key.substr(data_prefix.size());
    compiled += COMPILED_SUFFIX;
    if(!ex::is_format<gfx::shader>(fs::path(key).extension().generic_string()))
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
    fs::error_code ec;
    return fs::exists(library, ec);
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

auto collect_pack_inputs(const fs::path& compiled,
                         const fs::path& cooked,
                         const fs::path& index,
                         const compiled_file_filter& is_shipped) -> std::vector<fs::pack_input>
{
    std::vector<fs::pack_input> inputs;
    fs::error_code ec;
    const auto compiled_folder = ex::get_compiled_directory_no_slash();
    for(fs::recursive_directory_iterator it(compiled, ec), end; !ec && it != end; it.increment(ec))
    {
        const auto& file = it->path();
        const auto relative = file.lexically_relative(compiled);
        if(!it->is_regular_file(ec) || is_loose_only_file(file) || !is_shipped(file, relative))
        {
            continue;
        }
        const auto cooked_file = cooked / relative;
        fs::error_code exists_ec;
        inputs.push_back({compiled_folder + "/" + relative.generic_string(), fs::exists(cooked_file, exists_ec) ? cooked_file : file});
    }
    if(ec)
    {
        throw std::runtime_error(fmt::format("Listing {} failed: {}", compiled.generic_string(), ec.message()));
    }
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

void copy_cooked_or_throw(const fs::path& cooked, const fs::path& destination, const compiled_file_filter& is_shipped)
{
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(cooked, ec), end; !ec && it != end; it.increment(ec))
    {
        const auto relative = it->path().lexically_relative(cooked);
        if(it->is_regular_file(ec) && is_shipped(it->path(), relative))
        {
            copy_file_or_throw(it->path(), destination / relative);
        }
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
        if(!fs::remove(file, ec))
        {
            throw std::runtime_error(fmt::format("Removing {} failed: {}", file.generic_string(), ec.message()));
        }
    }
}

void remove_siblings(const fs::path& kept)
{
    fs::error_code ec;
    std::vector<fs::path> siblings;
    for(const auto& entry : fs::directory_iterator(kept.parent_path(), ec))
    {
        if(entry.path().filename() != kept.filename())
        {
            siblings.push_back(entry.path());
        }
    }
    for(const auto& sibling : siblings)
    {
        fs::remove_all(sibling, ec);
        if(ec)
        {
            throw std::runtime_error(fmt::format("Removing {} failed: {}", sibling.generic_string(), ec.message()));
        }
    }
}

} // namespace unravel::deployment
