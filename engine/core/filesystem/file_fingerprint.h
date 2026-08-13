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

/// Combine per-file fingerprints (raw 128-bit digests) into one hex fingerprint.
auto combine_file_fingerprints(const std::string& first_fingerprint,
                               const std::string* additional_fingerprints,
                               size_t additional_count) -> std::string;

} // namespace fs
