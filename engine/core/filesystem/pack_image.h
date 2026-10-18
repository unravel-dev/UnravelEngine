#pragma once

#include "mapped_file.h"
#include "pack_format.h"

#include <compression/compression.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace fs::pack_detail
{

/// A pack file mapped and its table of contents parsed: what a mount, a patch base and a reuse source
/// read from.
struct pack_image
{
    mapped_file file;
    /// Keyed by make_path_key of the record's name.
    std::unordered_map<std::string, pack_record> entries;
    std::uint32_t write_settings{PACK_UNCOMPRESSED_SETTINGS};
    /// Identifies the pack: the hash of its table of contents.
    std::uint64_t toc_hash{};
    /// For a patch, the toc_hash of the pack it was made against.
    std::uint64_t base_hash{PACK_NO_BASE};

    /// The record of an entry name (any separators or case the file system accepts); null when absent.
    auto find(const std::string& name) const -> const pack_record*;

    auto is_patch() const -> bool
    {
        return base_hash != PACK_NO_BASE;
    }
};

/// Maps and parses a pack; false with `err` set when it is missing, damaged, of another version, or
/// compressed with a codec this build does not decode.
auto open_pack_image(const path& file, pack_image& image, std::string& err) -> bool;

/// Maps and parses a pack that is a region of an open file (see mapped_file::open).
auto open_pack_image(file_region region, pack_image& image, std::string& err) -> bool;

/**
 * @brief The raw bytes of an entry stored in compressed blocks.
 * @return Null when a block does not decode. Throws std::bad_alloc when the size cannot be allocated.
 */
auto decode_entry(const pack_image& image, const pack_record& entry) -> std::shared_ptr<const byte_array_t>;

/// Whether an entry's stored bytes still give its content hash: a damaged earlier pack must not pass
/// its damage on to the packs that reuse its entries.
auto is_entry_intact(const pack_image& image, const pack_record& entry) -> bool;

/// The fingerprint of everything that decides how an input encodes: entries are reused only from a
/// pack written with the same fingerprint, so a reuse gives exactly what a fresh write would.
auto get_write_settings(bool compress, compression::level compression_level) -> std::uint32_t;

} // namespace fs::pack_detail
