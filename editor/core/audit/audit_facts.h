#pragma once

#include "audit_input.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace unravel::audit
{

/**
 * @brief Facts already read from compiled files, kept between runs: a file read again only when its size or time
 * changed. Any thread.
 */
class facts_cache
{
public:
    /// The facts of a compiled file, when the cache holds them for its current size and time.
    auto find(const fs::path& file, std::uint64_t size, std::int64_t time) const -> hpp::optional<asset_facts>;
    void store(const fs::path& file, std::uint64_t size, std::int64_t time, const asset_facts& facts);
    void clear();

private:
    struct entry
    {
        std::uint64_t size{};
        std::int64_t time{};
        asset_facts facts;
    };
    mutable std::mutex mutex_;
    std::unordered_map<std::string, entry> entries_;
};

/**
 * @brief Reads what the rules need from an asset's compiled file: a texture's container header and a normal map's
 * green direction, a clip decoded as the game decodes it, a mesh's counts. Nothing for other assets or an asset not
 * compiled yet. Any thread.
 */
auto read_asset_facts(const asset_record& asset, facts_cache* cache) -> asset_facts;

/// "4.0 MB", "512 KB", "300 bytes": sizes as the editor shows them, in units of 1024.
auto format_bytes(std::uint64_t bytes) -> std::string;

} // namespace unravel::audit
