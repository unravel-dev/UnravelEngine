#pragma once
#include <engine/assets/asset_manager.h>
#include <filesystem/filesystem.h>

namespace unravel
{
namespace asset_compiler
{

template<typename T>
auto compile(asset_manager& am, const fs::path& key, const fs::path& output_key, uint32_t flags = 0) -> bool;

template<typename T>
auto read_importer(asset_manager& am, const fs::path& key) -> std::shared_ptr<asset_importer_meta>;


} // namespace asset_compiler




} // namespace unravel
