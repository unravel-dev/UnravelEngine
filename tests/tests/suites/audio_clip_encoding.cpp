/*
 * Validation suite for compiled audio clips and the deploy's asset index filter.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "audio clip encoding"
 *
 * A compiled audio clip keeps the source file's bytes in their own codec and decodes when it loads, so
 * a compressed source stays compressed on disk and in a deployed game. This pins:
 *   - decode_sound reads a codec's bytes into PCM and applies force-to-mono;
 *   - an unknown codec and undecodable bytes fail with a reason instead of yielding a sound;
 *   - the compiled file round-trips, and a file in another format (an older cache) is refused;
 *   - asset_manager::save_database keeps only the entries its filter accepts, without changing the
 *     database itself.
 * Everything is generated in memory or in a temp folder; no audio device is needed.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/audio/audio_clip.hpp>

#include <filesystem/filesystem.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

constexpr std::uint32_t TEST_SAMPLE_RATE = 22050;
constexpr std::uint16_t TEST_CHANNELS = 2;
constexpr std::uint16_t TEST_BITS_PER_SAMPLE = 16;
constexpr std::uint32_t TEST_FRAMES = 2205;
constexpr std::uint32_t WAV_FMT_CHUNK_SIZE = 16;
constexpr std::uint16_t WAV_FORMAT_PCM = 1;
constexpr std::uint32_t WAV_RIFF_HEADER_REMAINDER = 36;
constexpr std::size_t GARBAGE_FILE_SIZE = 64;
constexpr std::uint8_t GARBAGE_BYTE = 0xFF;
constexpr const char* TEST_PROTOCOL = "unravel-tests-audio:/";

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(std::uint8_t(value & 0xFF));
    out.push_back(std::uint8_t(value >> 8));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    append_u16(out, std::uint16_t(value & 0xFFFF));
    append_u16(out, std::uint16_t(value >> 16));
}

void append_tag(std::vector<std::uint8_t>& out, const char* tag)
{
    out.insert(out.end(), tag, tag + 4);
}

/// A 16-bit PCM WAV file: a sawtooth on both channels.
auto make_wav_bytes() -> std::vector<std::uint8_t>
{
    const std::uint32_t block_align = TEST_CHANNELS * (TEST_BITS_PER_SAMPLE / 8);
    const std::uint32_t data_size = TEST_FRAMES * block_align;
    std::vector<std::uint8_t> bytes;
    append_tag(bytes, "RIFF");
    append_u32(bytes, WAV_RIFF_HEADER_REMAINDER + data_size);
    append_tag(bytes, "WAVE");
    append_tag(bytes, "fmt ");
    append_u32(bytes, WAV_FMT_CHUNK_SIZE);
    append_u16(bytes, WAV_FORMAT_PCM);
    append_u16(bytes, TEST_CHANNELS);
    append_u32(bytes, TEST_SAMPLE_RATE);
    append_u32(bytes, TEST_SAMPLE_RATE * block_align);
    append_u16(bytes, std::uint16_t(block_align));
    append_u16(bytes, TEST_BITS_PER_SAMPLE);
    append_tag(bytes, "data");
    append_u32(bytes, data_size);
    for(std::uint32_t frame = 0; frame < TEST_FRAMES; ++frame)
    {
        for(std::uint16_t channel = 0; channel < TEST_CHANNELS; ++channel)
        {
            append_u16(bytes, std::uint16_t(frame * 31u + channel * 7u));
        }
    }
    return bytes;
}

auto make_wav_clip() -> encoded_sound
{
    encoded_sound clip;
    clip.codec = "wav";
    clip.bytes = make_wav_bytes();
    return clip;
}

auto get_temp_folder() -> fs::path
{
    const auto folder = fs::temp_directory_path() / "unravel-tests-audio-clip-encoding";
    fs::error_code ec;
    fs::create_directories(folder, ec);
    return folder;
}

void test_decode_wav()
{
    std::printf("test_decode_wav\n");
    const auto input_clip = make_wav_clip();
    audio::sound_data actual;
    std::string error;
    check(decode_sound(input_clip, actual, error), "a WAV clip decodes: " + error);
    check(actual.info.sample_rate == TEST_SAMPLE_RATE, "the sample rate survives");
    check(actual.info.channels == TEST_CHANNELS, "both channels survive");
    check(actual.info.frames == TEST_FRAMES, "every frame decodes");
    check(actual.data.size() == std::size_t(TEST_FRAMES) * TEST_CHANNELS * (TEST_BITS_PER_SAMPLE / 8),
          "the PCM holds every sample");
}

void test_decode_to_mono()
{
    std::printf("test_decode_to_mono\n");
    auto input_clip = make_wav_clip();
    input_clip.force_to_mono = true;
    audio::sound_data actual;
    std::string error;
    check(decode_sound(input_clip, actual, error), "a WAV clip forced to mono decodes: " + error);
    check(actual.info.channels == 1, "force_to_mono leaves one channel");
    check(actual.info.frames == TEST_FRAMES, "force_to_mono keeps every frame");
}

void test_decode_failures()
{
    std::printf("test_decode_failures\n");
    auto unknown_codec = make_wav_clip();
    unknown_codec.codec = "aiff";
    audio::sound_data actual;
    std::string error;
    check(!decode_sound(unknown_codec, actual, error), "an unknown codec does not decode");
    check(error.find("aiff") != std::string::npos, "the error names the unknown codec");

    encoded_sound broken;
    broken.codec = "ogg";
    broken.bytes.assign(GARBAGE_FILE_SIZE, GARBAGE_BYTE);
    error.clear();
    check(!decode_sound(broken, actual, error), "bytes that are not the codec's do not decode");

    encoded_sound empty;
    empty.codec = "mp3";
    error.clear();
    check(!decode_sound(empty, actual, error), "an empty clip does not decode");
}

void test_compiled_file_round_trip()
{
    std::printf("test_compiled_file_round_trip\n");
    auto expected = make_wav_clip();
    expected.force_to_mono = true;
    const auto path = (get_temp_folder() / "clip.wav.asset").string();
    check(save_to_file_bin(path, expected), "the compiled clip is written");
    encoded_sound actual;
    check(load_from_file_bin(path, actual), "the compiled clip reads back");
    check(actual.codec == expected.codec, "the codec round-trips");
    check(actual.bytes == expected.bytes, "the bytes round-trip");
    check(actual.force_to_mono == expected.force_to_mono, "the mono flag round-trips");
    encoded_sound missing;
    check(!load_from_file_bin((get_temp_folder() / "missing.asset").string(), missing), "a missing file is refused");
}

void test_other_format_is_refused()
{
    std::printf("test_other_format_is_refused\n");
    const auto path = get_temp_folder() / "older.mp3.asset";
    {
        std::ofstream out(path.string(), std::ios::binary);
        const std::vector<char> garbage(GARBAGE_FILE_SIZE, char(GARBAGE_BYTE));
        out.write(garbage.data(), std::streamsize(garbage.size()));
    }
    encoded_sound actual;
    audio::sound_data decoded;
    std::string error;
    const bool is_loaded = load_from_file_bin(path.string(), actual) && decode_sound(actual, decoded, error);
    check(!is_loaded, "a file in another format yields no sound");
}

void test_filtered_database_save(rtti::context& ctx)
{
    std::printf("test_filtered_database_save\n");
    auto& am = ctx.get_cached<asset_manager>();
    const std::string kept_key = std::string(TEST_PROTOCOL) + "data/kept.png";
    const std::string dropped_key = std::string(TEST_PROTOCOL) + "data/dropped.cs";
    am.add_asset(kept_key, true);
    am.add_asset(dropped_key, true);
    const auto path = (get_temp_folder() / "assets.pack").string();
    am.save_database(TEST_PROTOCOL,
                     path,
                     [&dropped_key](const std::string& location)
                     {
                         return location != dropped_key;
                     });
    asset_database actual;
    check(load_from_file(path, actual), "the filtered database reads back");
    bool has_kept = false;
    bool has_dropped = false;
    for(const auto& [uid, meta] : actual.get_database())
    {
        has_kept = has_kept || meta.location == kept_key;
        has_dropped = has_dropped || meta.location == dropped_key;
    }
    check(has_kept && actual.get_database().size() == 1, "the saved copy keeps exactly the accepted entry");
    check(!has_dropped, "the saved copy leaves the rejected entry out");
    check(!am.get_metadata_for_key(dropped_key).location.empty(), "the database itself keeps the rejected entry");
    am.unload_group(TEST_PROTOCOL);
}

auto run_audio_clip_encoding_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_decode_wav();
    test_decode_to_mono();
    test_decode_failures();
    test_compiled_file_round_trip();
    test_other_format_is_refused();
    test_filtered_database_save(ctx);
    fs::error_code ec;
    fs::remove_all(get_temp_folder(), ec);
    std::printf("audio clip encoding: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("audio clip encoding", run_audio_clip_encoding_suite)
