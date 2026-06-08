#pragma once

#include "filesystem.h"
#include <iosfwd>
#include <istream>
#include <memory>
#include <string>

namespace fs
{

/// Recursively collects regular files under `directory` and benchmarks three read
/// backends (FILE*, file_istream, std::ifstream, mapped_file_reader) in two modes:
///   - chunked: 64 KiB reads with a simple uint64_t byte sum per chunk
///   - whole file: one read / one mapped scan over the entire file
/// Each backend runs a cold pass (best-effort OS page cache drop beforehand) and a hot
/// pass without cache drops. Results are logged via APPLOG_INFO.
void benchmark_directory_reads(const path& directory);


} // namespace fs
