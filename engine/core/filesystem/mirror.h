#pragma once

#include "filesystem.h"

#include <cstddef>
#include <functional>

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
 * @brief Makes the folder `to` hold exactly the files of `from` that `include` accepts (all when empty).
 *
 * New and changed files are copied with copy_file_if_changed, the others left alone. Files under `to`
 * that `from` does not have, or that `include` turns down, are removed, and so are folders left empty.
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
