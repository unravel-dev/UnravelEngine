#pragma once
#include <filesystem/filesystem.h>
#include <hpp/sha1.hpp>
#include <chrono>
#include <string>
#include <set>
#include "asset_extensions.h"

namespace unravel
{
namespace asset_compiler
{

/// Manifest data for compiled assets
struct asset_manifest
{    
    /// SHA1 hash of the source file content (includes dependency files when applicable)
    std::string source_sha;

    /// Version of the compiled format
    uint64_t format_version = 0;

    asset_manifest() = default;

    asset_manifest(const fs::path& key)
    {
        format_version = ex::get_format_version(key.extension().string());
    }

    /// Compute SHA from source file only
    void compute_source_sha(const fs::path& source_key);

    /// Compute SHA from source file + all resolved dependency file paths.
    /// If dependency_paths is empty, falls back to source-only SHA.
    void compute_source_sha(const fs::path& source_key, const std::set<fs::path>& dependency_paths);
};

/// Generate manifest file path from compiled asset path
auto get_manifest_path(const fs::path& compiled_asset_path) -> fs::path;

/// Save manifest to file
auto save_manifest(const fs::path& manifest_path, const asset_manifest& manifest) -> bool;

/// Load manifest from file
auto load_manifest(const fs::path& manifest_path, asset_manifest& manifest) -> bool;

/// Check if source file has changed compared to manifest (source-only)
auto is_source_file_changed(const fs::path& source_path, const asset_manifest& manifest) -> bool;

/// Check if source file or any of its dependencies have changed compared to manifest
auto is_source_file_changed(const fs::path& source_path,
                            const asset_manifest& manifest,
                            const std::set<fs::path>& dependency_paths) -> bool;

auto is_compiled_format_changed(const fs::path& source_path, const asset_manifest& manifest) -> bool;

} // namespace asset_compiler
} // namespace unravel
