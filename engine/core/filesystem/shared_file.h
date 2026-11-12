#pragma once

#include "filesystem.h"

#include <cstddef>
#include <cstdint>

namespace fs
{

/**
 * @brief Gives the file `from` the name `to`, in place of a file already there, as rename does. It also replaces a
 * file that a shared_read_file holds open, which goes on reading the file it opened: Windows refuses a plain rename
 * over any open file. A file held open in any other way is waited for, for a fraction of a second: virus scanners,
 * indexers and file watchers open a file that was just written and let go of it at once. One that stays held is not
 * replaced, and `ec` says so.
 */
void rename_over(const path& from, const path& to, error_code& ec);

/**
 * @brief A file open for reading that other programs may write, replace (rename_over), rename or delete meanwhile:
 * what sends a file to someone while a build updates the folder it lies in.
 *
 * A plain open on Windows keeps a file from being replaced or deleted for as long as it is open, so the update
 * fails; elsewhere every open allows both. The bytes read are those of the file that was opened, also after another
 * file took its name.
 */
class shared_read_file
{
public:
    shared_read_file() = default;
    shared_read_file(const shared_read_file&) = delete;
    auto operator=(const shared_read_file&) -> shared_read_file& = delete;
    ~shared_read_file();

    /// Opens `file`, closing the one open before; false when it cannot be opened.
    auto open(const path& file) -> bool;
    auto is_open() const -> bool;
    void close();

    /// The file's size when it was opened.
    auto size() const -> std::uint64_t
    {
        return size_;
    }

    /// Reads up to `count` bytes from `offset` on into `buffer`, from any thread. Returns how many it read: fewer
    /// than `count` at the file's end, and 0 past it or when the read fails.
    auto read(std::uint64_t offset, char* buffer, std::size_t count) const -> std::size_t;

private:
    /// The platform's file handle (a HANDLE on Windows, a descriptor elsewhere); -1 while closed.
    std::intptr_t file_{-1};
    std::uint64_t size_{};
};

} // namespace fs
