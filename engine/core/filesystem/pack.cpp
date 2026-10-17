#include "pack.h"
#include "file_istream.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <compression/compression.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs
{
namespace
{

constexpr std::array<char, 8> PACK_MAGIC = {'U', 'N', 'R', 'V', 'P', 'A', 'K', '1'};
/// 2: entries carry flags (deleted), the footer the hash of the base a patch was made against.
constexpr std::uint32_t PACK_VERSION = 2;
/// magic, version, write settings, toc offset, toc size, toc hash, base toc hash (0: not a patch).
constexpr std::size_t PACK_FOOTER_SIZE = 8 + 4 + 4 + 8 + 8 + 8 + 8;
/// Entry flag: the name is deleted - it hides the file of an earlier pack and of the disk.
constexpr std::uint8_t PACK_ENTRY_DELETED = 1;
/// The base hash of a pack that is not a patch.
constexpr std::uint64_t PACK_NO_BASE = 0;
/// Write settings of an uncompressed pack; a compressed one sets bit 0, the level in bits 8-15 and the
/// codec id in bits 16-23. Entries are reused only from a pack written with the same settings.
constexpr std::uint32_t PACK_UNCOMPRESSED_SETTINGS = 0;
constexpr std::uint64_t PACK_ENTRY_ALIGNMENT = 16;
constexpr std::size_t PACK_BLOCK_SIZE = 64 * 1024;
/// A block is kept compressed only when it saves at least this share of its size (1/20 = 5%).
constexpr std::size_t PACK_MIN_SAVED_DIVISOR = 20;
/// The codec byte of an entry stored as it is; any other value is a compression codec id.
constexpr std::uint8_t PACK_STORED_CODEC = 0;

//-----------------------------------------------------------------------------
// Little-endian encoding. Every supported platform is little-endian, so values are copied as they
// are in memory.
//-----------------------------------------------------------------------------
template<typename T>
void append_value(byte_array_t& out, T value)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

/// Bounds-checked reads over a byte range; a read past the end sets `failed` and returns zero.
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
        if(failed_ || count > size_ - position_)
        {
            failed_ = true;
            return nullptr;
        }
        const auto* bytes = data_ + position_;
        position_ += count;
        return bytes;
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

/// Entry names compare the way paths do on the platform: case-insensitively on Windows.
auto normalize_name(std::string name) -> std::string
{
    std::replace(name.begin(), name.end(), '\\', '/');
#ifdef _WIN32
    std::transform(name.begin(),
                   name.end(),
                   name.begin(),
                   [](char c)
                   {
                       return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
                   });
#endif
    return name;
}

auto get_block_count(std::uint64_t raw_size) -> std::uint32_t
{
    return std::uint32_t((raw_size + PACK_BLOCK_SIZE - 1) / PACK_BLOCK_SIZE);
}

auto get_raw_block_size(std::uint64_t raw_size, std::uint32_t block) -> std::size_t
{
    const std::uint64_t start = std::uint64_t(block) * PACK_BLOCK_SIZE;
    return std::size_t(std::min<std::uint64_t>(PACK_BLOCK_SIZE, raw_size - start));
}

//-----------------------------------------------------------------------------
// Read-only mapping of a whole file.
//-----------------------------------------------------------------------------
class mapped_file
{
public:
    mapped_file() = default;
    mapped_file(const mapped_file&) = delete;
    auto operator=(const mapped_file&) -> mapped_file& = delete;

    ~mapped_file()
    {
#ifdef _WIN32
        if(data_ != nullptr)
        {
            UnmapViewOfFile(data_);
        }
        if(mapping_ != nullptr)
        {
            CloseHandle(mapping_);
        }
        if(file_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(file_);
        }
#else
        if(data_ != nullptr)
        {
            munmap(const_cast<std::uint8_t*>(data_), std::size_t(size_));
        }
        if(descriptor_ >= 0)
        {
            close(descriptor_);
        }
#endif
    }

    auto open(const path& file, std::string& err) -> bool
    {
#ifdef _WIN32
        file_ = CreateFileW(file.c_str(),
                            GENERIC_READ,
                            FILE_SHARE_READ,
                            nullptr,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            nullptr);
        if(file_ == INVALID_HANDLE_VALUE)
        {
            err = "cannot open " + file.generic_string();
            return false;
        }
        LARGE_INTEGER file_size{};
        if(!GetFileSizeEx(file_, &file_size) || file_size.QuadPart <= 0)
        {
            err = "cannot read the size of " + file.generic_string();
            return false;
        }
        size_ = std::uint64_t(file_size.QuadPart);
        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if(mapping_ == nullptr)
        {
            err = "cannot map " + file.generic_string();
            return false;
        }
        data_ = static_cast<const std::uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
#else
        descriptor_ = ::open(file.c_str(), O_RDONLY);
        if(descriptor_ < 0)
        {
            err = "cannot open " + file.generic_string();
            return false;
        }
        struct stat info{};
        if(fstat(descriptor_, &info) != 0 || info.st_size <= 0)
        {
            err = "cannot read the size of " + file.generic_string();
            return false;
        }
        size_ = std::uint64_t(info.st_size);
        void* mapped = mmap(nullptr, std::size_t(size_), PROT_READ, MAP_PRIVATE, descriptor_, 0);
        data_ = mapped == MAP_FAILED ? nullptr : static_cast<const std::uint8_t*>(mapped);
#endif
        if(data_ == nullptr)
        {
            err = "cannot map " + file.generic_string();
            return false;
        }
        return true;
    }

    auto data() const -> const std::uint8_t*
    {
        return data_;
    }

    auto size() const -> std::uint64_t
    {
        return size_;
    }

private:
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
#else
    int descriptor_ = -1;
#endif
    const std::uint8_t* data_{};
    std::uint64_t size_{};
};

struct pack_entry
{
    /// The name as written; the entries map is keyed by its normalized form.
    std::string name;
    std::uint64_t offset{};
    std::uint64_t raw_size{};
    std::uint64_t stored_size{};
    std::uint8_t codec{PACK_STORED_CODEC};
    bool is_deleted{};
    std::uint64_t content_hash{};
    std::vector<std::uint32_t> stored_block_sizes;
};

struct mounted_pack
{
    std::string protocol;
    mapped_file file;
    std::unordered_map<std::string, pack_entry> entries;
    std::uint32_t write_settings{PACK_UNCOMPRESSED_SETTINGS};
    /// Identifies the pack: the hash of its table of contents.
    std::uint64_t toc_hash{};
    /// For a patch, the toc_hash of the pack it was made against.
    std::uint64_t base_hash{PACK_NO_BASE};
};

auto get_write_settings(const pack_write_options& options) -> std::uint32_t
{
    if(!options.compress)
    {
        return PACK_UNCOMPRESSED_SETTINGS;
    }
    return 1u | (std::uint32_t(options.compression_level) << 8) | (std::uint32_t(compression::get_codec_id()) << 16);
}

/// The mounted packs, the latest last. Lookups take the shared lock only to copy what they need.
std::shared_mutex mounts_mutex;
std::vector<std::shared_ptr<const mounted_pack>> mounted_packs;

//-----------------------------------------------------------------------------
// Writing
//-----------------------------------------------------------------------------

/// The bytes an entry stores, and how: whole and raw, or compressed blocks.
struct encoded_entry
{
    byte_array_t stored;
    std::uint8_t codec{PACK_STORED_CODEC};
    std::vector<std::uint32_t> stored_block_sizes;
};

auto compress_blocks(const byte_array_t& raw, compression::compressor& compressor, encoded_entry& out, std::string& err)
    -> bool
{
    const std::uint32_t block_count = get_block_count(raw.size());
    byte_array_t scratch(compressor.get_bound(PACK_BLOCK_SIZE));
    bool has_compressed_block = false;
    out.stored.clear();
    out.stored_block_sizes.clear();
    for(std::uint32_t block = 0; block < block_count; ++block)
    {
        const auto* source = raw.data() + std::size_t(block) * PACK_BLOCK_SIZE;
        const std::size_t raw_block_size = get_raw_block_size(raw.size(), block);
        const std::size_t packed_size =
            compressor.compress(source, raw_block_size, scratch.data(), scratch.size(), err);
        if(packed_size == 0)
        {
            return false;
        }
        const bool is_worth_it = packed_size < raw_block_size - raw_block_size / PACK_MIN_SAVED_DIVISOR;
        if(is_worth_it)
        {
            out.stored.insert(out.stored.end(), scratch.data(), scratch.data() + packed_size);
            out.stored_block_sizes.push_back(std::uint32_t(packed_size));
            has_compressed_block = true;
        }
        else
        {
            out.stored.insert(out.stored.end(), source, source + raw_block_size);
            out.stored_block_sizes.push_back(std::uint32_t(raw_block_size));
        }
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

/// `compressor` is null when the pack is written uncompressed.
auto encode_entry(byte_array_t raw, compression::compressor* compressor, encoded_entry& out, std::string& err) -> bool
{
    if(compressor != nullptr && !raw.empty())
    {
        return compress_blocks(raw, *compressor, out, err);
    }
    out.stored = std::move(raw);
    out.codec = PACK_STORED_CODEC;
    out.stored_block_sizes.clear();
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

void append_entry_record(byte_array_t& toc,
                         const std::string& name,
                         std::uint64_t offset,
                         std::uint64_t raw_size,
                         std::uint64_t content_hash,
                         const encoded_entry& entry)
{
    append_value(toc, std::uint32_t(name.size()));
    toc.insert(toc.end(), name.begin(), name.end());
    append_value(toc, offset);
    append_value(toc, raw_size);
    append_value(toc, std::uint64_t(entry.stored.size()));
    append_value(toc, entry.codec);
    append_value(toc, std::uint8_t(0));
    append_value(toc, content_hash);
    append_value(toc, std::uint32_t(entry.stored_block_sizes.size()));
    for(const auto size : entry.stored_block_sizes)
    {
        append_value(toc, size);
    }
}

/// A deleted entry: a name only, with no bytes.
void append_deleted_record(byte_array_t& toc, const std::string& name)
{
    append_value(toc, std::uint32_t(name.size()));
    toc.insert(toc.end(), name.begin(), name.end());
    append_value(toc, std::uint64_t(0));
    append_value(toc, std::uint64_t(0));
    append_value(toc, std::uint64_t(0));
    append_value(toc, PACK_STORED_CODEC);
    append_value(toc, PACK_ENTRY_DELETED);
    append_value(toc, std::uint64_t(0));
    append_value(toc, std::uint32_t(0));
}

auto make_footer(std::uint64_t toc_offset, const byte_array_t& toc, std::uint32_t write_settings, std::uint64_t base_hash)
    -> byte_array_t
{
    byte_array_t footer;
    footer.insert(footer.end(), PACK_MAGIC.begin(), PACK_MAGIC.end());
    append_value(footer, PACK_VERSION);
    append_value(footer, write_settings);
    append_value(footer, toc_offset);
    append_value(footer, std::uint64_t(toc.size()));
    append_value(footer, std::uint64_t(XXH3_64bits(toc.data(), toc.size())));
    append_value(footer, base_hash);
    return footer;
}

//-----------------------------------------------------------------------------
// Reading
//-----------------------------------------------------------------------------

auto parse_entry(byte_reader& reader, std::uint64_t data_end, std::string& name, pack_entry& entry) -> bool
{
    const auto name_size = reader.read<std::uint32_t>();
    const auto* name_bytes = reader.read_bytes(name_size);
    entry.offset = reader.read<std::uint64_t>();
    entry.raw_size = reader.read<std::uint64_t>();
    entry.stored_size = reader.read<std::uint64_t>();
    entry.codec = reader.read<std::uint8_t>();
    const auto flags = reader.read<std::uint8_t>();
    entry.content_hash = reader.read<std::uint64_t>();
    const auto block_count = reader.read<std::uint32_t>();
    if(reader.has_failed() || name_size == 0)
    {
        return false;
    }
    name.assign(reinterpret_cast<const char*>(name_bytes), name_size);
    entry.is_deleted = (flags & PACK_ENTRY_DELETED) != 0;
    if(entry.is_deleted)
    {
        return entry.raw_size == 0 && entry.stored_size == 0 && block_count == 0;
    }
    if(entry.offset > data_end || entry.stored_size > data_end - entry.offset)
    {
        return false;
    }
    if(entry.codec == PACK_STORED_CODEC)
    {
        return block_count == 0 && entry.stored_size == entry.raw_size;
    }
    if(block_count != get_block_count(entry.raw_size))
    {
        return false;
    }
    entry.stored_block_sizes.resize(block_count);
    std::uint64_t stored_total = 0;
    for(std::uint32_t block = 0; block < block_count; ++block)
    {
        entry.stored_block_sizes[block] = reader.read<std::uint32_t>();
        stored_total += entry.stored_block_sizes[block];
    }
    return !reader.has_failed() && stored_total == entry.stored_size;
}

auto parse_pack(mounted_pack& pack, std::string& err) -> bool
{
    const auto* data = pack.file.data();
    const auto size = pack.file.size();
    if(size < PACK_FOOTER_SIZE)
    {
        err = "too small to be a pack";
        return false;
    }
    byte_reader footer(data + size - PACK_FOOTER_SIZE, PACK_FOOTER_SIZE);
    const auto* magic = footer.read_bytes(PACK_MAGIC.size());
    const auto version = footer.read<std::uint32_t>();
    pack.write_settings = footer.read<std::uint32_t>();
    const auto toc_offset = footer.read<std::uint64_t>();
    const auto toc_size = footer.read<std::uint64_t>();
    pack.toc_hash = footer.read<std::uint64_t>();
    pack.base_hash = footer.read<std::uint64_t>();
    const auto toc_hash = pack.toc_hash;
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
    const std::uint64_t data_end = size - PACK_FOOTER_SIZE;
    if(toc_offset > data_end || toc_size != data_end - toc_offset)
    {
        err = "damaged pack (table of contents out of range)";
        return false;
    }
    if(XXH3_64bits(data + toc_offset, std::size_t(toc_size)) != toc_hash)
    {
        err = "damaged pack (table of contents hash mismatch)";
        return false;
    }
    byte_reader toc(data + toc_offset, std::size_t(toc_size));
    const auto entry_count = toc.read<std::uint32_t>();
    pack.entries.reserve(entry_count);
    for(std::uint32_t index = 0; index < entry_count; ++index)
    {
        std::string name;
        pack_entry entry;
        if(!parse_entry(toc, toc_offset, name, entry))
        {
            err = "damaged pack (entry " + std::to_string(index) + ")";
            return false;
        }
        if(entry.codec != PACK_STORED_CODEC && entry.codec != compression::get_codec_id())
        {
            err = name + " is compressed with codec " + std::to_string(entry.codec) + "; this build decodes " +
                  compression::get_codec_name() + " (" + std::to_string(compression::get_codec_id()) + ")";
            return false;
        }
        entry.name = name;
        pack.entries[normalize_name(std::move(name))] = std::move(entry);
    }
    if(toc.has_failed() || !toc.is_at_end())
    {
        err = "damaged pack (table of contents size)";
        return false;
    }
    return true;
}

auto open_pack_file(const path& file, mounted_pack& pack, std::string& err) -> bool
{
    return pack.file.open(file, err) && parse_pack(pack, err);
}

/// The protocol and entry name `file` maps to; false when it is under no protocol.
auto split_into_protocol_and_name(const path& file, std::string& protocol, std::string& name) -> bool
{
    const auto key = (has_known_protocol(file) ? file : convert_to_protocol(file)).generic_string();
    const auto separator = key.find(":/");
    if(separator == std::string::npos || separator == 0)
    {
        return false;
    }
    protocol = key.substr(0, separator);
    name = normalize_name(key.substr(separator + 2));
    return !name.empty();
}

struct entry_lookup
{
    std::shared_ptr<const mounted_pack> pack;
    const pack_entry* entry{};
};

auto find_entry(const path& file) -> entry_lookup
{
    std::vector<std::shared_ptr<const mounted_pack>> candidates;
    {
        std::shared_lock lock(mounts_mutex);
        if(mounted_packs.empty())
        {
            return {};
        }
        candidates = mounted_packs;
    }
    std::string protocol;
    std::string name;
    if(!split_into_protocol_and_name(file, protocol, name))
    {
        return {};
    }
    for(auto it = candidates.rbegin(); it != candidates.rend(); ++it)
    {
        if((*it)->protocol != protocol)
        {
            continue;
        }
        const auto found = (*it)->entries.find(name);
        if(found != (*it)->entries.end())
        {
            return {*it, &found->second};
        }
    }
    return {};
}

auto decode_entry(const mounted_pack& pack, const pack_entry& entry) -> std::shared_ptr<const byte_array_t>
{
    // One per thread: entries decode on the loading workers, in parallel.
    thread_local compression::decompressor decompressor;
    auto raw = std::make_shared<byte_array_t>(std::size_t(entry.raw_size));
    const auto* source = pack.file.data() + entry.offset;
    for(std::uint32_t block = 0; block < std::uint32_t(entry.stored_block_sizes.size()); ++block)
    {
        auto* destination = raw->data() + std::size_t(block) * PACK_BLOCK_SIZE;
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

/// A read-only stream over bytes it does not own, seekable both ways.
class view_streambuf : public std::streambuf
{
public:
    view_streambuf(const std::uint8_t* data, std::size_t size)
    {
        auto* begin = reinterpret_cast<char*>(const_cast<std::uint8_t*>(data));
        setg(begin, begin, begin + size);
    }

protected:
    auto seekoff(std::streamoff offset, std::ios_base::seekdir direction, std::ios_base::openmode which)
        -> std::streampos override
    {
        if((which & std::ios_base::in) == 0)
        {
            return std::streampos(std::streamoff(-1));
        }
        std::streamoff base = 0;
        if(direction == std::ios_base::cur)
        {
            base = gptr() - eback();
        }
        else if(direction == std::ios_base::end)
        {
            base = egptr() - eback();
        }
        const std::streamoff target = base + offset;
        if(target < 0 || target > egptr() - eback())
        {
            return std::streampos(std::streamoff(-1));
        }
        setg(eback(), eback() + target, egptr());
        return std::streampos(target);
    }

    auto seekpos(std::streampos position, std::ios_base::openmode which) -> std::streampos override
    {
        return seekoff(std::streamoff(position), std::ios_base::beg, which);
    }
};

/// A stream over a packed file that keeps the file's storage alive.
class packed_istream : public std::istream
{
public:
    explicit packed_istream(packed_file file)
        : std::istream(nullptr)
        , file_(std::move(file))
        , buffer_(file_.data, file_.size)
    {
        rdbuf(&buffer_);
    }

private:
    packed_file file_;
    view_streambuf buffer_;
};

void write_bytes(std::ofstream& out, const byte_array_t& bytes, std::size_t count)
{
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(count));
}

/// Turns an input's bytes into an entry: from an earlier pack when it holds the same bytes, else by
/// compressing them (or storing them whole, for an uncompressed pack).
class entry_encoder
{
public:
    explicit entry_encoder(const pack_write_options& options)
    {
        if(options.compress)
        {
            compressor_ = std::make_unique<compression::compressor>(options.compression_level);
        }
        open_reuse_source(options);
    }

    auto encode(byte_array_t raw, std::uint64_t content_hash, encoded_entry& out, bool& is_reused, std::string& err)
        -> bool
    {
        const auto* previous = find_reusable(content_hash, raw.size());
        is_reused = previous != nullptr;
        if(!is_reused)
        {
            return encode_entry(std::move(raw), compressor_.get(), out, err);
        }
        const auto* stored = reuse_pack_->file.data() + previous->offset;
        out.stored.assign(stored, stored + previous->stored_size);
        out.codec = previous->codec;
        out.stored_block_sizes = previous->stored_block_sizes;
        return true;
    }

private:
    /// The earlier pack is only a shortcut: one that is missing, damaged or written with other
    /// settings is left out, and every input is encoded afresh.
    void open_reuse_source(const pack_write_options& options)
    {
        error_code ec;
        if(options.reuse_from.empty() || !exists(options.reuse_from, ec))
        {
            return;
        }
        auto pack = std::make_unique<mounted_pack>();
        std::string ignored;
        if(!open_pack_file(options.reuse_from, *pack, ignored) || pack->write_settings != get_write_settings(options))
        {
            return;
        }
        for(const auto& [name, entry] : pack->entries)
        {
            if(!entry.is_deleted)
            {
                reusable_.emplace(entry.content_hash, &entry);
            }
        }
        reuse_pack_ = std::move(pack);
    }

    auto find_reusable(std::uint64_t content_hash, std::uint64_t raw_size) const -> const pack_entry*
    {
        const auto [first, last] = reusable_.equal_range(content_hash);
        for(auto it = first; it != last; ++it)
        {
            if(it->second->raw_size == raw_size)
            {
                return it->second;
            }
        }
        return nullptr;
    }

    std::unique_ptr<compression::compressor> compressor_;
    std::unique_ptr<mounted_pack> reuse_pack_;
    std::unordered_multimap<std::uint64_t, const pack_entry*> reusable_;
};

/// Writes a pack: the entries' bytes in the order they are added, then the table of contents that
/// describes them and the footer. For a patch, an input the base holds with the same bytes is left out
/// and the base's names no input has become deleted entries.
class pack_writer
{
public:
    pack_writer(std::ofstream& out, const pack_write_options& options, const mounted_pack* patch_base)
        : out_(out)
        , encoder_(options)
        , write_settings_(get_write_settings(options))
        , patch_base_(patch_base)
    {
        // The entry count, set once finish knows it.
        append_value(toc_, std::uint32_t(0));
    }

    auto add(const pack_input& input, std::string& err) -> bool
    {
        const auto name = normalize_name(input.name);
        if(input.name.empty() || !names_.insert(name).second)
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
        const std::uint64_t content_hash = XXH3_64bits(raw.data(), raw.size());
        if(is_unchanged_in_base(name, content_hash, raw_size))
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
        // The name as given keeps its case; lookups compare it normalized.
        append_entry_record(toc_, input.name, data_size_, raw_size, content_hash, entry);
        write_bytes(out_, entry.stored, entry.stored.size());
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
        write_bytes(out_, toc_, toc_.size());
        const auto base_hash = patch_base_ != nullptr ? patch_base_->toc_hash : PACK_NO_BASE;
        const auto footer = make_footer(data_size_, toc_, write_settings_, base_hash);
        write_bytes(out_, footer, footer.size());
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
        if(patch_base_ == nullptr)
        {
            return false;
        }
        const auto found = patch_base_->entries.find(name);
        return found != patch_base_->entries.end() && !found->second.is_deleted &&
               found->second.content_hash == content_hash && found->second.raw_size == raw_size;
    }

    /// The base's names that no input had, in name order so the patch is deterministic.
    void add_deleted_entries()
    {
        if(patch_base_ == nullptr)
        {
            return;
        }
        std::vector<std::string> deleted;
        for(const auto& [name, entry] : patch_base_->entries)
        {
            if(!entry.is_deleted && !names_.contains(name))
            {
                deleted.push_back(entry.name);
            }
        }
        std::sort(deleted.begin(), deleted.end());
        for(const auto& name : deleted)
        {
            append_deleted_record(toc_, name);
            ++entry_count_;
            ++stats_.deleted_count;
        }
    }

    void write_padding()
    {
        static const byte_array_t padding(PACK_ENTRY_ALIGNMENT, 0);
        const std::uint64_t size = (PACK_ENTRY_ALIGNMENT - data_size_ % PACK_ENTRY_ALIGNMENT) % PACK_ENTRY_ALIGNMENT;
        write_bytes(out_, padding, std::size_t(size));
        data_size_ += size;
    }

    std::ofstream& out_;
    entry_encoder encoder_;
    std::uint32_t write_settings_{};
    const mounted_pack* patch_base_{};
    byte_array_t toc_;
    std::unordered_set<std::string> names_;
    std::uint64_t data_size_{};
    std::uint32_t entry_count_{};
    pack_write_stats stats_;
};

/// Writes the whole pack into `file`.
auto write_pack_file(const path& file,
                     const std::vector<pack_input>& inputs,
                     const pack_write_options& options,
                     pack_write_stats& totals,
                     std::string& err) -> bool
{
    std::unique_ptr<mounted_pack> patch_base;
    if(!options.patch_base.empty())
    {
        patch_base = std::make_unique<mounted_pack>();
        if(!open_pack_file(options.patch_base, *patch_base, err))
        {
            err = "patch base " + options.patch_base.generic_string() + ": " + err;
            return false;
        }
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if(!out.good())
    {
        err = "cannot write " + file.generic_string();
        return false;
    }
    pack_writer writer(out, options, patch_base.get());
    for(const auto& input : inputs)
    {
        if(!writer.add(input, err))
        {
            return false;
        }
    }
    if(!writer.finish(file, err))
    {
        return false;
    }
    totals = writer.get_stats();
    return true;
}

/// Whether a pack whose table of contents hashes to `toc_hash` is mounted for `protocol`.
/// The caller holds mounts_mutex.
auto is_mounted_unlocked(const std::string& protocol, std::uint64_t toc_hash) -> bool
{
    return std::any_of(mounted_packs.begin(),
                       mounted_packs.end(),
                       [&](const std::shared_ptr<const mounted_pack>& pack)
                       {
                           return pack->protocol == protocol && pack->toc_hash == toc_hash;
                       });
}

/// The bytes of a found, not deleted entry.
auto read_found_entry(const entry_lookup& found) -> packed_file
{
    const auto& entry = *found.entry;
    if(entry.codec == PACK_STORED_CODEC)
    {
        // A view of the mapping: the aliasing pointer keeps the pack mapped while it is used.
        const auto* data = found.pack->file.data() + entry.offset;
        return {data, std::size_t(entry.raw_size), std::shared_ptr<const void>(found.pack, data)};
    }
    auto decoded = decode_entry(*found.pack, entry);
    if(!decoded)
    {
        return {};
    }
    return {decoded->data(), decoded->size(), decoded};
}

} // namespace

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
    if(!write_pack_file(temp, inputs, options, totals, err))
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

auto mount_pack(const std::string& protocol, const path& pack_file, std::string& err) -> bool
{
    auto pack = std::make_shared<mounted_pack>();
    pack->protocol = protocol;
    if(!open_pack_file(pack_file, *pack, err))
    {
        err = pack_file.generic_string() + ": " + err;
        return false;
    }
    std::unique_lock lock(mounts_mutex);
    // Over any other base its additions and deletions would mix two builds.
    if(pack->base_hash != PACK_NO_BASE && !is_mounted_unlocked(protocol, pack->base_hash))
    {
        err = pack_file.generic_string() + ": a patch for another build (the pack it was made against is not mounted)";
        return false;
    }
    mounted_packs.push_back(std::move(pack));
    return true;
}

void unmount_packs()
{
    std::unique_lock lock(mounts_mutex);
    mounted_packs.clear();
}

auto has_mounted_packs() -> bool
{
    std::shared_lock lock(mounts_mutex);
    return !mounted_packs.empty();
}

auto find_packed_file(const path& file) -> packed_file
{
    const auto found = find_entry(file);
    if(found.entry == nullptr || found.entry->is_deleted)
    {
        return {};
    }
    return read_found_entry(found);
}

auto is_readable_file(const path& file) -> bool
{
    const auto found = find_entry(file);
    if(found.entry != nullptr)
    {
        return !found.entry->is_deleted;
    }
    error_code ec;
    return exists(file, ec) && !is_directory(file, ec);
}

auto open_readable_stream(const path& file) -> std::unique_ptr<std::istream>
{
    const auto found = find_entry(file);
    if(found.entry != nullptr)
    {
        // A deleted entry hides the disk as well as the packs under it.
        auto packed = found.entry->is_deleted ? packed_file{} : read_found_entry(found);
        return packed.is_found() ? std::make_unique<packed_istream>(std::move(packed)) : nullptr;
    }
    auto stream = std::make_unique<file_istream>(file, std::ios::binary);
    if(!stream->is_open())
    {
        return nullptr;
    }
    return stream;
}

} // namespace fs
