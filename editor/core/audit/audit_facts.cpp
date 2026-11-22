#include "audit_facts.h"

#include <audiopp/sound_data.h>
#include <bimg/bimg.h>
#include <engine/audio/audio_clip.h>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <graphics/utils/bgfx_utils.h>
#include <logging/logging.h>

#include <chrono>
#include <exception>
#include <fstream>
#include <vector>

namespace unravel::audit
{
namespace
{

/// Bytes read from a compiled texture before parsing its header: every container keeps its header and level
/// index in its first bytes.
constexpr std::size_t TEXTURE_HEADER_READ_BYTES = 64 * 1024;

auto read_file_head(const fs::path& file, std::size_t max_bytes, std::vector<char>& out) -> bool
{
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        return false;
    }
    out.resize(max_bytes);
    stream.read(out.data(), static_cast<std::streamsize>(max_bytes));
    out.resize(static_cast<std::size_t>(stream.gcount()));
    return !out.empty();
}

auto parse_texture_header(const std::vector<char>& bytes, bimg::ImageContainer& info) -> bool
{
    // A container whose levels run past the bytes read fails to parse: the caller then reads the whole file. The
    // error is the caller's to handle, so it is collected here rather than asserted on.
    bx::Error error;
    return ::imageParseInfo(bytes.data(), static_cast<std::uint32_t>(bytes.size()), info, &error) && error.isOk();
}

auto read_texture_facts(const fs::path& file) -> hpp::optional<texture_facts>
{
    std::vector<char> bytes;
    if(!read_file_head(file, TEXTURE_HEADER_READ_BYTES, bytes))
    {
        return hpp::nullopt;
    }
    bimg::ImageContainer info{};
    if(!parse_texture_header(bytes, info))
    {
        // A container whose header runs past the first bytes: read it whole.
        fs::error_code ec;
        const auto size = fs::file_size(file, ec);
        if(ec || size <= bytes.size() || !read_file_head(file, static_cast<std::size_t>(size), bytes) ||
           !parse_texture_header(bytes, info))
        {
            return hpp::nullopt;
        }
    }
    texture_facts facts;
    facts.width = info.m_width;
    facts.height = info.m_height;
    facts.mips = info.m_numMips;
    facts.format = bimg::getName(info.m_format);
    facts.is_compressed = bimg::isCompressed(info.m_format);
    facts.bits_per_pixel = bimg::getBitsPerPixel(info.m_format);
    return facts;
}

auto read_audio_facts(const fs::path& file) -> hpp::optional<audio_facts>
{
    encoded_sound encoded;
    audio::sound_data data;
    std::string error;
    try
    {
        if(!load_from_file_bin(file.string(), encoded) || !decode_sound(encoded, data, error))
        {
            return hpp::nullopt;
        }
    }
    catch(const std::exception&)
    {
        return hpp::nullopt;
    }
    audio_facts facts;
    facts.seconds = std::chrono::duration<double>(data.info.duration).count();
    facts.sample_rate = data.info.sample_rate;
    facts.channels = data.info.channels;
    facts.decoded_bytes = data.data.size();
    return facts;
}

auto read_mesh_facts(const fs::path& file) -> hpp::optional<mesh_facts>
{
    mesh::load_data data;
    try
    {
        load_from_file_bin(file.string(), data);
    }
    catch(const std::exception&)
    {
        return hpp::nullopt;
    }
    if(data.vertex_count == 0)
    {
        return hpp::nullopt;
    }
    mesh_facts facts;
    facts.vertices = data.vertex_count;
    facts.triangles = data.triangle_count;
    facts.submeshes = static_cast<std::uint32_t>(data.submeshes.size());
    facts.lods = static_cast<std::uint32_t>(data.lods.size());
    facts.morph_targets = static_cast<std::uint32_t>(data.morph_targets.targets.size());
    facts.morphed_vertices = static_cast<std::uint32_t>(data.morph_targets.morphed_vertices.size());
    facts.morph_bytes = data.morph_targets.deltas.size() * sizeof(morph_delta) +
                        data.morph_targets.morphed_vertices.size() * sizeof(std::uint32_t);
    return facts;
}

auto read_facts_of(const asset_record& asset, const fs::path& file) -> asset_facts
{
    asset_facts facts;
    if(!asset.importer)
    {
        return facts;
    }
    if(asset.importer->is<texture_importer_meta>())
    {
        facts.texture = read_texture_facts(file);
    }
    else if(asset.importer->is<audio_importer_meta>())
    {
        facts.audio = read_audio_facts(file);
    }
    else if(asset.importer->is<mesh_importer_meta>())
    {
        facts.mesh = read_mesh_facts(file);
    }
    return facts;
}

auto get_write_time(const fs::path& file) -> std::int64_t
{
    fs::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    return ec ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
}

} // namespace

auto facts_cache::find(const fs::path& file, std::uint64_t size, std::int64_t time) const -> hpp::optional<asset_facts>
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = entries_.find(file.generic_string());
    if(found == entries_.end() || found->second.size != size || found->second.time != time)
    {
        return hpp::nullopt;
    }
    return found->second.facts;
}

void facts_cache::store(const fs::path& file, std::uint64_t size, std::int64_t time, const asset_facts& facts)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    entries_[file.generic_string()] = {size, time, facts};
}

void facts_cache::clear()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

auto read_asset_facts(const asset_record& asset, facts_cache* cache) -> asset_facts
{
    if(asset.compiled_files.empty() || !asset.importer)
    {
        return {};
    }
    const auto& file = asset.compiled_files.front();
    fs::error_code ec;
    const auto size = fs::file_size(file, ec);
    if(ec)
    {
        return {};
    }
    const auto time = get_write_time(file);
    if(cache != nullptr)
    {
        if(auto cached = cache->find(file, size, time))
        {
            return *cached;
        }
    }
    auto facts = read_facts_of(asset, file);
    if(cache != nullptr)
    {
        cache->store(file, size, time, facts);
    }
    return facts;
}

auto format_bytes(std::uint64_t bytes) -> std::string
{
    constexpr double KIB = 1024.0;
    constexpr double MIB = 1024.0 * 1024.0;
    constexpr double GIB = 1024.0 * 1024.0 * 1024.0;
    const auto value = static_cast<double>(bytes);
    if(value >= GIB)
    {
        return fmt::format("{:.1f} GB", value / GIB);
    }
    if(value >= MIB)
    {
        return fmt::format("{:.1f} MB", value / MIB);
    }
    if(value >= KIB)
    {
        return fmt::format("{:.0f} KB", value / KIB);
    }
    return fmt::format("{} bytes", bytes);
}

} // namespace unravel::audit
