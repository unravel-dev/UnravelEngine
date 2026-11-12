#pragma once

#include "filesystem.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace fs
{

/// What a mirror did.
struct mirror_stats
{
    std::size_t copied{};
    std::size_t unchanged{};
    std::size_t removed{};
};

/// Decides, by source path, which files a mirror takes.
using mirror_filter = std::function<bool(const path& source_file)>;

/// Whether the files `lhs` and `rhs` hold the same bytes. False with `ec` set when one of them cannot be read.
auto have_same_content(const path& lhs, const path& rhs, error_code& ec) -> bool;

/**
 * @brief Copies `from` to `to` unless `to` already has its size and write time.
 *
 * The copy gets the source's write time, so the next call finds the two equal and skips it. It is
 * written beside `to` and renamed into place, so a file another process maps is replaced, not
 * rewritten under it (where the platform allows replacing it at all).
 * @return False with `ec` set when the copy fails.
 */
auto copy_file_if_changed(const path& from, const path& to, bool& is_copied, error_code& ec) -> bool;

/**
 * @brief Removes the files under `root` that `earlier` names and `kept` does not, then the folders that leaves
 * empty: what an older list of files left behind once a newer list replaced it.
 *
 * Names are relative to `root` and '/' separated, and compare as make_path_key does. A name that leads out of
 * `root` is skipped. Every file is tried, whatever fails.
 * @return The number of files removed; `ec` holds the first failure.
 */
auto remove_unlisted_files(const path& root,
                           const std::vector<std::string>& earlier,
                           const std::vector<std::string>& kept,
                           error_code& ec) -> std::size_t;

/**
 * @brief Makes the folder `to` hold exactly the files of `from` that `include` accepts (all when empty).
 *
 * New and changed files are copied with copy_file_if_changed, the others left alone. Files under `to`
 * that `from` does not have, or that `include` turns down, are removed, and so are folders left empty.
 * A `to` that does not exist is created with the first file copied; with none to copy it stays missing.
 * Names compare as make_path_key does: case-insensitively where the file system is.
 * @return False with `ec` set when a file cannot be listed, copied or removed; `to` may then be partly
 * updated.
 */
auto mirror_directory(const path& from,
                      const path& to,
                      const mirror_filter& include,
                      mirror_stats& stats,
                      error_code& ec) -> bool;

} // namespace fs
