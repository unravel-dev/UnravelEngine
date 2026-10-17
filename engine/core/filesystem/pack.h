#pragma once

#include "filesystem.h"

#include <compression/compression.h>

#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace fs
{

/// One file to put into a pack.
struct pack_input
{
    /// The entry's name: its path below the protocol root the pack is mounted at, '/' separated
    /// ("compiled/textures/a.png.asset").
    std::string name;
    /// The file the bytes are read from.
    path source;
};

struct pack_write_options
{
    /// Compress entries in independent 64 KB blocks, so a part of an entry decodes without the rest;
    /// a block that does not shrink by 5% is kept raw, and an entry where none does is stored whole.
    bool compress{true};
    compression::level compression_level{compression::level::balanced};
};

/// What a written pack holds.
struct pack_write_stats
{
    std::size_t entry_count{};
    std::uint64_t raw_bytes{};
    std::uint64_t stored_bytes{};
};

/**
 * @brief Writes the inputs into one pack file, in the order given.
 *
 * The output depends only on the inputs and the options (no timestamps), so the same build packs to
 * the same bytes. Written to a temporary file next to `pack_file` and moved into place.
 * @return False with `err` set when an input cannot be read or the pack cannot be written.
 */
auto write_pack(const path& pack_file,
                const std::vector<pack_input>& inputs,
                const pack_write_options& options,
                std::string& err,
                pack_write_stats* stats = nullptr) -> bool;

/**
 * @brief Mounts a pack: its entries answer for the files under the root of `protocol`.
 *
 * The pack is memory-mapped and stays mapped until unmount_packs. A later mount of the same
 * protocol wins for the entries both hold.
 * @return False with `err` set when the file is missing, is not a pack, is damaged or was
 * compressed with a codec this build does not decode.
 */
auto mount_pack(const std::string& protocol, const path& pack_file, std::string& err) -> bool;

/// Unmounts every pack. Files found in them before stay readable.
void unmount_packs();

/// Whether any pack is mounted.
auto has_mounted_packs() -> bool;

/// A file's bytes as a mounted pack holds them.
struct packed_file
{
    const std::uint8_t* data{};
    std::size_t size{};
    /// Keeps the bytes alive: the pack's mapping, or the buffer a compressed entry decoded into.
    std::shared_ptr<const void> storage;

    auto is_found() const -> bool
    {
        return storage != nullptr;
    }
};

/**
 * @brief The bytes of `file` from the mounted packs; empty (is_found false) when none holds it.
 *
 * `file` is absolute or a protocol path ("app:/compiled/a.png.asset"). A stored entry is a view of
 * the mapping; a compressed one is decoded into an owned buffer.
 */
auto find_packed_file(const path& file) -> packed_file;

/// Whether `file` is in a mounted pack or on disk.
auto is_readable_file(const path& file) -> bool;

/// A stream over `file` from a mounted pack, else from disk (binary); null when neither has it.
auto open_readable_stream(const path& file) -> std::unique_ptr<std::istream>;

} // namespace fs
