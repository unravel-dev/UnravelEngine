#include "pack_image.h"

#include <array>
#include <utility>

namespace fs::pack_detail
{
namespace
{
/// The table of contents: an entry count, then one record per entry, every entry before the table.
auto parse_table_of_contents(pack_image& image, const pack_footer& footer, std::string& err) -> bool
{
    byte_reader toc(image.file.data() + footer.toc_offset, std::size_t(footer.toc_size));
    const auto entry_count = toc.read<std::uint32_t>();
    // Checked before allocating: a damaged count must not become a huge allocation.
    if(std::uint64_t(entry_count) * PACK_RECORD_FIXED_SIZE > toc.get_remaining())
    {
        err = "damaged pack (entry count)";
        return false;
    }
    image.entries.reserve(entry_count);
    for(std::uint32_t index = 0; index < entry_count; ++index)
    {
        pack_record record;
        if(!read_record(toc, footer.toc_offset, record))
        {
            err = "damaged pack (entry " + std::to_string(index) + ")";
            return false;
        }
        if(record.codec != PACK_STORED_CODEC && record.codec != compression::get_codec_id())
        {
            err = record.name + " is compressed with codec " + std::to_string(record.codec) + "; this build decodes " +
                  compression::get_codec_name() + " (" + std::to_string(compression::get_codec_id()) + ")";
            return false;
        }
        image.entries[make_path_key(record.name)] = std::move(record);
    }
    if(toc.has_failed() || !toc.is_at_end())
    {
        err = "damaged pack (table of contents size)";
        return false;
    }
    return true;
}

auto parse_pack(pack_image& image, std::string& err) -> bool
{
    const auto* data = image.file.data();
    const auto size = image.file.size();
    pack_footer footer;
    if(!read_footer(data, size, footer, err))
    {
        return false;
    }
    const std::uint64_t data_end = size - PACK_FOOTER_SIZE;
    if(footer.toc_offset > data_end || footer.toc_size != data_end - footer.toc_offset)
    {
        err = "damaged pack (table of contents out of range)";
        return false;
    }
    if(hash_bytes(data + footer.toc_offset, std::size_t(footer.toc_size)) != footer.toc_hash)
    {
        err = "damaged pack (table of contents hash mismatch)";
        return false;
    }
    image.write_settings = footer.write_settings;
    image.toc_hash = footer.toc_hash;
    image.base_hash = footer.base_hash;
    return parse_table_of_contents(image, footer, err);
}
} // namespace

auto pack_image::find(const std::string& name) const -> const pack_record*
{
    const auto found = entries.find(make_path_key(name));
    return found == entries.end() ? nullptr : &found->second;
}

auto open_pack_image(const path& file, pack_image& image, std::string& err) -> bool
{
    return image.file.open(file, err) && parse_pack(image, err);
}

auto open_pack_image(file_region region, pack_image& image, std::string& err) -> bool
{
    return image.file.open(std::move(region), err) && parse_pack(image, err);
}

auto decode_entry(const pack_image& image, const pack_record& entry) -> std::shared_ptr<const byte_array_t>
{
    // One per thread: entries decode on the loading workers, in parallel.
    thread_local compression::decompressor decompressor;
    auto raw = std::make_shared<byte_array_t>(std::size_t(entry.raw_size));
    const auto* source = image.file.data() + entry.offset;
    for(std::size_t block = 0; block < entry.stored_block_sizes.size(); ++block)
    {
        auto* destination = raw->data() + block * PACK_BLOCK_SIZE;
        const std::size_t raw_block_size = get_raw_block_size(entry.raw_size, block);
        const std::size_t stored_block_size = entry.stored_block_sizes[block];
        if(stored_block_size == raw_block_size)
        {
            std::memcpy(destination, source, raw_block_size);
        }
        else if(!decompressor.decompress(source, stored_block_size, destination, raw_block_size))
        {
            return nullptr;
        }
        source += stored_block_size;
    }
    return raw;
}

auto is_entry_intact(const pack_image& image, const pack_record& entry) -> bool
{
    if(entry.codec == PACK_STORED_CODEC)
    {
        return hash_bytes(image.file.data() + entry.offset, std::size_t(entry.stored_size)) == entry.content_hash;
    }
    const auto raw = decode_entry(image, entry);
    return raw != nullptr && hash_bytes(raw->data(), raw->size()) == entry.content_hash;
}

auto get_write_settings(bool compress, compression::level compression_level) -> std::uint32_t
{
    if(!compress)
    {
        return PACK_UNCOMPRESSED_SETTINGS;
    }
    const std::array<std::uint64_t, 5> settings = {std::uint64_t(compression_level),
                                                   std::uint64_t(compression::get_codec_id()),
                                                   std::uint64_t(compression::get_codec_version()),
                                                   std::uint64_t(PACK_BLOCK_SIZE),
                                                   std::uint64_t(PACK_MIN_SAVED_DIVISOR)};
    const auto fingerprint = std::uint32_t(hash_bytes(settings.data(), sizeof(settings)));
    // Never the uncompressed value, so a compressed pack cannot pass for an uncompressed one.
    return fingerprint == PACK_UNCOMPRESSED_SETTINGS ? 1 : fingerprint;
}

} // namespace fs::pack_detail
