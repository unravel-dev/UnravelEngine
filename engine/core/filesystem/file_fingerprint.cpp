#include "file_fingerprint.h"
#include "mio.hpp"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <array>
#include <fstream>
#include <vector>

namespace fs
{
namespace
{

auto xxh128_to_hex(const XXH128_hash_t& hash) -> std::string
{
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string hex(32, '\0');
    const std::array<uint64_t, 2> parts = {hash.high64, hash.low64};
    size_t offset = 0;
    for(const auto part : parts)
    {
        for(int shift = 60; shift >= 0; shift -= 4)
        {
            hex[offset++] = alphabet[(part >> shift) & 0xF];
        }
    }
    return hex;
}

auto hex_to_xxh128(const std::string& hex) -> XXH128_hash_t
{
    XXH128_hash_t hash{};
    if(hex.size() != 32)
    {
        return hash;
    }
    auto parse_nibble = [](char c) -> uint64_t
    {
        if(c >= '0' && c <= '9')
        {
            return static_cast<uint64_t>(c - '0');
        }
        if(c >= 'a' && c <= 'f')
        {
            return static_cast<uint64_t>(10 + c - 'a');
        }
        if(c >= 'A' && c <= 'F')
        {
            return static_cast<uint64_t>(10 + c - 'A');
        }
        return 0;
    };
    for(size_t i = 0; i < 16; ++i)
    {
        hash.high64 = (hash.high64 << 4) | parse_nibble(hex[i]);
    }
    for(size_t i = 16; i < 32; ++i)
    {
        hash.low64 = (hash.low64 << 4) | parse_nibble(hex[i]);
    }
    return hash;
}

void xxh3_update_text_normalized(XXH3_state_t& state,
                                 const unsigned char* data,
                                 size_t size,
                                 bool& last_was_cr)
{
    auto feed = [&](const unsigned char* ptr, size_t len)
    {
        if(len > 0)
        {
            XXH3_128bits_update(&state, ptr, len);
        }
    };
    auto feed_lf = [&]()
    {
        static const unsigned char lf = '\n';
        feed(&lf, 1);
    };
    size_t index = 0;
    while(index < size)
    {
        if(last_was_cr)
        {
            const unsigned char c = data[index++];
            if(c == '\n')
            {
                feed_lf();
                last_was_cr = false;
            }
            else
            {
                feed_lf();
                last_was_cr = false;
                if(c == '\r')
                {
                    last_was_cr = true;
                }
                else if(c == '\n')
                {
                    feed_lf();
                }
                else
                {
                    feed(&c, 1);
                }
            }
            continue;
        }
        const size_t start = index;
        while(index < size && data[index] != '\r' && data[index] != '\n')
        {
            ++index;
        }
        if(index > start)
        {
            feed(data + start, index - start);
        }
        if(index >= size)
        {
            break;
        }
        const unsigned char c = data[index++];
        if(c == '\r')
        {
            last_was_cr = true;
        }
        else
        {
            feed_lf();
        }
    }
}

auto hash_mapped_fingerprint(const char* data, size_t size, bool normalize_text_line_endings) -> std::string
{
    if(normalize_text_line_endings)
    {
        XXH3_state_t state;
        XXH3_128bits_reset(&state);
        bool last_was_cr = false;
        xxh3_update_text_normalized(state, reinterpret_cast<const unsigned char*>(data), size, last_was_cr);
        if(last_was_cr)
        {
            static const unsigned char lf = '\n';
            XXH3_128bits_update(&state, &lf, 1);
        }
        return xxh128_to_hex(XXH3_128bits_digest(&state));
    }
    return xxh128_to_hex(XXH3_128bits(data, size));
}

auto hash_stream_fingerprint(std::ifstream& file, bool normalize_text_line_endings) -> std::string
{
    std::vector<char> buffer(64 * 1024);
    if(normalize_text_line_endings)
    {
        XXH3_state_t state;
        XXH3_128bits_reset(&state);
        bool last_was_cr = false;
        while(file.good())
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize bytes_read = file.gcount();
            if(bytes_read <= 0)
            {
                break;
            }
            xxh3_update_text_normalized(state,
                                        reinterpret_cast<const unsigned char*>(buffer.data()),
                                        static_cast<size_t>(bytes_read),
                                        last_was_cr);
        }
        if(last_was_cr)
        {
            static const unsigned char lf = '\n';
            XXH3_128bits_update(&state, &lf, 1);
        }
        return xxh128_to_hex(XXH3_128bits_digest(&state));
    }
    XXH3_state_t state;
    XXH3_128bits_reset(&state);
    while(file.good())
    {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytes_read = file.gcount();
        if(bytes_read <= 0)
        {
            break;
        }
        XXH3_128bits_update(&state, buffer.data(), static_cast<size_t>(bytes_read));
    }
    return xxh128_to_hex(XXH3_128bits_digest(&state));
}

} // namespace

auto hash_file_fingerprint(const path& file_path, bool normalize_text_line_endings) -> std::string
{
    std::error_code error;
    mio::mmap_source mapped = mio::make_mmap_source(file_path.string(), error);
    if(!error && mapped.is_mapped())
    {
        return hash_mapped_fingerprint(mapped.data(), mapped.size(), normalize_text_line_endings);
    }
    std::ifstream file(file_path, std::ios::binary);
    if(!file.is_open())
    {
        return {};
    }
    return hash_stream_fingerprint(file, normalize_text_line_endings);
}

auto combine_file_fingerprints(const std::string& first_fingerprint,
                               const std::string* additional_fingerprints,
                               size_t additional_count) -> std::string
{
    XXH3_state_t state;
    XXH3_128bits_reset(&state);
    if(!first_fingerprint.empty())
    {
        const XXH128_hash_t first_hash = hex_to_xxh128(first_fingerprint);
        XXH3_128bits_update(&state, &first_hash, sizeof(first_hash));
    }
    for(size_t i = 0; i < additional_count; ++i)
    {
        const XXH128_hash_t hash = hex_to_xxh128(additional_fingerprints[i]);
        XXH3_128bits_update(&state, &hash, sizeof(hash));
    }
    return xxh128_to_hex(XXH3_128bits_digest(&state));
}

} // namespace fs
