/*
 * Validation suite for asset packs and the compression library under them.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "asset packs"
 *
 * A deployed game reads its compiled assets from one pack per protocol, mounted over that protocol's
 * root. This pins:
 *   - compression round trips, and a decode to the wrong size fails;
 *   - packs round-trip empty, small and multi-block files, compressed and stored, by absolute and by
 *     protocol path (and by any letter case on Windows), through find_packed_file and streams;
 *   - incompressible entries are stored whole, compressible ones shrink;
 *   - a file a pack does not hold falls back to disk; a later mount wins;
 *   - a truncated, damaged or foreign file is refused at mount;
 *   - the same inputs write the same bytes; streams seek and tell;
 *   - a write that reuses an earlier pack copies the entries whose bytes it holds (renamed ones too) and
 *     gives exactly the bytes a fresh write gives; other compression settings reuse nothing;
 *   - a patch leaves unchanged entries to its base, holds changed and added ones and deletes removed ones;
 *     it mounts only right over its base, and its deleted entries hide the base and the disk; a patch is
 *     never a patch base, and two patches of one base do not stack;
 *   - entries mixing compressed and raw blocks, and exact block multiples, read back;
 *   - found files and open streams outlive unmount_packs; reads on other threads while packs mount and
 *     unmount return the whole entry or nothing;
 *   - the parser refuses huge counts and sizes before allocating, other codecs and versions;
 *   - a pack can be replaced reusing itself; a damaged entry is reported (lookup status) and never reused;
 *   - a protocol path no pack holds is read from disk.
 * Everything lives in a temp folder under a protocol the suite registers.
 */

#include "../tests.h"

#include <compression/compression.h>
#include <filesystem/filesystem.h>
#include <filesystem/pack.h>

#define XXH_INLINE_ALL
#include <filesystem/xxhash.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

int g_checks = 0;
int g_failures = 0;

constexpr const char* TEST_PROTOCOL = "unravel-tests-packs";
constexpr std::size_t SMALL_FILE_SIZE = 100;
/// Spans several 64 KB blocks, with a short last one.
constexpr std::size_t MULTI_BLOCK_FILE_SIZE = 200 * 1024 + 123;
constexpr std::size_t INCOMPRESSIBLE_FILE_SIZE = 80 * 1024;
constexpr std::uint32_t RANDOM_SEED = 12345u;
constexpr std::streamoff SEEK_PROBE_OFFSET = 70000;
constexpr int PARALLEL_READERS = 4;
constexpr int PARALLEL_MOUNT_ROUNDS = 200;
constexpr auto PARALLEL_ROUND_TIMEOUT = std::chrono::milliseconds(200);
// The format's layout, as the tests that edit a pack by hand need it (tasks/pack_v1_plan.md).
constexpr std::size_t PACK_TEST_BLOCK_SIZE = 64 * 1024;
constexpr std::size_t PACK_TEST_FOOTER_SIZE = 48;
constexpr std::size_t PACK_TEST_FOOTER_VERSION = 8;
constexpr std::size_t PACK_TEST_FOOTER_TOC_OFFSET = 16;
constexpr std::size_t PACK_TEST_FOOTER_TOC_SIZE = 24;
constexpr std::size_t PACK_TEST_FOOTER_TOC_HASH = 32;
constexpr std::uint8_t PACK_TEST_FOREIGN_CODEC = 99;
constexpr std::uint8_t PACK_TEST_OTHER_VERSION = 9;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto get_temp_folder() -> fs::path
{
    return fs::temp_directory_path() / "unravel-tests-asset-packs";
}

auto get_protocol_root() -> fs::path
{
    return get_temp_folder() / "root";
}

auto get_protocol_path(const std::string& name) -> fs::path
{
    return fs::path(std::string(TEST_PROTOCOL) + ":/" + name);
}

/// Repetitive text: compresses well.
auto make_compressible_bytes(std::size_t size) -> fs::byte_array_t
{
    static const std::string phrase = "the compiled asset stays the same from build to build; ";
    fs::byte_array_t bytes(size);
    for(std::size_t i = 0; i < size; ++i)
    {
        bytes[i] = std::uint8_t(phrase[i % phrase.size()] + std::uint8_t(i / 4096 % 3));
    }
    return bytes;
}

/// Pseudo-random bytes: do not compress.
auto make_incompressible_bytes(std::size_t size) -> fs::byte_array_t
{
    fs::byte_array_t bytes(size);
    std::uint32_t state = RANDOM_SEED;
    for(auto& byte : bytes)
    {
        state = state * 1664525u + 1013904223u;
        byte = std::uint8_t(state >> 24);
    }
    return bytes;
}

void write_file(const fs::path& file, const fs::byte_array_t& bytes)
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

auto read_disk_file(const fs::path& file) -> fs::byte_array_t
{
    std::ifstream in(file, std::ios::binary);
    return fs::read_stream(in);
}

auto read_packed_bytes(const fs::path& file) -> fs::byte_array_t
{
    const auto packed = fs::find_packed_file(file);
    return packed.is_found() ? fs::byte_array_t(packed.data, packed.data + packed.size) : fs::byte_array_t{};
}

auto read_through_stream(const fs::path& file) -> fs::byte_array_t
{
    auto stream = fs::open_readable_stream(file);
    return stream ? fs::read_stream(*stream) : fs::byte_array_t{};
}

struct test_input
{
    std::string name;
    fs::byte_array_t bytes;
};

/// The inputs written as source files in a staging folder, and the matching pack inputs.
auto stage_inputs(const std::string& folder, const std::vector<test_input>& files) -> std::vector<fs::pack_input>
{
    std::vector<fs::pack_input> inputs;
    for(const auto& file : files)
    {
        const auto source = get_temp_folder() / folder / file.name;
        write_file(source, file.bytes);
        inputs.push_back({file.name, source});
    }
    return inputs;
}

auto make_test_files() -> std::vector<test_input>
{
    return {
        {"compiled/empty.asset", {}},
        {"compiled/small.txt.asset", make_compressible_bytes(SMALL_FILE_SIZE)},
        {"compiled/textures/multi_block.png.asset", make_compressible_bytes(MULTI_BLOCK_FILE_SIZE)},
        {"compiled/audio/noise.ogg.asset", make_incompressible_bytes(INCOMPRESSIBLE_FILE_SIZE)},
        {"assets.index", make_compressible_bytes(SMALL_FILE_SIZE)},
    };
}

void test_compression_round_trip()
{
    std::printf("test_compression_round_trip\n");
    const auto input = make_compressible_bytes(MULTI_BLOCK_FILE_SIZE);
    compression::compressor compressor(compression::level::fast);
    fs::byte_array_t packed(compressor.get_bound(input.size()));
    std::string error;
    const auto packed_size = compressor.compress(input.data(), input.size(), packed.data(), packed.size(), error);
    check(packed_size > 0 && packed_size < input.size(), "repetitive bytes compress: " + error);
    compression::decompressor decompressor;
    fs::byte_array_t actual(input.size());
    check(decompressor.decompress(packed.data(), packed_size, actual.data(), actual.size()), "the block decodes");
    check(actual == input, "the decoded block equals the input");
    fs::byte_array_t too_small(input.size() - 1);
    check(!decompressor.decompress(packed.data(), packed_size, too_small.data(), too_small.size()),
          "a decode to a smaller size fails");
    check(compression::get_codec_id() != 0, "the codec id is never the stored marker 0");
}

void test_round_trip(bool compress)
{
    std::printf("test_round_trip (%s)\n", compress ? "compressed" : "stored");
    fs::unmount_packs();
    const auto files = make_test_files();
    const auto inputs = stage_inputs(compress ? "stage_compressed" : "stage_stored", files);
    const auto pack_file = get_temp_folder() / (compress ? "compressed.pak" : "stored.pak");
    fs::pack_write_options options;
    options.compress = compress;
    std::string error;
    fs::pack_write_stats stats;
    check(fs::write_pack(pack_file, inputs, options, error, &stats), "the pack is written: " + error);
    check(stats.entry_count == files.size(), "every input becomes an entry");
    if(compress)
    {
        check(stats.stored_bytes < stats.raw_bytes, "compressible entries shrink");
    }
    else
    {
        check(stats.stored_bytes == stats.raw_bytes, "a stored pack keeps every byte");
    }
    check(fs::mount_pack(TEST_PROTOCOL, pack_file, error), "the pack mounts: " + error);
    for(const auto& file : files)
    {
        const auto absolute = get_protocol_root() / file.name;
        check(read_packed_bytes(absolute) == file.bytes, file.name + " reads back by absolute path");
        check(read_through_stream(get_protocol_path(file.name)) == file.bytes,
              file.name + " reads back through a stream by protocol path");
        check(fs::is_readable_file(absolute), file.name + " is readable");
    }
#ifdef _WIN32
    check(read_packed_bytes(get_protocol_root() / "COMPILED/Small.TXT.asset") == files[1].bytes,
          "names compare in any letter case on Windows");
#endif
    check(!fs::find_packed_file(get_protocol_root() / "compiled/missing.asset").is_found(),
          "a name the pack does not hold is not found");
    fs::unmount_packs();
}

void test_disk_fallback_and_later_mount()
{
    std::printf("test_disk_fallback_and_later_mount\n");
    fs::unmount_packs();
    const auto loose_bytes = make_compressible_bytes(SMALL_FILE_SIZE);
    const auto loose_file = get_protocol_root() / "compiled/loose_only.asset";
    write_file(loose_file, loose_bytes);
    const auto first = stage_inputs("stage_first", {{"compiled/shared.asset", make_compressible_bytes(SMALL_FILE_SIZE)}});
    const auto second_bytes = make_incompressible_bytes(SMALL_FILE_SIZE);
    const auto second = stage_inputs("stage_second", {{"compiled/shared.asset", second_bytes}});
    std::string error;
    check(fs::write_pack(get_temp_folder() / "first.pak", first, {}, error), "the first pack is written");
    check(fs::write_pack(get_temp_folder() / "second.pak", second, {}, error), "the second pack is written");
    check(fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "first.pak", error), "the first pack mounts");
    check(fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "second.pak", error), "the second pack mounts");
    check(read_packed_bytes(get_protocol_path("compiled/shared.asset")) == second_bytes,
          "the later mount wins for a name both hold");
    check(!fs::find_packed_file(loose_file).is_found() && read_through_stream(loose_file) == loose_bytes,
          "a file no pack holds is read from disk");
    check(!fs::is_readable_file(get_protocol_root() / "compiled/nowhere.asset") &&
              fs::open_readable_stream(get_protocol_root() / "compiled/nowhere.asset") == nullptr,
          "a file neither a pack nor the disk holds is not readable");
    fs::unmount_packs();
    check(!fs::has_mounted_packs(), "unmount_packs leaves no pack mounted");
}

void test_damaged_packs_are_refused()
{
    std::printf("test_damaged_packs_are_refused\n");
    fs::unmount_packs();
    const auto inputs = stage_inputs("stage_damage", make_test_files());
    const auto pack_file = get_temp_folder() / "intact.pak";
    std::string error;
    check(fs::write_pack(pack_file, inputs, {}, error), "the pack to damage is written");
    const auto intact = read_disk_file(pack_file);

    auto damaged_toc = intact;
    constexpr std::size_t footer_and_some_toc = 60;
    damaged_toc[damaged_toc.size() - footer_and_some_toc] ^= 0xFF;
    write_file(get_temp_folder() / "damaged_toc.pak", damaged_toc);
    error.clear();
    check(!fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "damaged_toc.pak", error), "a damaged index is refused");
    check(error.find("damaged") != std::string::npos, "the error says the pack is damaged");

    auto truncated = intact;
    truncated.resize(truncated.size() / 2);
    write_file(get_temp_folder() / "truncated.pak", truncated);
    check(!fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "truncated.pak", error), "a truncated pack is refused");

    write_file(get_temp_folder() / "foreign.pak", make_incompressible_bytes(SMALL_FILE_SIZE));
    error.clear();
    check(!fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "foreign.pak", error), "a file that is no pack is refused");
    check(error.find("not a pack") != std::string::npos, "the error says it is not a pack");
    check(!fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "absent.pak", error), "a missing pack is refused");
    check(!fs::has_mounted_packs(), "nothing refused was mounted");
}

void test_deterministic_output()
{
    std::printf("test_deterministic_output\n");
    const auto inputs = stage_inputs("stage_determinism", make_test_files());
    std::string error;
    check(fs::write_pack(get_temp_folder() / "once.pak", inputs, {}, error), "the first write succeeds");
    check(fs::write_pack(get_temp_folder() / "twice.pak", inputs, {}, error), "the second write succeeds");
    check(read_disk_file(get_temp_folder() / "once.pak") == read_disk_file(get_temp_folder() / "twice.pak"),
          "the same inputs write the same bytes");
    auto repeated = inputs;
    repeated.push_back(inputs.front());
    check(!fs::write_pack(get_temp_folder() / "repeated.pak", repeated, {}, error), "a repeated name is refused");
}

void test_stream_seek_and_tell()
{
    std::printf("test_stream_seek_and_tell\n");
    fs::unmount_packs();
    const auto bytes = make_compressible_bytes(MULTI_BLOCK_FILE_SIZE);
    const auto inputs = stage_inputs("stage_seek", {{"compiled/seek.asset", bytes}});
    std::string error;
    check(fs::write_pack(get_temp_folder() / "seek.pak", inputs, {}, error), "the pack is written");
    check(fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "seek.pak", error), "the pack mounts");
    auto stream = fs::open_readable_stream(get_protocol_path("compiled/seek.asset"));
    check(stream != nullptr, "the entry opens as a stream");
    if(stream)
    {
        stream->seekg(0, std::ios::end);
        check(std::size_t(stream->tellg()) == bytes.size(), "seeking to the end tells the size");
        stream->seekg(SEEK_PROBE_OFFSET, std::ios::beg);
        char actual = 0;
        stream->read(&actual, 1);
        check(std::uint8_t(actual) == bytes[std::size_t(SEEK_PROBE_OFFSET)], "a seek inside a later block reads its byte");
        stream->seekg(-1, std::ios::cur);
        check(std::streamoff(stream->tellg()) == SEEK_PROBE_OFFSET, "a relative seek moves back");
    }
    stream.reset();
    fs::unmount_packs();
}

void test_reuse_of_unchanged_entries()
{
    std::printf("test_reuse_of_unchanged_entries\n");
    const auto files = make_test_files();
    const auto first_inputs = stage_inputs("stage_reuse_first", files);
    std::string error;
    const auto previous = get_temp_folder() / "previous.pak";
    check(fs::write_pack(previous, first_inputs, {}, error), "the earlier pack is written");

    // The next build: one asset changed, one renamed with the same bytes, the rest as they were.
    auto next_files = files;
    next_files[2].bytes.back() ^= 0xFF;
    next_files[1].name = "compiled/renamed.txt.asset";
    const auto next_inputs = stage_inputs("stage_reuse_next", next_files);
    fs::pack_write_options options;
    options.reuse_from = previous;
    fs::pack_write_stats stats;
    const auto reused_pack = get_temp_folder() / "reused.pak";
    check(fs::write_pack(reused_pack, next_inputs, options, error, &stats), "the next pack is written: " + error);
    check(stats.reused_count == next_files.size() - 1, "every entry but the changed one is reused, renamed ones too");
    const auto fresh_pack = get_temp_folder() / "fresh.pak";
    check(fs::write_pack(fresh_pack, next_inputs, {}, error), "the same inputs are written without reuse");
    check(read_disk_file(reused_pack) == read_disk_file(fresh_pack), "reusing entries gives the bytes a fresh write gives");
    fs::unmount_packs();
    check(fs::mount_pack(TEST_PROTOCOL, reused_pack, error), "the pack with reused entries mounts");
    for(const auto& file : next_files)
    {
        check(read_packed_bytes(get_protocol_path(file.name)) == file.bytes, file.name + " reads back from the reused pack");
    }
    fs::unmount_packs();

    fs::pack_write_options stored;
    stored.compress = false;
    stored.reuse_from = previous;
    check(fs::write_pack(get_temp_folder() / "stored_reuse.pak", next_inputs, stored, error, &stats) &&
              stats.reused_count == 0,
          "an uncompressed pack reuses nothing from a compressed one");
    fs::pack_write_options best;
    best.compression_level = compression::level::best;
    best.reuse_from = previous;
    check(fs::write_pack(get_temp_folder() / "best_reuse.pak", next_inputs, best, error, &stats) && stats.reused_count == 0,
          "another compression level reuses nothing");
    fs::pack_write_options missing;
    missing.reuse_from = get_temp_folder() / "no_such.pak";
    check(fs::write_pack(get_temp_folder() / "missing_reuse.pak", next_inputs, missing, error, &stats) &&
              stats.reused_count == 0,
          "a missing earlier pack is ignored");
}

void test_patch_over_base()
{
    std::printf("test_patch_over_base\n");
    fs::unmount_packs();
    const auto kept = make_compressible_bytes(SMALL_FILE_SIZE);
    const auto changed_before = make_compressible_bytes(MULTI_BLOCK_FILE_SIZE);
    auto changed_after = changed_before;
    changed_after.front() ^= 0xFF;
    const auto removed = make_incompressible_bytes(SMALL_FILE_SIZE);
    const auto added = make_incompressible_bytes(MULTI_BLOCK_FILE_SIZE);
    const auto base_inputs = stage_inputs("stage_patch_base",
                                          {{"compiled/kept.asset", kept},
                                           {"compiled/changed.asset", changed_before},
                                           {"compiled/removed.asset", removed}});
    const auto next_inputs = stage_inputs("stage_patch_next",
                                          {{"compiled/kept.asset", kept},
                                           {"compiled/changed.asset", changed_after},
                                           {"compiled/added.asset", added}});
    std::string error;
    const auto base = get_temp_folder() / "base.pak";
    check(fs::write_pack(base, base_inputs, {}, error), "the base pack is written");
    fs::pack_write_options patch_options;
    patch_options.patch_base = base;
    fs::pack_write_stats stats;
    const auto patch = get_temp_folder() / "patch.pak";
    check(fs::write_pack(patch, next_inputs, patch_options, error, &stats), "the patch is written: " + error);
    check(stats.unchanged_count == 1, "the unchanged entry is left to the base");
    check(stats.deleted_count == 1, "the removed entry becomes a deleted entry");
    check(stats.entry_count == 3, "the patch holds the changed, the added and the deleted entry");
    check(fs::write_pack(get_temp_folder() / "patch_again.pak", next_inputs, patch_options, error) &&
              read_disk_file(patch) == read_disk_file(get_temp_folder() / "patch_again.pak"),
          "the same inputs write the same patch");

    // A loose file under the protocol root that the patch deletes must stay hidden as well.
    write_file(get_protocol_root() / "compiled/removed.asset", removed);
    check(!fs::mount_pack(TEST_PROTOCOL, patch, error), "a patch does not mount without its base");
    check(error.find("another build") != std::string::npos, "the error says the patch is for another build");
    check(fs::mount_pack(TEST_PROTOCOL, base, error), "the base mounts");
    check(fs::mount_pack(TEST_PROTOCOL, patch, error), "the patch mounts over its base: " + error);
    check(read_packed_bytes(get_protocol_path("compiled/kept.asset")) == kept, "an unchanged entry reads from the base");
    check(read_packed_bytes(get_protocol_path("compiled/changed.asset")) == changed_after,
          "a changed entry reads from the patch");
    check(read_through_stream(get_protocol_path("compiled/added.asset")) == added, "an added entry reads from the patch");
    const auto removed_path = get_protocol_path("compiled/removed.asset");
    check(!fs::find_packed_file(removed_path).is_found(), "a deleted entry is not found");
    check(!fs::is_readable_file(get_protocol_root() / "compiled/removed.asset"),
          "a deleted entry hides the loose file on disk");
    check(fs::open_readable_stream(removed_path) == nullptr, "a deleted entry opens no stream");
    fs::unmount_packs();

    check(fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "fresh.pak", error), "an unrelated pack mounts");
    check(!fs::mount_pack(TEST_PROTOCOL, patch, error), "a patch does not mount over another base");
    fs::unmount_packs();
    fs::error_code ec;
    fs::remove(get_protocol_root() / "compiled/removed.asset", ec);
}

void test_mixed_and_exact_blocks()
{
    std::printf("test_mixed_and_exact_blocks\n");
    fs::unmount_packs();
    // One block that compresses and one that does not: the second is kept raw inside a compressed entry.
    auto mixed = make_compressible_bytes(PACK_TEST_BLOCK_SIZE);
    const auto noise = make_incompressible_bytes(PACK_TEST_BLOCK_SIZE);
    mixed.insert(mixed.end(), noise.begin(), noise.end());
    const auto exact = make_compressible_bytes(2 * PACK_TEST_BLOCK_SIZE);
    const auto inputs = stage_inputs("stage_blocks", {{"compiled/mixed.asset", mixed}, {"compiled/exact.asset", exact}});
    std::string error;
    fs::pack_write_stats stats;
    check(fs::write_pack(get_temp_folder() / "blocks.pak", inputs, {}, error, &stats), "the pack is written: " + error);
    check(stats.stored_bytes < stats.raw_bytes, "the compressible blocks shrink");
    check(fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "blocks.pak", error), "the pack mounts");
    check(read_packed_bytes(get_protocol_path("compiled/mixed.asset")) == mixed,
          "an entry mixing compressed and raw blocks reads back");
    check(read_packed_bytes(get_protocol_path("compiled/exact.asset")) == exact,
          "an entry of exactly two blocks reads back");
    fs::unmount_packs();
}

void test_lifetime_after_unmount()
{
    std::printf("test_lifetime_after_unmount\n");
    fs::unmount_packs();
    const auto compressed = make_compressible_bytes(MULTI_BLOCK_FILE_SIZE);
    const auto stored = make_incompressible_bytes(SMALL_FILE_SIZE);
    const auto inputs =
        stage_inputs("stage_lifetime", {{"compiled/compressed.asset", compressed}, {"compiled/stored.asset", stored}});
    std::string error;
    check(fs::write_pack(get_temp_folder() / "lifetime.pak", inputs, {}, error), "the pack is written");
    check(fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "lifetime.pak", error), "the pack mounts");
    const auto compressed_file = fs::find_packed_file(get_protocol_path("compiled/compressed.asset"));
    const auto stored_file = fs::find_packed_file(get_protocol_path("compiled/stored.asset"));
    auto stream = fs::open_readable_stream(get_protocol_path("compiled/stored.asset"));
    fs::unmount_packs();
    check(compressed_file.is_found() && fs::byte_array_t(compressed_file.data, compressed_file.data + compressed_file.size) ==
                                            compressed,
          "a decoded entry stays readable after unmount");
    check(stored_file.is_found() && fs::byte_array_t(stored_file.data, stored_file.data + stored_file.size) == stored,
          "a view of a stored entry keeps the pack mapped after unmount");
    check(stream != nullptr && fs::read_stream(*stream) == stored, "an open stream stays readable after unmount");
}

void test_parallel_reads_during_mounts()
{
    std::printf("test_parallel_reads_during_mounts\n");
    fs::unmount_packs();
    const auto compressed = make_compressible_bytes(MULTI_BLOCK_FILE_SIZE);
    const auto inputs = stage_inputs("stage_parallel", {{"compiled/parallel.asset", compressed}});
    std::string error;
    check(fs::write_pack(get_temp_folder() / "parallel.pak", inputs, {}, error), "the pack is written");
    std::atomic<bool> is_done{false};
    std::atomic<int> found_count{0};
    std::atomic<int> torn_count{0};
    std::vector<std::thread> readers;
    for(int i = 0; i < PARALLEL_READERS; ++i)
    {
        readers.emplace_back(
            [&]()
            {
                while(!is_done.load())
                {
                    const auto file = fs::find_packed_file(get_protocol_path("compiled/parallel.asset"));
                    if(!file.is_found())
                    {
                        continue;
                    }
                    ++found_count;
                    const bool is_whole = file.size == compressed.size() && std::memcmp(file.data, compressed.data(), file.size) == 0;
                    torn_count += is_whole ? 0 : 1;
                }
            });
    }
    for(int round = 0; round < PARALLEL_MOUNT_ROUNDS; ++round)
    {
        fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "parallel.pak", error);
        // Unmount only once a reader found the entry, so reads and unmounts overlap every round.
        const int found_before = found_count.load();
        const auto deadline = std::chrono::steady_clock::now() + PARALLEL_ROUND_TIMEOUT;
        while(found_count.load() == found_before && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        fs::unmount_packs();
    }
    is_done = true;
    for(auto& reader : readers)
    {
        reader.join();
    }
    check(found_count.load() >= PARALLEL_MOUNT_ROUNDS, "readers found the entry in every round");
    check(torn_count.load() == 0, "every read during mounts and unmounts returns the whole entry or nothing");
}

/// Reads the footer field at `offset` from the end-of-file footer.
auto read_footer_value(const fs::byte_array_t& pack, std::size_t offset) -> std::uint64_t
{
    std::uint64_t value = 0;
    std::memcpy(&value, pack.data() + pack.size() - PACK_TEST_FOOTER_SIZE + offset, sizeof(value));
    return value;
}

/// A copy of `pack` with `edit` applied to its table of contents and the footer's hash updated, so the
/// parser's own checks are what refuses it.
template<typename Edit>
auto edit_toc(fs::byte_array_t pack, const Edit& edit) -> fs::byte_array_t
{
    const auto toc_offset = std::size_t(read_footer_value(pack, PACK_TEST_FOOTER_TOC_OFFSET));
    const auto toc_size = std::size_t(read_footer_value(pack, PACK_TEST_FOOTER_TOC_SIZE));
    edit(pack.data() + toc_offset);
    const std::uint64_t hash = XXH3_64bits(pack.data() + toc_offset, toc_size);
    std::memcpy(pack.data() + pack.size() - PACK_TEST_FOOTER_SIZE + PACK_TEST_FOOTER_TOC_HASH, &hash, sizeof(hash));
    return pack;
}

auto mount_bytes(const std::string& name, const fs::byte_array_t& bytes, std::string& error) -> bool
{
    write_file(get_temp_folder() / name, bytes);
    error.clear();
    const bool is_mounted = fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / name, error);
    fs::unmount_packs();
    return is_mounted;
}

void test_parser_errors()
{
    std::printf("test_parser_errors\n");
    fs::unmount_packs();
    const auto inputs = stage_inputs("stage_parser", {{"compiled/a.asset", make_compressible_bytes(MULTI_BLOCK_FILE_SIZE)}});
    std::string error;
    check(fs::write_pack(get_temp_folder() / "parser.pak", inputs, {}, error), "the pack is written");
    const auto intact = read_disk_file(get_temp_folder() / "parser.pak");
    check(mount_bytes("parser_intact.pak", edit_toc(intact, [](std::uint8_t*) {}), error), "a rehashed, unedited pack mounts");
    const auto huge_count = edit_toc(intact,
                                     [](std::uint8_t* toc)
                                     {
                                         const std::uint32_t count = 0xFFFFFFFFu;
                                         std::memcpy(toc, &count, sizeof(count));
                                     });
    check(!mount_bytes("parser_count.pak", huge_count, error) && error.find("entry count") != std::string::npos,
          "an entry count larger than the table is refused before allocating: " + error);
    const auto name_size = std::size_t(std::string("compiled/a.asset").size());
    const auto codec_offset = sizeof(std::uint32_t) + sizeof(std::uint32_t) + name_size + 3 * sizeof(std::uint64_t);
    const auto foreign_codec = edit_toc(intact,
                                        [codec_offset](std::uint8_t* toc)
                                        {
                                            toc[codec_offset] = PACK_TEST_FOREIGN_CODEC;
                                        });
    check(!mount_bytes("parser_codec.pak", foreign_codec, error) && error.find("codec") != std::string::npos,
          "an entry of another codec is refused: " + error);
    const auto raw_size_offset = sizeof(std::uint32_t) + sizeof(std::uint32_t) + name_size + sizeof(std::uint64_t);
    const auto huge_size = edit_toc(intact,
                                    [raw_size_offset](std::uint8_t* toc)
                                    {
                                        const std::uint64_t size = std::uint64_t(1) << 62;
                                        std::memcpy(toc + raw_size_offset, &size, sizeof(size));
                                    });
    check(!mount_bytes("parser_size.pak", huge_size, error), "an absurd entry size is refused");
    auto other_version = intact;
    other_version[other_version.size() - PACK_TEST_FOOTER_SIZE + PACK_TEST_FOOTER_VERSION] = PACK_TEST_OTHER_VERSION;
    check(!mount_bytes("parser_version.pak", other_version, error) && error.find("version") != std::string::npos,
          "a pack of another version is refused: " + error);
}

void test_replace_with_self_reuse()
{
    std::printf("test_replace_with_self_reuse\n");
    fs::unmount_packs();
    auto files = make_test_files();
    const auto pack_file = get_temp_folder() / "self_reuse.pak";
    std::string error;
    check(fs::write_pack(pack_file, stage_inputs("stage_self_first", files), {}, error), "the first pack is written");
    files[2].bytes.front() ^= 0xFF;
    const auto next_inputs = stage_inputs("stage_self_next", files);
    fs::pack_write_options options;
    options.reuse_from = pack_file;
    fs::pack_write_stats stats;
    check(fs::write_pack(pack_file, next_inputs, options, error, &stats), "the pack is replaced reusing itself: " + error);
    check(stats.reused_count == files.size() - 1, "every unchanged entry is reused from the pack being replaced");
    check(fs::write_pack(get_temp_folder() / "self_fresh.pak", next_inputs, {}, error) &&
              read_disk_file(pack_file) == read_disk_file(get_temp_folder() / "self_fresh.pak"),
          "the replaced pack equals a fresh write");
}

void test_lookup_status_and_damaged_reuse()
{
    std::printf("test_lookup_status_and_damaged_reuse\n");
    fs::unmount_packs();
    const auto bytes = make_compressible_bytes(MULTI_BLOCK_FILE_SIZE);
    const auto inputs = stage_inputs("stage_status", {{"compiled/first.asset", bytes}});
    std::string error;
    check(fs::write_pack(get_temp_folder() / "status.pak", inputs, {}, error), "the pack is written");
    // The first entry starts at offset 0: breaking its frame header leaves the table intact.
    auto damaged = read_disk_file(get_temp_folder() / "status.pak");
    damaged[0] ^= 0xFF;
    write_file(get_temp_folder() / "status_damaged.pak", damaged);
    check(fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "status_damaged.pak", error), "a pack with damaged data mounts");
    const auto path = get_protocol_path("compiled/first.asset");
    check(fs::lookup_packed_file(path).status == fs::packed_status::damaged, "an entry that does not decode is damaged");
    check(!fs::find_packed_file(path).is_found() && fs::open_readable_stream(path) == nullptr,
          "a damaged entry is neither found nor opened");
    check(fs::lookup_packed_file(get_protocol_path("compiled/none.asset")).status == fs::packed_status::absent,
          "a name no pack holds is absent");
    fs::unmount_packs();

    fs::pack_write_options options;
    options.reuse_from = get_temp_folder() / "status_damaged.pak";
    fs::pack_write_stats stats;
    check(fs::write_pack(get_temp_folder() / "status_reuse.pak", inputs, options, error, &stats) && stats.reused_count == 0,
          "a damaged entry of the earlier pack is not reused");
    check(read_disk_file(get_temp_folder() / "status_reuse.pak") == read_disk_file(get_temp_folder() / "status.pak"),
          "the pack written instead equals a fresh write");
}

void test_patch_rules()
{
    std::printf("test_patch_rules\n");
    fs::unmount_packs();
    const auto base_inputs = stage_inputs("stage_rules_base",
                                          {{"compiled/a.asset", make_compressible_bytes(SMALL_FILE_SIZE)},
                                           {"compiled/b.asset", make_compressible_bytes(MULTI_BLOCK_FILE_SIZE)}});
    const auto next_inputs = stage_inputs("stage_rules_next", {{"compiled/a.asset", make_incompressible_bytes(SMALL_FILE_SIZE)}});
    std::string error;
    const auto base = get_temp_folder() / "rules_base.pak";
    const auto patch = get_temp_folder() / "rules_patch.pak";
    check(fs::write_pack(base, base_inputs, {}, error), "the base is written");
    fs::pack_write_options patch_options;
    patch_options.patch_base = base;
    check(fs::write_pack(patch, next_inputs, patch_options, error), "the patch is written");

    fs::pack_write_options reuse_patch = patch_options;
    reuse_patch.reuse_from = patch;
    fs::pack_write_stats stats;
    check(fs::write_pack(get_temp_folder() / "rules_patch_again.pak", next_inputs, reuse_patch, error, &stats) &&
              read_disk_file(get_temp_folder() / "rules_patch_again.pak") == read_disk_file(patch),
          "a patch reusing the last patch equals a fresh patch");

    fs::pack_write_options stacked;
    stacked.patch_base = patch;
    check(!fs::write_pack(get_temp_folder() / "rules_stacked.pak", next_inputs, stacked, error) &&
              error.find("patch itself") != std::string::npos,
          "a patch is never the base of another patch: " + error);

    check(fs::mount_pack(TEST_PROTOCOL, base, error) && fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "fresh.pak", error),
          "the base and another pack mount");
    check(!fs::mount_pack(TEST_PROTOCOL, patch, error), "a patch does not mount when its base is not the topmost pack");
    fs::unmount_packs();
    check(fs::mount_pack(TEST_PROTOCOL, base, error) && fs::mount_pack(TEST_PROTOCOL, patch, error), "the patch mounts over its base");
    check(!fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "rules_patch_again.pak", error),
          "a second patch of the same base does not stack over the first");
    fs::unmount_packs();
}

void test_protocol_path_disk_fallback()
{
    std::printf("test_protocol_path_disk_fallback\n");
    fs::unmount_packs();
    const auto loose = make_compressible_bytes(SMALL_FILE_SIZE);
    write_file(get_protocol_root() / "compiled/loose_protocol.asset", loose);
    std::string error;
    check(fs::mount_pack(TEST_PROTOCOL, get_temp_folder() / "first.pak", error), "a pack mounts");
    const auto path = get_protocol_path("compiled/loose_protocol.asset");
    check(fs::is_readable_file(path), "a protocol path no pack holds is found on disk");
    check(read_through_stream(path) == loose, "a protocol path no pack holds is read from disk");
    fs::unmount_packs();
}

auto run_asset_packs_suite(rtti::context&) -> int
{
    g_checks = 0;
    g_failures = 0;
    fs::error_code ec;
    fs::remove_all(get_temp_folder(), ec);
    fs::create_directories(get_protocol_root(), ec);
    fs::add_path_protocol(TEST_PROTOCOL, get_protocol_root());
    test_compression_round_trip();
    test_round_trip(true);
    test_round_trip(false);
    test_disk_fallback_and_later_mount();
    test_damaged_packs_are_refused();
    test_deterministic_output();
    test_stream_seek_and_tell();
    test_reuse_of_unchanged_entries();
    test_patch_over_base();
    test_mixed_and_exact_blocks();
    test_lifetime_after_unmount();
    test_parallel_reads_during_mounts();
    test_parser_errors();
    test_replace_with_self_reuse();
    test_lookup_status_and_damaged_reuse();
    test_patch_rules();
    test_protocol_path_disk_fallback();
    fs::unmount_packs();
    fs::remove_all(get_temp_folder(), ec);
    std::printf("asset packs: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("asset packs", run_asset_packs_suite)
