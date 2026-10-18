#pragma once

#include <engine/assets/asset_manager.h>
#include <filesystem/filesystem.h>

#include <functional>

namespace unravel
{
namespace generated_asset
{

/// Writes generated content to the path it is given.
using content_writer_t = std::function<void(const fs::path&)>;

/// What a generated write did with the file.
enum class write_result
{
    /// The file did not exist, or still held what the import last wrote, and now holds this import.
    written,
    /// The file already held exactly this import and was not touched.
    unchanged,
    /// The file was edited since the import wrote it and was kept as it is.
    kept_edited,
    /// The content could not be produced or written.
    failed
};

/**
 * @brief Writes @p path as a by-product of the asset @p source_uid, unless it was edited.
 *
 * The .meta of @p path records the fingerprint of every write. The file is rewritten while it still
 * matches that fingerprint and kept once it does not, so edits survive every later import. A file
 * without a record is adopted when it already holds this import and kept otherwise. Line endings do
 * not count, so a checkout that converts them is not an edit.
 *
 * The .meta is written before the content: the editor rewrites an asset's .meta from disk when the
 * asset changes, so the record has to be there by then.
 *
 * @param uid The id @p path is registered under.
 * @param source_uid The id of the asset whose import writes @p path.
 * @param write_content Writes the generated content.
 */
auto write_file(asset_manager& am,
                const fs::path& path,
                const hpp::uuid& uid,
                const hpp::uuid& source_uid,
                const content_writer_t& write_content) -> write_result;

/**
 * @brief Moves a generated file from @p former_path, the name an earlier import gave it, to @p path
 * together with its .meta, so the asset keeps its id and every reference to it keeps resolving.
 *
 * Does nothing unless the former file exists and @p path does not. When several imports claim the same
 * former file, the first one takes it and the others write their own.
 *
 * @return True when the file was moved.
 */
auto adopt_former_file(asset_manager& am, const fs::path& former_path, const fs::path& path) -> bool;

/**
 * @brief Whether the generated file at @p path no longer holds what its import last wrote.
 */
auto is_edited(const fs::path& path, const asset_generated_info& generated) -> bool;

/**
 * @brief Lets the next import of its source overwrite the generated file at @p path, edits included.
 * @return False when @p path is not a generated asset or its .meta could not be written.
 */
auto release_to_import(asset_manager& am, const fs::path& path) -> bool;

} // namespace generated_asset
} // namespace unravel
