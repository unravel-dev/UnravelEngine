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

namespace asset_compiler
{

/// Recursively resolve external dependency files for a given source file.
/// The root source itself is omitted (it is watched separately); only includes /
/// linked files (and the root shader varying def) are appended in DFS order.
/// Default implementation returns no dependencies.
/// Specializations exist for gfx::shader (#include + varying) and ui_tree (<link>).
template<typename T>
void resolve_dependencies(const fs::path& /*file_path*/, std::vector<fs::path>& /*processed_files*/)
{
}

template<>
void resolve_dependencies<gfx::shader>(const fs::path& file_path, std::vector<fs::path>& processed_files);

template<>
void resolve_dependencies<ui_tree>(const fs::path& file_path, std::vector<fs::path>& processed_files);

} // namespace asset_compiler
} // namespace unravel
