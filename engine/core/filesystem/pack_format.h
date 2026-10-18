#pragma once

#include "filesystem.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/// The on-disk layout of a pack (tasks/pack_v1_plan.md): entry data, then the table of contents (one
/// record per entry), then a fixed-size footer. Internal to the pack implementation.
namespace fs::pack_detail
{

static_assert(std::endian::native == std::endian::little,
              "packs store values as they are in memory, which the format defines as little-endian");

constexpr std::array<char, 8> PACK_MAGIC = {'U', 'N', 'R', 'V', 'P', 'A', 'K', '1'};
/// 2: entries carry flags (deleted), the footer the hash of the base a patch was made against.
constexpr std::uint32_t PACK_VERSION = 2;
/// Record flag: the name is deleted - it hides the file of an earlier pack and of the disk.
constexpr std::uint8_t PACK_ENTRY_DELETED = 1;
/// The base hash of a pack that is not a patch.
constexpr std::uint64_t PACK_NO_BASE = 0;
/// The write settings fingerprint of an uncompressed pack.
constexpr std::uint32_t PACK_UNCOMPRESSED_SETTINGS = 0;
constexpr std::uint64_t PACK_ENTRY_ALIGNMENT = 16;
constexpr std::size_t PACK_BLOCK_SIZE = 64 * 1024;
/// A block is kept compressed only when it saves at least this share of its size (1/20 = 5%).
constexpr std::size_t PACK_MIN_SAVED_DIVISOR = 20;
/// The codec byte of an entry stored as it is; any other value is a compression codec id.
constexpr std::uint8_t PACK_STORED_CODEC = 0;
/// Larger entries are refused: no asset comes near it, and a damaged size must not turn into a huge
/// allocation.
constexpr std::uint64_t PACK_MAX_ENTRY_SIZE = std::uint64_t(1) << 34;

/// One entry of the table of contents.
struct pack_record
{
    /// The name as written: a path below the protocol root, '/' separated.
    std::string name;
    std::uint64_t offset{};
    std::uint64_t raw_size{};
    std::uint64_t stored_size{};
    std::uint8_t codec{PACK_STORED_CODEC};
    std::uint8_t flags{};
    /// xxh3-64 of the raw bytes.
    std::uint64_t content_hash{};
    /// Compressed entries only: each block's stored size; a block stored at its raw size is raw.
    std::vector<std::uint32_t> stored_block_sizes;

    auto is_deleted() const -> bool
    {
        return (flags & PACK_ENTRY_DELETED) != 0;
    }
};

/// The bytes of a record without its name and block sizes.
constexpr std::size_t PACK_RECORD_FIXED_SIZE = sizeof(std::uint32_t) + 3 * sizeof(std::uint64_t) +
                                               2 * sizeof(std::uint8_t) + sizeof(std::uint64_t) +
                                               sizeof(std::uint32_t);

struct pack_footer
{
    std::uint32_t write_settings{PACK_UNCOMPRESSED_SETTINGS};
    std::uint64_t toc_offset{};
    std::uint64_t toc_size{};
    std::uint64_t toc_hash{};
    std::uint64_t base_hash{PACK_NO_BASE};
};

/// magic, version, write settings, toc offset, toc size, toc hash, base toc hash.
constexpr std::size_t PACK_FOOTER_SIZE = PACK_MAGIC.size() + 2 * sizeof(std::uint32_t) + 4 * sizeof(std::uint64_t);

template<typename T>
void append_value(byte_array_t& out, T value)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

/// Bounds-checked reads over a byte range; a read past the end sets the failed state and returns zero.
class byte_reader
{
public:
    byte_reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size)
    {
    }

    template<typename T>
    auto read() -> T
    {
        T value{};
        const auto* bytes = read_bytes(sizeof(T));
        if(bytes != nullptr)
        {
            std::memcpy(&value, bytes, sizeof(T));
        }
        return value;
    }

    auto read_bytes(std::size_t count) -> const std::uint8_t*
    {
        if(failed_ || count > get_remaining())
        {
            failed_ = true;
            return nullptr;
        }
        const auto* bytes = data_ + position_;
        position_ += count;
        return bytes;
    }

    auto get_remaining() const -> std::size_t
    {
        return size_ - position_;
    }

    auto has_failed() const -> bool
    {
        return failed_;
    }

    auto is_at_end() const -> bool
    {
        return position_ == size_;
    }

private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    std::size_t position_{};
    bool failed_{};
};

auto hash_bytes(const void* data, std::size_t size) -> std::uint64_t;

auto get_block_count(std::uint64_t raw_size) -> std::uint64_t;
auto get_raw_block_size(std::uint64_t raw_size, std::uint64_t block) -> std::size_t;

void append_record(byte_array_t& toc, const pack_record& record);
/// Reads one record; false when it is malformed or its bytes lie outside [0, data_end).
auto read_record(byte_reader& reader, std::uint64_t data_end, pack_record& record) -> bool;

void append_footer(byte_array_t& out, const pack_footer& footer);
/// Reads the footer at the end of `data`; false with `err` set when there is none or its version is
/// not this build's.
auto read_footer(const std::uint8_t* data, std::uint64_t size, pack_footer& footer, std::string& err) -> bool;

} // namespace fs::pack_detail
