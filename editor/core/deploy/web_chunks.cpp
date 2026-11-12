#include "web_chunks.h"
#include "files.h"

#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/file_fingerprint.h>
#include <filesystem/mapped_file.h>
#include <filesystem/pack.h>
#include <filesystem/shared_file.h>
#include <logging/logging.h>

#include <stdexcept>

namespace unravel::deployment
{
namespace
{
/// What a chunk on its way into place is named with, beside its final name.
constexpr const char* CHUNK_TEMP_SUFFIX = ".tmp";

/// The cut rule: a chunk may start at half of all units, picked by their keys alone.
auto is_cut_point(std::uint64_t key) -> bool
{
    return (key & 1) == 0;
}

/// Collects the cuts of one file, each after the last.
class chunk_cutter
{
public:
    explicit chunk_cutter(std::uint64_t file_size) : file_size_(file_size)
    {
    }

    /// The bytes the open chunk holds up to `offset`.
    auto get_size_at(std::uint64_t offset) const -> std::uint64_t
    {
        return offset - start_;
    }

    /// Closes the open chunk at `offset`, unless it would be empty or `offset` is the file's end.
    void cut_at(std::uint64_t offset)
    {
        if(offset > start_ && offset < file_size_)
        {
            cuts_.push_back(offset);
            start_ = offset;
        }
    }

    /// Cuts in even steps until the open chunk fits up to `end`.
    void cut_evenly_to(std::uint64_t end)
    {
        while(get_size_at(end) > WEB_CHUNK_MAX_SIZE)
        {
            cut_at(start_ + WEB_CHUNK_MAX_SIZE);
        }
    }

    /// Takes a unit into the chunks, whole unless it is larger than a chunk may be.
    void add(const web_chunk_unit& unit)
    {
        const auto end = unit.offset + unit.size;
        cut_evenly_to(unit.offset);
        if(unit.size > WEB_CHUNK_MAX_SIZE)
        {
            cut_at(unit.offset);
            cut_evenly_to(end);
            cut_at(end);
            return;
        }
        const bool is_picked = get_size_at(unit.offset) >= WEB_CHUNK_MIN_SIZE && is_cut_point(unit.key);
        if(unit.starts_chunk || is_picked || get_size_at(end) > WEB_CHUNK_MAX_SIZE)
        {
            cut_at(unit.offset);
        }
    }

    auto finish() -> std::vector<std::uint64_t>
    {
        cut_evenly_to(file_size_);
        return std::move(cuts_);
    }

private:
    std::uint64_t file_size_{};
    /// Where the open chunk starts.
    std::uint64_t start_{};
    std::vector<std::uint64_t> cuts_;
};

auto is_pack_file(const fs::path& file) -> bool
{
    return file.extension() == fs::path(ex::get_pack_file_name()).extension();
}

/// Writes a chunk's bytes under its name, beside the name first so a write that stops half way leaves no chunk.
void write_chunk_file(const fs::path& file, const std::uint8_t* data, std::uint64_t size)
{
    fs::error_code ec;
    if(fs::file_size(file, ec) == size && !ec)
    {
        return;
    }
    auto temp = file;
    temp += CHUNK_TEMP_SUFFIX;
    if(!write_file(temp, data, std::size_t(size)))
    {
        throw std::runtime_error(fmt::format("Writing {} failed.", temp.generic_string()));
    }
    fs::rename_over(temp, file, ec);
    if(ec)
    {
        throw std::runtime_error(fmt::format("Writing {} failed: {}", file.generic_string(), ec.message()));
    }
}
} // namespace

auto find_web_chunk_cuts(std::uint64_t file_size, const std::vector<web_chunk_unit>& units)
    -> std::vector<std::uint64_t>
{
    chunk_cutter cutter(file_size);
    for(const auto& unit : units)
    {
        cutter.add(unit);
    }
    return cutter.finish();
}

auto find_web_chunk_units(const fs::path& file) -> std::vector<web_chunk_unit>
{
    fs::pack_layout layout;
    std::string error;
    if(!is_pack_file(file) || !fs::read_pack_layout(file, layout, error))
    {
        return {};
    }
    std::vector<web_chunk_unit> units;
    units.reserve(layout.entries.size() + 1);
    for(const auto& entry : layout.entries)
    {
        units.push_back({entry.offset, entry.size, fs::hash_bytes_check(entry.name.data(), entry.name.size()), false});
    }
    units.push_back({layout.contents_offset, layout.file_size - layout.contents_offset, 0, true});
    return units;
}

auto write_web_chunks(const fs::path& file, const fs::path& chunks_folder) -> std::vector<web_chunk>
{
    fs::error_code ec;
    const auto file_size = fs::file_size(file, ec);
    if(ec)
    {
        throw std::runtime_error(
            fmt::format("{} could not be read for the web build's chunks.", file.generic_string()));
    }
    if(file_size == 0)
    {
        return {};
    }
    fs::mapped_file mapped;
    std::string error;
    if(!mapped.open(file, error))
    {
        throw std::runtime_error(
            fmt::format("{} could not be read for the web build's chunks: {}", file.generic_string(), error));
    }
    auto ends = find_web_chunk_cuts(mapped.size(), find_web_chunk_units(file));
    ends.push_back(mapped.size());
    fs::create_directories(chunks_folder, ec);
    std::vector<web_chunk> chunks;
    chunks.reserve(ends.size());
    std::uint64_t start = 0;
    for(const auto end : ends)
    {
        const auto* data = mapped.data() + start;
        const auto size = end - start;
        web_chunk chunk{fs::hash_bytes_fingerprint(data, std::size_t(size)) + WEB_CHUNK_EXTENSION,
                        size,
                        fs::hash_bytes_check(data, std::size_t(size))};
        write_chunk_file(chunks_folder / chunk.name, data, size);
        chunks.push_back(std::move(chunk));
        start = end;
    }
    return chunks;
}

} // namespace unravel::deployment
