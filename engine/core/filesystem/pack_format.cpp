#include "pack_format.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <algorithm>

namespace fs::pack_detail
{

auto hash_bytes(const void* data, std::size_t size) -> std::uint64_t
{
    return XXH3_64bits(data, size);
}

auto get_block_count(std::uint64_t raw_size) -> std::uint64_t
{
    return raw_size / PACK_BLOCK_SIZE + (raw_size % PACK_BLOCK_SIZE != 0 ? 1 : 0);
}

auto get_raw_block_size(std::uint64_t raw_size, std::uint64_t block) -> std::size_t
{
    const std::uint64_t start = block * PACK_BLOCK_SIZE;
    return std::size_t(std::min<std::uint64_t>(PACK_BLOCK_SIZE, raw_size - start));
}

void append_record(byte_array_t& toc, const pack_record& record)
{
    append_value(toc, std::uint32_t(record.name.size()));
    toc.insert(toc.end(), record.name.begin(), record.name.end());
    append_value(toc, record.offset);
    append_value(toc, record.raw_size);
    append_value(toc, record.stored_size);
    append_value(toc, record.codec);
    append_value(toc, record.flags);
    append_value(toc, record.content_hash);
    append_value(toc, std::uint32_t(record.stored_block_sizes.size()));
    for(const auto size : record.stored_block_sizes)
    {
        append_value(toc, size);
    }
}

namespace
{
/// The block sizes of a compressed record, which must add up to its stored size.
auto read_block_sizes(byte_reader& reader, std::uint32_t block_count, pack_record& record) -> bool
{
    // Checked before allocating: a damaged count must not become a huge allocation.
    if(std::uint64_t(block_count) * sizeof(std::uint32_t) > reader.get_remaining())
    {
        return false;
    }
    record.stored_block_sizes.resize(block_count);
    std::uint64_t stored_total = 0;
    for(auto& size : record.stored_block_sizes)
    {
        size = reader.read<std::uint32_t>();
        stored_total += size;
    }
    return !reader.has_failed() && stored_total == record.stored_size;
}

auto is_valid_deleted_record(const pack_record& record, std::uint32_t block_count) -> bool
{
    return record.raw_size == 0 && record.stored_size == 0 && block_count == 0;
}
} // namespace

auto read_record(byte_reader& reader, std::uint64_t data_end, pack_record& record) -> bool
{
    const auto name_size = reader.read<std::uint32_t>();
    const auto* name_bytes = reader.read_bytes(name_size);
    record.offset = reader.read<std::uint64_t>();
    record.raw_size = reader.read<std::uint64_t>();
    record.stored_size = reader.read<std::uint64_t>();
    record.codec = reader.read<std::uint8_t>();
    record.flags = reader.read<std::uint8_t>();
    record.content_hash = reader.read<std::uint64_t>();
    const auto block_count = reader.read<std::uint32_t>();
    if(reader.has_failed() || name_size == 0)
    {
        return false;
    }
    record.name.assign(reinterpret_cast<const char*>(name_bytes), name_size);
    if(record.is_deleted())
    {
        return is_valid_deleted_record(record, block_count);
    }
    if(record.raw_size > PACK_MAX_ENTRY_SIZE || record.offset > data_end || record.stored_size > data_end - record.offset)
    {
        return false;
    }
    if(record.codec == PACK_STORED_CODEC)
    {
        return block_count == 0 && record.stored_size == record.raw_size;
    }
    return block_count == get_block_count(record.raw_size) && read_block_sizes(reader, block_count, record);
}

void append_footer(byte_array_t& out, const pack_footer& footer)
{
    out.insert(out.end(), PACK_MAGIC.begin(), PACK_MAGIC.end());
    append_value(out, PACK_VERSION);
    append_value(out, footer.write_settings);
    append_value(out, footer.toc_offset);
    append_value(out, footer.toc_size);
    append_value(out, footer.toc_hash);
    append_value(out, footer.base_hash);
}

auto read_footer(const std::uint8_t* data, std::uint64_t size, pack_footer& footer, std::string& err) -> bool
{
    if(size < PACK_FOOTER_SIZE)
    {
        err = "too small to be a pack";
        return false;
    }
    byte_reader reader(data + size - PACK_FOOTER_SIZE, PACK_FOOTER_SIZE);
    const auto* magic = reader.read_bytes(PACK_MAGIC.size());
    const auto version = reader.read<std::uint32_t>();
    footer.write_settings = reader.read<std::uint32_t>();
    footer.toc_offset = reader.read<std::uint64_t>();
    footer.toc_size = reader.read<std::uint64_t>();
    footer.toc_hash = reader.read<std::uint64_t>();
    footer.base_hash = reader.read<std::uint64_t>();
    if(magic == nullptr || std::memcmp(magic, PACK_MAGIC.data(), PACK_MAGIC.size()) != 0)
    {
        err = "not a pack (no pack footer)";
        return false;
    }
    if(version != PACK_VERSION)
    {
        err = "pack version " + std::to_string(version) + " is not supported; deploy again with this engine";
        return false;
    }
    return true;
}

} // namespace fs::pack_detail
