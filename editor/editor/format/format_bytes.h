#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace unravel
{

/// Format a byte count as a human-readable kibibyte string (B, KiB, MiB, GiB, ...).
/// Uses @ref bx::Units::KibiByte via bx::toHuman / bx::prettify.
/// @param num_frac Fraction digits for scaled values (0 selects bx::prettify).
auto format_bytes(std::uint64_t bytes, std::uint8_t num_frac = 2) -> std::string;

/// Signed overload; negative values are clamped to zero.
auto format_bytes(std::int64_t bytes, std::uint8_t num_frac = 2) -> std::string;

/// Write a formatted byte string into @p out (NUL-terminated). No-op if @p out or @p out_size is zero.
void format_bytes(std::uint64_t bytes, char* out, std::size_t out_size, std::uint8_t num_frac = 2);

} // namespace unravel
