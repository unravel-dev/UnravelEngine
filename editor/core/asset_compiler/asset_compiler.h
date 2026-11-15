#pragma once
#include <engine/assets/asset_manager.h>
#include <engine/meta/rendering/shader_variants.hpp>
#include <filesystem/filesystem.h>

#include <bgfx/bgfx.h>

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

/**
 * @brief Compiles the shader at `key` (a protocol or absolute path) to `output` for the renderer its extension
 *        names (".wgsl": WebGPU), with a manifest beside it, as the editor compiles its own cache.
 * @return False when it did not compile; the log says why.
 */
auto compile_shader(const fs::path& key, const fs::path& output) -> bool;

/**
 * @brief Whether `output` holds the shader at `key` as compile_shader would write it now: the file and its
 *        manifest exist, the manifest is of the current format, and the source and what it includes have the
 *        fingerprint it records.
 */
auto is_shader_compiled(const fs::path& key, const fs::path& output) -> bool;

/**
 * @brief Compiles the text of a shader that has no file, such as one generated, for `renderer`. Its includes are the
 *        engine's shader files alone: a file of the project of the same name never stands in for one.
 * @param shader_name What the shader is named in messages: its vs_, fs_ or cs_ prefix gives the stage. It need not
 *        exist.
 * @param temp_dir Where shaderc's input and output files are written meanwhile.
 * @param[out] variants Every variant the text declares, compiled; filled only when all of them compile.
 * @param[out] error When given: why it did not compile, the error lines of the first failed variant.
 * @return False when it did not compile; the log says why in full.
 */
auto compile_shader_text(const fs::path& shader_name,
                         const std::string& text,
                         bgfx::RendererType::Enum renderer,
                         const fs::path& temp_dir,
                         std::vector<compiled_shader_variant>& variants,
                         std::string* error = nullptr) -> bool;

/**
 * @brief Compiles the shader the graph material at `key` generates, for the renderer `output`'s extension names, into
 *        `output`: the file beside the compiled material (get_graph_material_shader_file). Leaves an `output` that
 *        holds it already as it is.
 * @return False when it did not compile; the log says why.
 */
auto compile_graph_material_shader(const fs::path& key, const fs::path& output) -> bool;

/// Whether `output` holds the shader the graph material at `key` generates now, compiled for its renderer as
/// compile_graph_material_shader would compile it.
auto is_graph_material_shader_compiled(const fs::path& key, const fs::path& output) -> bool;

/**
 * @brief The import settings a source starts with, by the type its extension names; null for a type without any.
 *        A new source's .meta is written with them, so its first compile already has them.
 */
auto create_default_importer(const fs::path& source_path) -> std::shared_ptr<asset_importer_meta>;

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
    /// The sources it compiled, as absolute paths.
    std::vector<std::string> files;
};

/**
 * @brief The diagnostics of the last build of `protocol`'s script library ("app", "engine").
 *        The build already parses them for the log; this keeps the latest set so a caller can
 *        read exactly one build's errors instead of searching the log. Thread-safe.
 */
auto get_last_script_compilation(const std::string& protocol) -> script_compilation_result;

/**
 * @brief Builds `files` alone against the engine's script library, into a temporary folder that is removed after:
 *        whether a set of scripts builds without the rest of the project (a package's scripts). Slow (it runs the C#
 *        compiler); any thread.
 * @param errors Receives the compiler's errors when the build fails.
 * @return True when the files build.
 */
auto check_script_files(const std::vector<fs::path>& files, std::vector<script_compilation_entry>& errors) -> bool;

} // namespace asset_compiler
} // namespace unravel
