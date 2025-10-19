#include "asset_manifest.h"
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


void asset_manifest::compute_source_sha()
{
    fs::error_code ec;
    if(!fs::exists(source_file_path, ec) || ec)
    {
        APPLOG_WARNING("Source file does not exist for SHA computation: {}", source_file_path.string());
        source_sha = "";
        return;
    }
    
    try
    {
        std::ifstream file(source_file_path, std::ios::binary);
        if(!file.is_open())
        {
            APPLOG_ERROR("Failed to open source file for SHA computation: {}", source_file_path.string());
            source_sha = "";
            return;
        }
        
        auto hasher = hpp::sha1::compute_file_sha1(file);
        
        std::array<char, SHA1_HEX_SIZE> hex_buffer{};
        hasher.print_hex(hex_buffer.data(), true, false);
        source_sha = std::string(hex_buffer.data());
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
    
        archive(ser20::make_nvp("source_path", manifest.source_file_path.generic_string()));
        archive(ser20::make_nvp("source_sha", manifest.source_sha));
        archive(ser20::make_nvp("source_timestamp", manifest.source_timestamp));
        
        return true;
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
        
        std::string source_path;
        archive(ser20::make_nvp("source_path", source_path));
        manifest.source_file_path = fs::path(source_path);
        archive(ser20::make_nvp("source_sha", manifest.source_sha));
        archive(ser20::make_nvp("source_timestamp", manifest.source_timestamp));
        return true;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Exception while loading manifest {}: {}", manifest_path.string(), e.what());
        return false;
    }
}

auto is_source_file_changed(const fs::path& source_path, const asset_manifest& manifest) -> bool
{
    auto resolve_input_file = [](const fs::path& key) -> fs::path
    {
        fs::path absolute_path = fs::convert_to_protocol(key);
        absolute_path = fs::resolve_protocol(fs::replace(absolute_path, ex::get_meta_directory(), ex::get_data_directory()));
        if(absolute_path.extension() == ".meta")
        {
            absolute_path.replace_extension();
        }
        return absolute_path;
    };

    auto source_file_path = resolve_input_file(source_path);
    // Create a temporary manifest to compute current SHA
    asset_manifest current_manifest(source_file_path);

    if(current_manifest.source_timestamp == manifest.source_timestamp)
    {
        return false;
    }
    
    current_manifest.compute_source_sha();
    // Compare SHA values
    return current_manifest.source_sha != manifest.source_sha;
}

} // namespace asset_compiler
} // namespace unravel
