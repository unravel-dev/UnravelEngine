#pragma once

#include <hpp/string_view.hpp>
#include <hpp/uuid.hpp>

#include <string>
#include <vector>

namespace unravel
{
namespace asset_references
{

/**
 * @brief Every quoted uuid in a text asset file - a scene, a prefab, a material, an import setting.
 * Asset handles and other asset fields store their asset's uid that way ("uid", "prefab_uid",
 * "document"); the caller keeps the ones the asset database knows. Nil uids are skipped.
 * @return The uids in order of first appearance, without repeats.
 */
auto find_quoted_uids(hpp::string_view text) -> std::vector<hpp::uuid>;

/**
 * @brief Every quoted string in `text` that starts with `protocol` followed by ":/", such as
 * "app:/data/ui/click.wav" - asset paths written into scene fields or script literals.
 * Single and double quotes both count.
 * @return The paths in order of first appearance, without repeats.
 */
auto find_quoted_protocol_paths(hpp::string_view text, hpp::string_view protocol) -> std::vector<std::string>;

/**
 * @brief The files a UI document or style sheet points at: href and src attribute values, url(...)
 * values and @import targets, as written (relative to the file or a protocol path). Fragments,
 * data: URIs and web addresses are skipped.
 * @return The paths in order of first appearance, without repeats.
 */
auto find_ui_linked_paths(hpp::string_view text) -> std::vector<std::string>;

} // namespace asset_references
} // namespace unravel
