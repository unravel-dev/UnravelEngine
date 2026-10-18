#include "pack.h"
#include "pack_image.h"

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace fs::pack_detail
{
namespace
{

/// The bytes an entry stores, and how: whole and raw, or compressed blocks.
struct encoded_entry
{
    byte_array_t stored;
    std::uint8_t codec{PACK_STORED_CODEC};
    std::vector<std::uint32_t> stored_block_sizes;
};

auto is_block_worth_compressing(std::size_t packed_size, std::size_t raw_block_size) -> bool
{
    return packed_size < raw_block_size - raw_block_size / PACK_MIN_SAVED_DIVISOR;
}

auto compress_blocks(const byte_array_t& raw, compression::compressor& compressor, encoded_entry& out, std::string& err)
    -> bool
{
    const std::uint64_t block_count = get_block_count(raw.size());
    byte_array_t scratch(compressor.get_bound(PACK_BLOCK_SIZE));
    bool has_compressed_block = false;
    out.stored.clear();
    out.stored_block_sizes.clear();
    for(std::uint64_t block = 0; block < block_count; ++block)
    {
        const auto* source = raw.data() + std::size_t(block) * PACK_BLOCK_SIZE;
        const std::size_t raw_block_size = get_raw_block_size(raw.size(), block);
        const std::size_t packed_size = compressor.compress(source, raw_block_size, scratch.data(), scratch.size(), err);
        if(packed_size == 0)
        {
            return false;
        }
        const bool is_compressed = is_block_worth_compressing(packed_size, raw_block_size);
        const auto* stored = is_compressed ? scratch.data() : source;
        const std::size_t stored_size = is_compressed ? packed_size : raw_block_size;
        out.stored.insert(out.stored.end(), stored, stored + stored_size);
        out.stored_block_sizes.push_back(std::uint32_t(stored_size));
        has_compressed_block = has_compressed_block || is_compressed;
    }
    out.codec = compression::get_codec_id();
    if(!has_compressed_block)
    {
        // Nothing shrank (already compressed data): the plain bytes read without a block table.
        out.stored = raw;
        out.stored_block_sizes.clear();
        out.codec = PACK_STORED_CODEC;
    }
    return true;
}

auto read_input(const path& source, byte_array_t& out) -> bool
{
    std::ifstream stream(source, std::ios::binary);
    if(!stream.good())
    {
        return false;
    }
    out = read_stream(stream);
    return !stream.bad();
}

void write_bytes(std::ofstream& out, const std::uint8_t* bytes, std::size_t count)
{
    out.write(reinterpret_cast<const char*>(bytes), std::streamsize(count));
}

/// Turns an input's bytes into an entry: from an earlier pack when it holds the same bytes intact,
/// else by compressing them (or storing them whole, for an uncompressed pack).
class entry_encoder
{
public:
    explicit entry_encoder(const pack_write_options& options) : write_settings_(get_write_settings(options.compress, options.compression_level))
    {
        if(options.compress)
        {
            compressor_ = std::make_unique<compression::compressor>(options.compression_level);
        }
        open_reuse_source(options.reuse_from);
    }

    auto encode(byte_array_t raw, std::uint64_t content_hash, encoded_entry& out, bool& is_reused, std::string& err)
        -> bool
    {
        const auto* previous = find_reusable(content_hash, raw.size());
        is_reused = previous != nullptr;
        if(is_reused)
        {
            const auto* stored = reuse_pack_->file.data() + previous->offset;
            out.stored.assign(stored, stored + previous->stored_size);
            out.codec = previous->codec;
            out.stored_block_sizes = previous->stored_block_sizes;
            return true;
        }
        if(compressor_ != nullptr && !raw.empty())
        {
            return compress_blocks(raw, *compressor_, out, err);
        }
        out.stored = std::move(raw);
        out.codec = PACK_STORED_CODEC;
        out.stored_block_sizes.clear();
        return true;
    }

private:
    /// The earlier pack is only a shortcut: one that is missing, damaged or written with other settings
    /// is left out, and every input is encoded afresh.
    void open_reuse_source(const path& reuse_from)
    {
        error_code ec;
        if(reuse_from.empty() || !exists(reuse_from, ec))
        {
            return;
        }
        auto pack = std::make_unique<pack_image>();
        std::string ignored;
        if(!open_pack_image(reuse_from, *pack, ignored) || pack->write_settings != write_settings_)
        {
            return;
        }
        for(const auto& [key, entry] : pack->entries)
        {
            if(!entry.is_deleted())
            {
                reusable_.emplace(entry.content_hash, &entry);
            }
        }
        reuse_pack_ = std::move(pack);
    }

    auto find_reusable(std::uint64_t content_hash, std::uint64_t raw_size) const -> const pack_record*
    {
        const auto [first, last] = reusable_.equal_range(content_hash);
        const auto found = std::find_if(first,
                                        last,
                                        [&](const auto& candidate)
                                        {
                                            return candidate.second->raw_size == raw_size &&
                                                   is_entry_intact(*reuse_pack_, *candidate.second);
                                        });
        return found == last ? nullptr : found->second;
    }

    std::uint32_t write_settings_{};
    std::unique_ptr<compression::compressor> compressor_;
    std::unique_ptr<pack_image> reuse_pack_;
    std::unordered_multimap<std::uint64_t, const pack_record*> reusable_;
};

/// Writes a pack: the entries' bytes in the order they are added, then the table of contents that
/// describes them and the footer. For a patch, an input the base holds with the same bytes is left out
/// and the base's names no input has become deleted entries.
class pack_writer
{
public:
    pack_writer(std::ofstream& out, const pack_write_options& options, const pack_image* patch_base)
        : out_(out)
        , encoder_(options)
        , write_settings_(get_write_settings(options.compress, options.compression_level))
        , patch_base_(patch_base)
    {
        // The entry count, set once finish knows it.
        append_value(toc_, std::uint32_t(0));
    }

    auto add(const pack_input& input, std::string& err) -> bool
    {
        if(input.name.empty() || !names_.insert(make_path_key(input.name)).second)
        {
            err = "pack entry name is empty or repeated: " + input.name;
            return false;
        }
        byte_array_t raw;
        if(!read_input(input.source, raw))
        {
            err = "cannot read " + input.source.generic_string();
            return false;
        }
        const std::uint64_t raw_size = raw.size();
        const std::uint64_t content_hash = hash_bytes(raw.data(), raw.size());
        if(is_unchanged_in_base(input.name, content_hash, raw_size))
        {
            ++stats_.unchanged_count;
            return true;
        }
        encoded_entry entry;
        bool is_reused = false;
        if(!encoder_.encode(std::move(raw), content_hash, entry, is_reused, err))
        {
            err = input.name + ": " + err;
            return false;
        }
        stats_.reused_count += is_reused ? 1 : 0;
        write_padding();
        // The name as given keeps its case; lookups compare it by make_path_key.
        append_record(toc_,
                      {input.name, data_size_, raw_size, entry.stored.size(), entry.codec, 0, content_hash, entry.stored_block_sizes});
        write_bytes(out_, entry.stored.data(), entry.stored.size());
        data_size_ += entry.stored.size();
        stats_.raw_bytes += raw_size;
        stats_.stored_bytes += entry.stored.size();
        ++entry_count_;
        return true;
    }

    /// Ends the pack: a patch's deleted entries, the table of contents and the footer.
    auto finish(const path& file, std::string& err) -> bool
    {
        add_deleted_entries();
        std::memcpy(toc_.data(), &entry_count_, sizeof(entry_count_));
        stats_.entry_count = entry_count_;
        write_bytes(out_, toc_.data(), toc_.size());
        pack_footer footer;
        footer.write_settings = write_settings_;
        footer.toc_offset = data_size_;
        footer.toc_size = toc_.size();
        footer.toc_hash = hash_bytes(toc_.data(), toc_.size());
        footer.base_hash = patch_base_ != nullptr ? patch_base_->toc_hash : PACK_NO_BASE;
        byte_array_t footer_bytes;
        append_footer(footer_bytes, footer);
        write_bytes(out_, footer_bytes.data(), footer_bytes.size());
        out_.close();
        if(!out_.good())
        {
            err = "cannot write " + file.generic_string();
            return false;
        }
        return true;
    }

    auto get_stats() const -> const pack_write_stats&
    {
        return stats_;
    }

private:
    auto is_unchanged_in_base(const std::string& name, std::uint64_t content_hash, std::uint64_t raw_size) const -> bool
    {
        const auto* base_entry = patch_base_ != nullptr ? patch_base_->find(name) : nullptr;
        return base_entry != nullptr && !base_entry->is_deleted() && base_entry->content_hash == content_hash &&
               base_entry->raw_size == raw_size;
    }

    /// The base's names that no input had, in name order so the patch is deterministic.
    void add_deleted_entries()
    {
        if(patch_base_ == nullptr)
        {
            return;
        }
        std::vector<std::string> deleted;
        for(const auto& [key, entry] : patch_base_->entries)
        {
            if(!entry.is_deleted() && !names_.contains(key))
            {
                deleted.push_back(entry.name);
            }
        }
        std::sort(deleted.begin(), deleted.end());
        for(const auto& name : deleted)
        {
            pack_record record;
            record.name = name;
            record.flags = PACK_ENTRY_DELETED;
            append_record(toc_, record);
            ++entry_count_;
            ++stats_.deleted_count;
        }
    }

    void write_padding()
    {
        static const byte_array_t padding(PACK_ENTRY_ALIGNMENT, 0);
        const std::uint64_t size = (PACK_ENTRY_ALIGNMENT - data_size_ % PACK_ENTRY_ALIGNMENT) % PACK_ENTRY_ALIGNMENT;
        write_bytes(out_, padding.data(), std::size_t(size));
        data_size_ += size;
    }

    std::ofstream& out_;
    entry_encoder encoder_;
    std::uint32_t write_settings_{};
    const pack_image* patch_base_{};
    byte_array_t toc_;
    std::unordered_set<std::string> names_;
    std::uint64_t data_size_{};
    std::uint32_t entry_count_{};
    pack_write_stats stats_;
};

/// The base a patch is written against: a full pack, since the writer compares the inputs with the
/// base's own entries only.
auto open_patch_base(const path& file, std::unique_ptr<pack_image>& base, std::string& err) -> bool
{
    if(file.empty())
    {
        return true;
    }
    base = std::make_unique<pack_image>();
    if(!open_pack_image(file, *base, err))
    {
        err = "patch base " + file.generic_string() + ": " + err;
        return false;
    }
    if(base->is_patch())
    {
        err = "patch base " + file.generic_string() + " is a patch itself; patch a full pack";
        return false;
    }
    return true;
}

/// Writes the whole pack into `file`. The reuse source and the patch base are closed when this returns.
auto write_pack_file(const path& file,
                     const std::vector<pack_input>& inputs,
                     const pack_write_options& options,
                     pack_write_stats& totals,
                     std::string& err) -> bool
{
    std::unique_ptr<pack_image> patch_base;
    if(!open_patch_base(options.patch_base, patch_base, err))
    {
        return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if(!out.good())
    {
        err = "cannot write " + file.generic_string();
        return false;
    }
    pack_writer writer(out, options, patch_base.get());
    const bool is_written = std::all_of(inputs.begin(),
                                        inputs.end(),
                                        [&](const pack_input& input)
                                        {
                                            return writer.add(input, err);
                                        }) &&
                            writer.finish(file, err);
    if(is_written)
    {
        totals = writer.get_stats();
    }
    return is_written;
}
} // namespace
} // namespace fs::pack_detail

namespace fs
{

auto write_pack(const path& pack_file,
                const std::vector<pack_input>& inputs,
                const pack_write_options& options,
                std::string& err,
                pack_write_stats* stats) -> bool
{
    path temp = pack_file;
    temp += ".tmp";
    pack_write_stats totals;
    error_code ec;
    if(!pack_detail::write_pack_file(temp, inputs, options, totals, err))
    {
        remove(temp, ec);
        return false;
    }
    rename(temp, pack_file, ec);
    if(ec)
    {
        err = "cannot move " + temp.generic_string() + " into place: " + ec.message();
        remove(temp, ec);
        return false;
    }
    if(stats != nullptr)
    {
        *stats = totals;
    }
    return true;
}

} // namespace fs
