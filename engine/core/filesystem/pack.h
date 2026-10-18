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
    /// Compress entries in independent 64 KB blocks; a block that does not shrink by 5% is kept raw,
    /// and an entry where none does is stored whole. An open decodes the whole entry.
    bool compress{true};
    compression::level compression_level{compression::level::balanced};
    /// An earlier pack to take entries from, which may be `pack_file` itself: an input with the same
    /// bytes (content hash and size) as one of its intact entries gets that entry's stored bytes instead
    /// of being compressed again, whatever its name. Used only when it was written with the same
    /// settings (compression level, codec and its version, block size), so the output is what a fresh
    /// write would give. Empty, missing or unreadable: everything is compressed.
    path reuse_from;
    /// Makes the pack a patch over this full pack (its base): an input the base holds under the same name
    /// with the same bytes is left out, a name the base holds that no input has becomes a deleted entry,
    /// and the patch records the base so it mounts only right over that exact pack. Empty: a full pack.
    path patch_base;
};

/// What a written pack holds.
struct pack_write_stats
{
    /// Every entry, deleted ones included.
    std::size_t entry_count{};
    /// Entries taken from the pack in pack_write_options::reuse_from.
    std::size_t reused_count{};
    /// Patch only: inputs left out because the base holds them unchanged.
    std::size_t unchanged_count{};
    /// Patch only: names the base holds and the inputs do not, written as deleted entries.
    std::size_t deleted_count{};
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
 * protocol wins for the entries both hold, and its deleted entries hide the files of earlier packs
 * and of the disk. A patch mounts only right over the pack it was made against.
 * @return False with `err` set when the file is missing, is not a pack, is damaged, was
 * compressed with a codec this build does not decode, or is a patch whose base is not the
 * protocol's topmost pack.
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

/// What the mounted packs say about a file.
enum class packed_status
{
    /// No mounted pack holds it: the disk decides.
    absent,
    found,
    /// The topmost pack holding it deletes it: it hides the packs under it and the disk.
    deleted,
    /// A pack holds it but its bytes do not decode (logged).
    damaged,
};

struct packed_lookup
{
    packed_status status{packed_status::absent};
    /// The bytes when found.
    packed_file file;
};

/**
 * @brief What the mounted packs hold for `file`.
 *
 * `file` is absolute or a protocol path ("app:/compiled/a.png.asset"). A stored entry is a view of
 * the mapping; a compressed one is decoded into an owned buffer.
 */
auto lookup_packed_file(const path& file) -> packed_lookup;

/// The bytes of `file` from the mounted packs; empty (is_found false) unless it is found there.
auto find_packed_file(const path& file) -> packed_file;

/// Whether `file` is in a mounted pack or on disk (and not deleted by a mounted pack).
auto is_readable_file(const path& file) -> bool;

/// A stream over `file` from a mounted pack, else from disk (binary); null when neither has it or a
/// mounted pack deletes it.
auto open_readable_stream(const path& file) -> std::unique_ptr<std::istream>;

} // namespace fs
