#pragma once

#include <engine/settings/settings.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(settings);
LOAD_EXTERN(settings);
REFLECT_EXTERN(settings);

void save_to_file(const std::string& absolute_path, const settings& obj);
void save_to_file_bin(const std::string& absolute_path, const settings& obj);
auto load_from_file(const std::string& absolute_path, settings& obj) -> bool;
auto load_from_file_bin(const std::string& absolute_path, settings& obj) -> bool;

/**
 * @brief Loads only the cold-boot sections (graphics, physics) of a settings document.
 *
 * @ref load_from_file walks asset handles, and an asset handle resolves through the
 * asset manager's per-type storages - which exist only after asset_manager::init. Boot
 * configuration is peeked before any system is initialized, so it must read this subset.
 * Sections of @p obj other than graphics and physics are left untouched.
 */
auto load_boot_sections_from_file(const std::string& absolute_path, settings& obj) -> bool;

/**
 * @brief Loads only the asset import defaults of a settings document over `obj`.
 *
 * Like @ref load_boot_sections_from_file it never reaches an asset handle, so it can run before the
 * project's asset database is loaded: the asset compiler needs the defaults for the first compiles.
 * A document without the section leaves `obj` as it is.
 */
auto load_asset_sections_from_file(const std::string& absolute_path, settings::asset_settings& obj) -> bool;

} // namespace unravel
