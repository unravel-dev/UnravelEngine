#pragma once
#include <engine/assets/asset_manager.h>
#include <filesystem/filesystem.h>

#include <string>
#include <vector>

namespace unravel
{
namespace asset_compiler
{

template<typename T>
auto compile(asset_manager& am, const fs::path& key, const fs::path& output_key, uint32_t flags = 0) -> bool;

template<typename T>
auto read_importer(asset_manager& am, const fs::path& key) -> std::shared_ptr<asset_importer_meta>;

/// One compiler diagnostic, as parsed from the C# compiler output.
struct script_compilation_entry
{
    std::string file{}; // Path to the file
    int line{};         // Line number of the error
    std::string msg{};  // Full error line
};

/// The outcome of the most recent build of one script library.
struct script_compilation_result
{
    bool has_run{false};
    bool succeeded{true};
    std::vector<script_compilation_entry> errors;
    std::vector<script_compilation_entry> warnings;
};

/**
 * @brief The diagnostics of the last build of `protocol`'s script library ("app", "engine").
 *        The build already parses them for the log; this keeps the latest set so a caller can
 *        read exactly one build's errors instead of searching the log. Thread-safe.
 */
auto get_last_script_compilation(const std::string& protocol) -> script_compilation_result;

} // namespace asset_compiler
} // namespace unravel
