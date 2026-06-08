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

/// Recursively resolve include/dependency files for a given source file.
/// The source file itself is appended first, then dependencies in source parse order (DFS).
/// Default implementation returns no dependencies.
/// Specializations exist for gfx::shader (#include) and ui_tree (<link> tags).
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
