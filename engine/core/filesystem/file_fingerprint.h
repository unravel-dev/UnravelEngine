#pragma once

#include "filesystem.h"
#include <cstdint>
#include <string>

namespace fs
{

/// xxHash3 128-bit fingerprints (hex). Bump when the hashing or dependency-ordering scheme changes.
/// v2: the source's .meta sidecar (compile parameters: color space, compression, ...) joined the
/// combined fingerprint, so meta-only changes invalidate compiled outputs at startup.
inline constexpr uint64_t current_source_fingerprint_version = 2;

/// Hash a file's contents. Text assets normalize CRLF to LF before hashing.
auto hash_file_fingerprint(const path& file_path, bool normalize_text_line_endings) -> std::string;

/// The fingerprint of `size` bytes at `data`, as hash_file_fingerprint gives a file of those bytes.
auto hash_bytes_fingerprint(const void* data, size_t size) -> std::string;

/// A 32-bit check of `size` bytes at `data` (XXH32, seed 0): what a script that has only 32-bit arithmetic, such as
/// a web page's, computes fast to tell a damaged or foreign download from the bytes it expects.
auto hash_bytes_check(const void* data, size_t size) -> uint32_t;

/// Combine per-file fingerprints (raw 128-bit digests) into one hex fingerprint.
auto combine_file_fingerprints(const std::string& first_fingerprint,
                               const std::string* additional_fingerprints,
                               size_t additional_count) -> std::string;

} // namespace fs
