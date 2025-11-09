#pragma once
#include <filesystem/filesystem.h>
#include <hpp/sha1.hpp>
#include <chrono>
#include <string>

namespace unravel
{
namespace asset_compiler
{

/// Manifest data for compiled assets
struct asset_manifest
{
    /// Path to the source file
    fs::path source_key;
    /// Timestamp when the asset was compiled
    std::chrono::nanoseconds source_timestamp;
    
    /// SHA1 hash of the source file content
    std::string source_sha;
    
    
    asset_manifest() = default;
    
    asset_manifest(const fs::path& key)
        : source_key(key)
    {
        fs::error_code ec;
        source_timestamp = fs::last_write_time(fs::resolve_protocol(key), ec).time_since_epoch();
    }
    
    void compute_source_sha();
};

/// Generate manifest file path from compiled asset path
auto get_manifest_path(const fs::path& compiled_asset_path) -> fs::path;

/// Save manifest to file
auto save_manifest(const fs::path& manifest_path, const asset_manifest& manifest) -> bool;

/// Load manifest from file
auto load_manifest(const fs::path& manifest_path, asset_manifest& manifest) -> bool;

/// Check if source file has changed compared to manifest
auto is_source_file_changed(const fs::path& source_path, const asset_manifest& manifest) -> bool;

} // namespace asset_compiler
} // namespace unravel
