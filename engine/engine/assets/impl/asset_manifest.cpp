#include "asset_manifest.h"
#include "asset_extensions.h"
#include <logging/logging.h>
#include <fstream>
#include <array>
#include <engine/assets/impl/asset_extensions.h>
#include <serialization/serialization.h>
#include <serialization/associative_archive.h>
#include <serialization/types/chrono.hpp>

namespace unravel
{
namespace asset_compiler
{


void asset_manifest::compute_source_sha(const fs::path& source_key)
{
    std::set<fs::path> empty;
    compute_source_sha(source_key, empty);
}

void asset_manifest::compute_source_sha(const fs::path& source_key, const std::set<fs::path>& dependency_paths)
{
    auto source_file_path = fs::resolve_protocol(source_key);
    fs::error_code ec;
    if(!fs::exists(source_file_path, ec) || ec)
    {
        APPLOG_WARNING("Source file does not exist for SHA computation: {}", source_file_path.string());
        source_sha = "";
        return;
    }
    try
    {
        if(dependency_paths.empty())
        {
            std::ifstream file(source_file_path, std::ios::binary);
            if(!file.is_open())
            {
                APPLOG_ERROR("Failed to open source file for SHA computation: {}", source_file_path.string());
                source_sha = "";
                return;
            }
            bool is_text = !ex::is_binary(source_file_path.extension().string());
            auto hasher = hpp::sha1::compute_file_sha1_stable(file, is_text);
            std::array<char, SHA1_HEX_SIZE> hex_buffer{};
            hasher.print_hex(hex_buffer.data(), true, false);
            source_sha = std::string(hex_buffer.data());
        }
        else
        {
            hpp::sha1 combined_hasher;
            for(const auto& dep_path : dependency_paths)
            {
                fs::error_code dep_ec;
                if(!fs::exists(dep_path, dep_ec) || dep_ec)
                {
                    continue;
                }
                std::ifstream file(dep_path, std::ios::binary);
                if(!file.is_open())
                {
                    continue;
                }
                bool is_text = !ex::is_binary(dep_path.extension().string());
                auto file_hasher = hpp::sha1::compute_file_sha1_stable(file, is_text);
                std::array<char, SHA1_HEX_SIZE> hex_buffer{};
                file_hasher.print_hex(hex_buffer.data(), true, false);
                combined_hasher.add(hex_buffer.data(), SHA1_HEX_SIZE - 1);
            }
            combined_hasher.finalize();
            std::array<char, SHA1_HEX_SIZE> hex_buffer{};
            combined_hasher.print_hex(hex_buffer.data(), true, false);
            source_sha = std::string(hex_buffer.data());
        }
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Exception while computing SHA for {}: {}", source_file_path.string(), e.what());
        source_sha = "";
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
        success &= try_save(archive, ser20::make_nvp("source_sha", manifest.source_sha));
        success &= try_save(archive, ser20::make_nvp("format_version", manifest.format_version));
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
        fs::error_code ec;
        if(!fs::exists(manifest_path, ec) || ec)
        {
            return false;
        }
        
        std::ifstream file(manifest_path);
        if(!file.is_open())
        {
            APPLOG_ERROR("Failed to open manifest file for reading: {}", manifest_path.string());
            return false;
        }
        
        auto archive = ser20::create_iarchive_associative(file);
        
        
        bool success = true;
        success &= try_load(archive, ser20::make_nvp("source_sha", manifest.source_sha));
        success &= try_load(archive, ser20::make_nvp("format_version", manifest.format_version));
        return success;
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

auto is_source_file_changed(const fs::path& source_path, const asset_manifest& manifest) -> bool
{
    auto source_key = resolve_manifest_input_file(source_path);
    asset_manifest current_manifest(source_key);
    current_manifest.compute_source_sha(source_key);
    return current_manifest.source_sha != manifest.source_sha;
}

auto is_source_file_changed(const fs::path& source_path,
                            const asset_manifest& manifest,
                            const std::set<fs::path>& dependency_paths) -> bool
{
    auto source_key = resolve_manifest_input_file(source_path);
    asset_manifest current_manifest(source_key);
    current_manifest.compute_source_sha(source_key, dependency_paths);
    return current_manifest.source_sha != manifest.source_sha;
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
