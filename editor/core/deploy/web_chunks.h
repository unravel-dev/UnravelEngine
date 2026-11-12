#pragma once

#include <filesystem/filesystem.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief A web build's data as chunks.
 *
 * A page downloads the game's data in pieces of a few megabytes, each a file named by its content: a host's limit
 * on file sizes does not meet a large pack, a download that drops costs one piece, a piece is cached for good under
 * its name, and an update downloads only the pieces that changed. For that last one a pack is cut between its
 * entries, at places only the entries around a cut decide, so an entry that changes moves the cuts beside it and
 * no others.
 */
namespace unravel::deployment
{

/// The folder of a web build's site that holds its chunks.
constexpr const char* WEB_CHUNKS_FOLDER = "chunks";
/// How a chunk's file name ends.
constexpr const char* WEB_CHUNK_EXTENSION = ".bin";
/// A chunk of whole pack entries is closed at the first entry the cut rule picks once it holds this much.
constexpr std::uint64_t WEB_CHUNK_MIN_SIZE = 2ULL * 1024 * 1024;
/// No chunk is larger: under the file size limit of every common static host.
constexpr std::uint64_t WEB_CHUNK_MAX_SIZE = 8ULL * 1024 * 1024;

/// A piece of a data file, which a page downloads on its own.
struct web_chunk
{
    /// Its file in the chunks folder: the fingerprint of its bytes and WEB_CHUNK_EXTENSION.
    std::string name;
    std::uint64_t size{};
    /// fs::hash_bytes_check of its bytes, which the page checks a download against.
    std::uint32_t check{};
};

/// A part of a file that a cut leaves whole: an entry of a pack.
struct web_chunk_unit
{
    std::uint64_t offset{};
    std::uint64_t size{};
    /// Decides, with the unit's place alone, whether a chunk may start at it: a hash of the entry's name that is
    /// the same on every computer, so a build deployed from another one cuts the same.
    std::uint64_t key{};
    /// A chunk always starts here: a pack's table of contents, which changes whenever an entry does.
    bool starts_chunk{};
};

/**
 * @brief Where a file of `file_size` bytes is cut into chunks: the offsets the chunks after the first start at, in
 * rising order.
 *
 * A chunk holds whole units and is closed before the first unit whose key the cut rule picks once the chunk holds
 * WEB_CHUNK_MIN_SIZE, before a unit that starts a chunk, and before a unit that would take it past
 * WEB_CHUNK_MAX_SIZE. A unit larger than that is cut on its own, in even steps from its start. Bytes outside every
 * unit are cut in even steps too. No chunk is larger than WEB_CHUNK_MAX_SIZE.
 * @param units In rising order of their offsets, apart from each other.
 */
auto find_web_chunk_cuts(std::uint64_t file_size, const std::vector<web_chunk_unit>& units)
    -> std::vector<std::uint64_t>;

/// The units of `file`: the entries and the table of contents of a pack; none for any other file, which is cut in
/// even steps.
auto find_web_chunk_units(const fs::path& file) -> std::vector<web_chunk_unit>;

/**
 * @brief Cuts `file` into chunks and writes them into `chunks_folder`. A chunk already there under its name is the
 * same bytes and is left alone.
 * @return The chunks in the file's order; none for an empty file. Throws when the file cannot be read or a chunk
 * cannot be written.
 */
auto write_web_chunks(const fs::path& file, const fs::path& chunks_folder) -> std::vector<web_chunk>;

} // namespace unravel::deployment
