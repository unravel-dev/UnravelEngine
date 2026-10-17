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
 *   - the same inputs write the same bytes; streams seek and tell.
 * Everything lives in a temp folder under a protocol the suite registers.
 */

#include "../tests.h"

#include <compression/compression.h>
#include <filesystem/filesystem.h>
#include <filesystem/pack.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
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
        {"assets.pack", make_compressible_bytes(SMALL_FILE_SIZE)},
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
    fs::unmount_packs();
    fs::remove_all(get_temp_folder(), ec);
    std::printf("asset packs: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("asset packs", run_asset_packs_suite)
