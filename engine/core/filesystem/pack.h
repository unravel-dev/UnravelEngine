#pragma once

#include "filesystem.h"
#include "mapped_file.h"

#include <compression/compression.h>

#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace fs
{
namespace pack_detail
{
struct pack_image;
} // namespace pack_detail

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

/// Where a pack file holds one entry's stored bytes.
struct pack_entry_span
{
    /// The entry's name, as written.
    std::string name;
    std::uint64_t offset{};
    std::uint64_t size{};
};

/// How a pack file is laid out: its entries' stored bytes, then the table of contents to the end of the file.
struct pack_layout
{
    /// The entries that hold bytes, in file order; deleted entries hold none.
    std::vector<pack_entry_span> entries;
    /// Where the table of contents starts.
    std::uint64_t contents_offset{};
    std::uint64_t file_size{};
};

/**
 * @brief Reads the layout of the pack `pack_file`: what cuts it into pieces that hold whole entries, so a piece
 * changes only when one of its entries does.
 * @return False with `err` set when the file is missing, is not a pack or is damaged.
 */
auto read_pack_layout(const path& pack_file, pack_layout& layout, std::string& err) -> bool;

/**
 * @brief A pack file opened to read its entries by name, without mounting it: what a tool that takes a pack
 * apart (an editor package) reads it with. Every entry is checked against its content hash as it is read.
 */
class pack_reader
{
public:
    pack_reader();
    ~pack_reader();
    pack_reader(pack_reader&&) noexcept;
    auto operator=(pack_reader&&) noexcept -> pack_reader&;
    pack_reader(const pack_reader&) = delete;
    auto operator=(const pack_reader&) -> pack_reader& = delete;

    /// Maps the pack. @return False with `err` set when it is missing, is not a pack, is damaged or is a patch.
    auto open(const path& pack_file, std::string& err) -> bool;
    auto is_open() const -> bool;
    /// The names of the entries that hold bytes, sorted.
    auto get_names() const -> std::vector<std::string>;
    /// Whether an entry of that name holds bytes.
    auto has_entry(const std::string& name) const -> bool;
    /// The raw size of an entry; 0 when there is none.
    auto get_size(const std::string& name) const -> std::uint64_t;
    /**
     * @brief The bytes of an entry, decoded.
     * @return False with `err` set when there is no such entry or its bytes do not give its content hash.
     */
    auto read(const std::string& name, byte_array_t& bytes, std::string& err) const -> bool;
    /// Identifies the pack's content: the hash of its table of contents.
    auto get_contents_hash() const -> std::uint64_t;

private:
    std::shared_ptr<const pack_detail::pack_image> image_;
};

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

/// Mounts a pack that is a region of an open file, such as a pack stored uncompressed in an APK, as
/// mount_pack above does a pack file. Not supported on Windows.
auto mount_pack(const std::string& protocol, file_region pack_region, std::string& err) -> bool;

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
