#pragma once
#include <filesystem/filesystem.h>
#include <vector>

namespace gfx
{
struct shader;
}

namespace unravel
{

struct ui_tree;
class mesh;
class material;

namespace asset_compiler
{

/// Recursively resolve external dependency files for a given source file.
/// The root source itself is omitted (it is watched separately); only includes /
/// linked files (and the root shader varying def) are appended in DFS order.
/// Default implementation returns no dependencies.
/// Specializations exist for gfx::shader (#include + varying), ui_tree (<link>),
/// and mesh (.gltf buffers/images, .obj mtllib).
template<typename T>
void resolve_dependencies(const fs::path& /*file_path*/, std::vector<fs::path>& /*processed_files*/)
{
}

template<>
void resolve_dependencies<gfx::shader>(const fs::path& file_path, std::vector<fs::path>& processed_files);

template<>
void resolve_dependencies<ui_tree>(const fs::path& file_path, std::vector<fs::path>& processed_files);

template<>
void resolve_dependencies<mesh>(const fs::path& file_path, std::vector<fs::path>& processed_files);

/// A material (a graph) depends on the engine shader files its generated shader includes and the material functions
/// its graph calls.
template<>
void resolve_dependencies<material>(const fs::path& file_path, std::vector<fs::path>& processed_files);

/// The engine shader files every graph material's generated shader includes, and what they include, in order. Read
/// from the files again only when one of them changed on disk (file_stamp): every material's check asks.
void resolve_graph_material_shader_dependencies(std::vector<fs::path>& processed_files);

/// When a file was last written and how big it is: what tells something read from it is still valid.
struct file_stamp
{
    fs::path file;
    fs::file_time_type write_time;
    std::uintmax_t size{};

    auto operator==(const file_stamp& other) const -> bool = default;
};

/// @p file's stamp now.
auto make_file_stamp(const fs::path& file) -> file_stamp;

/// Whether @p stamps is not empty and every file of it still has its stamp.
auto are_file_stamps_current(const std::vector<file_stamp>& stamps) -> bool;

/// The files of the material functions the graph material at @p file_path calls, and those they call.
void resolve_graph_material_function_dependencies(const fs::path& file_path, std::vector<fs::path>& processed_files);

} // namespace asset_compiler
} // namespace unravel
