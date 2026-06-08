#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ios>

namespace fs
{

/// Whether a file stream wrapper should fclose its FILE* on destruction.
enum class file_stream_ownership : std::uint8_t
{
    take_ownership,
    retain_ownership,
};

inline constexpr std::ios_base::openmode default_file_read_mode =
    std::ios_base::in | std::ios_base::binary;

inline constexpr std::ios_base::openmode default_file_write_mode =
    std::ios_base::out | std::ios_base::binary;

namespace file_stream_detail
{

constexpr std::size_t default_buffer_size = 64 * 1024;

auto stdio_tell64(FILE* file) -> long long;
auto stdio_seek64(FILE* file, long long offset, int origin) -> int;
auto stdio_file_size(FILE* file) -> long long;

/// Maps std::ios open flags to a stdio mode string (e.g. "rb", "w+b"). Returns false for invalid
/// combinations (neither in nor out).
auto openmode_to_stdio_mode(std::ios_base::openmode mode, char (&out)[4]) -> bool;

} // namespace file_stream_detail

} // namespace fs
