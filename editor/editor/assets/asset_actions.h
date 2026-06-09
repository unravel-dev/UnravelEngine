#pragma once

#include <engine/assets/asset_handle.h>
#include <filesystem/filesystem.h>

#include <string>

namespace unravel::asset_actions
{

auto resolve_asset_source_path(const std::string& asset_key) -> fs::path;
auto resolve_asset_source_path(const fs::path& path) -> fs::path;

auto can_reimport(const fs::path& absolute_path) -> bool;

void reimport_path(const fs::path& absolute_path);
void reimport_key(const std::string& asset_key);

template<typename T>
void reimport(const asset_handle<T>& asset)
{
    reimport_key(asset.id());
}

} // namespace unravel::asset_actions
