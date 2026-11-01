#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @file gi_hash.h
 * @brief The FNV-1a parameters the GI's change fingerprints and debug colours hash with. Depends on nothing, so the
 *        translation units validated without the engine can use it.
 */

namespace unravel::gi_hash
{

inline constexpr uint64_t FNV64_OFFSET_BASIS = 0xcbf29ce484222325ull;
inline constexpr uint64_t FNV64_PRIME = 0x100000001b3ull;
inline constexpr uint32_t FNV32_OFFSET_BASIS = 2166136261u;
inline constexpr uint32_t FNV32_PRIME = 16777619u;

/// FNV-1a over @p size bytes at @p data.
inline auto fnv1a_32(const void* data, size_t size) -> uint32_t
{
    uint32_t hash = FNV32_OFFSET_BASIS;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for(size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ bytes[i]) * FNV32_PRIME;
    }
    return hash;
}

} // namespace unravel::gi_hash
