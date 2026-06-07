#include "asset_manifest.h"
#include "asset_extensions.h"
#include <logging/logging.h>
#include <fstream>
#include <vector>
#include <engine/assets/impl/asset_extensions.h>
#include <serialization/serialization.h>
#include <serialization/associative_archive.h>
#include <serialization/types/chrono.hpp>

namespace unravel
{
namespace asset_compiler
{

namespace
{
auto hash_file(const fs::path& file_path) -> std::string
{
    const bool is_text = !ex::is_binary(file_path.extension().string());
    return fs::hash_file_fingerprint(file_path, is_text);
}
} // namespace

void asset_manifest::compute_source_fingerprint(const fs::path& source_key)
{
    std::set<fs::path> empty;
    compute_source_fingerprint(source_key, empty);
}

void asset_manifest::compute_source_fingerprint(const fs::path& source_key,
                                                const std::set<fs::path>& dependency_paths)
{
    auto source_file_path = fs::resolve_protocol(source_key);
    try
    {
        fingerprint_version = fs::current_source_fingerprint_version;
        if(dependency_paths.empty())
        {
            source_fingerprint = hash_file(source_file_path);
            if(source_fingerprint.empty())
            {
                APPLOG_ERROR("Failed to fingerprint source file: {}", source_file_path.string());
            }
            return;
        }
        std::vector<std::string> fingerprints;
        fingerprints.reserve(dependency_paths.size());
        for(const auto& dep_path : dependency_paths)
        {
            fs::error_code dep_ec;
            if(!fs::exists(dep_path, dep_ec) || dep_ec)
            {
                continue;
            }
            const auto fingerprint = hash_file(dep_path);
            if(fingerprint.empty())
            {
                continue;
            }
            fingerprints.push_back(fingerprint);
        }
        if(fingerprints.empty())
        {
            source_fingerprint = "";
            return;
        }
        if(fingerprints.size() == 1)
        {
            source_fingerprint = fingerprints.front();
            return;
        }
        source_fingerprint = fs::combine_file_fingerprints(fingerprints.front(),
                                                           fingerprints.data() + 1,
                                                           fingerprints.size() - 1);
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Exception while fingerprinting {}: {}", source_file_path.string(), e.what());
        source_fingerprint = "";
    }
}

auto get_manifest_path(const fs::path& compiled_asset_path) -> fs::path
{
    return compiled_asset_path.string() + ".manifest";
}

auto save_manifest(const fs::path& manifest_path, const asset_manifest& manifest) -> bool
{
    try
    {
        std::ofstream file(manifest_path);
        if(!file.is_open())
        {
            APPLOG_ERROR("Failed to open manifest file for writing: {}", manifest_path.string());
            return false;
        }
        auto archive = ser20::create_oarchive_associative(file);
        bool success = true;
        success &= try_save(archive, ser20::make_nvp("source_fingerprint", manifest.source_fingerprint));
        success &= try_save(archive, ser20::make_nvp("format_version", manifest.format_version));
        success &= try_save(archive, ser20::make_nvp("fingerprint_version", manifest.fingerprint_version));
        return success;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Exception while saving manifest {}: {}", manifest_path.string(), e.what());
        return false;
    }
}

auto load_manifest(const fs::path& manifest_path, asset_manifest& manifest) -> bool
{
    try
    {
        std::ifstream file(manifest_path);
        if(!file.is_open())
        {
            APPLOG_ERROR("Failed to open manifest file for reading: {}", manifest_path.string());
            return false;
        }
        auto archive = ser20::create_iarchive_associative(file);
        try_load(archive, ser20::make_nvp("source_fingerprint", manifest.source_fingerprint));
        try_load(archive, ser20::make_nvp("format_version", manifest.format_version));
        try_load(archive, ser20::make_nvp("fingerprint_version", manifest.fingerprint_version));
        return true;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Exception while loading manifest {}: {}", manifest_path.string(), e.what());
        return false;
    }
}

namespace
{
auto resolve_manifest_input_file(const fs::path& key) -> fs::path
{
    fs::path source_key = fs::convert_to_protocol(key);
    source_key = fs::replace(source_key, ex::get_meta_directory(), ex::get_data_directory());
    if(source_key.extension() == ".meta")
    {
        source_key.replace_extension();
    }
    return source_key;
}
} // namespace

auto is_fingerprint_algorithm_current(const asset_manifest& manifest) -> bool
{
    return manifest.fingerprint_version == fs::current_source_fingerprint_version;
}

auto is_source_file_changed(const fs::path& source_path, const asset_manifest& manifest) -> bool
{
    if(!is_fingerprint_algorithm_current(manifest))
    {
        APPLOG_WARNING("Fingerprint algorithm changed for {}, recompilation needed", source_path.string());
        return true;
    }
    auto source_key = resolve_manifest_input_file(source_path);
    asset_manifest current_manifest(source_key);
    current_manifest.compute_source_fingerprint(source_key);
    return current_manifest.source_fingerprint != manifest.source_fingerprint;
}

auto is_source_file_changed(const fs::path& source_path,
                            const asset_manifest& manifest,
                            const std::set<fs::path>& dependency_paths) -> bool
{
    if(!is_fingerprint_algorithm_current(manifest))
    {
        APPLOG_WARNING("Fingerprint algorithm changed for {}, recompilation needed", source_path.string());
        return true;
    }
    auto source_key = resolve_manifest_input_file(source_path);
    asset_manifest current_manifest(source_key);
    current_manifest.compute_source_fingerprint(source_key, dependency_paths);
    return current_manifest.source_fingerprint != manifest.source_fingerprint;
}

auto is_compiled_format_changed(const fs::path& source_path, const asset_manifest& manifest) -> bool
{
    auto source_key = resolve_manifest_input_file(source_path);
    auto extension = source_key.extension().string();
    auto current_version = ex::get_format_version(extension);
    return manifest.format_version != current_version;
}
} // namespace asset_compiler
} // namespace unravel
