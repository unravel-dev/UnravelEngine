/*
 * Validation suite for compiled audio clips and the deploy's asset index filter.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "audio clip encoding"
 *
 * A compiled audio clip keeps the source file's bytes in their own codec, or for a lossless source its Vorbis
 * encoding, and decodes when it loads, so a compressed source stays compressed on disk and in a deployed game.
 * This pins:
 *   - decode_sound reads a codec's bytes into PCM and applies force-to-mono;
 *   - Vorbis at import: a stream decodes to exactly the source's frames at any length (a loop has no gap), a
 *     tone keeps its shape, the stream is several times smaller and larger at a higher quality, the same sound
 *     encodes to the same bytes, a clip mixed to mono stays mono, and sounds the encoder cannot take are refused
 *     with a reason;
 *   - the compression setting: a new clip follows the project's default (Normal Quality), its own setting wins, a
 *     project default of Project Default falls back to Normal Quality, only WAV and FLAC at a quality compress, and
 *     each setting records its own compile parameters;
 *   - an unknown codec and undecodable bytes fail with a reason instead of yielding a sound;
 *   - the compiled file round-trips, and a file in another format (an older cache) is refused;
 *   - asset_manager::save_database keeps only the entries its filter accepts, without changing the
 *     database itself.
 * Everything is generated in memory or in a temp folder; no audio device is needed.
 */

#include "../tests.h"

#include <asset_compiler/audio_compression.h>
#include <asset_compiler/vorbis_encoder.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/audio/audio_clip.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/settings/settings.h>

#include <filesystem/filesystem.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
constexpr std::uint32_t TONE_SAMPLE_RATE = 44100;
/// A second and a bit: no multiple of the encoder's blocks or of the frames it is handed at a time.
constexpr std::uint64_t TONE_FRAMES = 44100 + 123;
constexpr std::array<double, 2> TONE_FREQUENCIES = {440.0, 660.0};
constexpr double TONE_AMPLITUDE = 0.5;
constexpr double PCM_FULL_SCALE = 32767.0;
constexpr double TWO_PI = 6.283185307179586;
/// Lengths around the encoder's block and write sizes, and a single frame.
constexpr std::array<std::uint64_t, 5> AWKWARD_FRAME_COUNTS = {1, 255, 1023, 1025, 4097};
/// A tone at normal quality decodes far above this; a stream that drifts or drops samples falls well below.
constexpr double MIN_TONE_SNR_DB = 20.0;
/// Vorbis at normal quality is about a tenth of 16-bit stereo PCM; a fourth leaves room for any tone.
constexpr std::size_t MIN_COMPRESSION_RATIO = 4;
constexpr std::uint8_t UNSUPPORTED_BITS_PER_SAMPLE = 8;

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

/// 16-bit PCM of a sine on each channel (440 Hz, 660 Hz), as audiopp's decoders give a sound.
auto make_tone(std::uint8_t channels, std::uint64_t frames) -> audio::sound_data
{
    audio::sound_data tone;
    tone.info.sample_rate = TONE_SAMPLE_RATE;
    tone.info.bits_per_sample = TEST_BITS_PER_SAMPLE;
    tone.info.channels = channels;
    tone.info.frames = frames;
    std::vector<std::int16_t> samples(std::size_t(frames) * channels);
    for(std::uint64_t frame = 0; frame < frames; ++frame)
    {
        for(std::uint8_t channel = 0; channel < channels; ++channel)
        {
            const double phase = TWO_PI * TONE_FREQUENCIES[channel % TONE_FREQUENCIES.size()] * double(frame) / TONE_SAMPLE_RATE;
            samples[frame * channels + channel] = std::int16_t(std::lround(TONE_AMPLITUDE * PCM_FULL_SCALE * std::sin(phase)));
        }
    }
    tone.data.resize(samples.size() * sizeof(std::int16_t));
    std::memcpy(tone.data.data(), samples.data(), tone.data.size());
    return tone;
}

auto read_samples(const audio::sound_data& sound) -> std::vector<std::int16_t>
{
    std::vector<std::int16_t> samples(sound.data.size() / sizeof(std::int16_t));
    std::memcpy(samples.data(), sound.data.data(), samples.size() * sizeof(std::int16_t));
    return samples;
}

/// The signal-to-noise ratio of `actual` against `expected` in dB, sample by sample.
auto get_snr_db(const audio::sound_data& expected, const audio::sound_data& actual) -> double
{
    const auto expected_samples = read_samples(expected);
    const auto actual_samples = read_samples(actual);
    double signal = 0.0;
    double noise = 0.0;
    for(std::size_t i = 0; i < expected_samples.size() && i < actual_samples.size(); ++i)
    {
        const double difference = double(actual_samples[i]) - double(expected_samples[i]);
        signal += double(expected_samples[i]) * double(expected_samples[i]);
        noise += difference * difference;
    }
    return noise == 0.0 ? 1000.0 : 10.0 * std::log10(signal / noise);
}

auto encode_tone(const audio::sound_data& tone, audio_importer_meta::compression_quality quality) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> encoded;
    std::string error;
    check(asset_compiler::encode_vorbis(tone, asset_compiler::get_vorbis_quality(quality), encoded, error),
          "a tone encodes to Vorbis: " + error);
    return encoded;
}

auto decode_vorbis(const std::vector<std::uint8_t>& bytes) -> audio::sound_data
{
    encoded_sound clip;
    clip.codec = "ogg";
    clip.bytes = bytes;
    audio::sound_data decoded;
    std::string error;
    check(decode_sound(clip, decoded, error), "the Vorbis stream decodes: " + error);
    return decoded;
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

void test_vorbis_round_trip()
{
    std::printf("test_vorbis_round_trip\n");
    const auto tone = make_tone(TEST_CHANNELS, TONE_FRAMES);
    const auto encoded = encode_tone(tone, audio_importer_meta::compression_quality::normal_quality);
    const auto decoded = decode_vorbis(encoded);
    check(decoded.info.frames == tone.info.frames, "the stream decodes to exactly the source's frames");
    check(decoded.info.channels == tone.info.channels && decoded.info.sample_rate == tone.info.sample_rate,
          "the channels and the rate survive");
    const double snr = get_snr_db(tone, decoded);
    std::printf("  tone at normal quality: %zu of %zu bytes, %.1f dB\n", encoded.size(), tone.data.size(), snr);
    check(snr >= MIN_TONE_SNR_DB, "a tone keeps its shape");
    check(encoded.size() * MIN_COMPRESSION_RATIO < tone.data.size(), "the stream is several times smaller than the PCM");
    check(encode_tone(tone, audio_importer_meta::compression_quality::normal_quality) == encoded,
          "the same sound encodes to the same bytes");
}

void test_vorbis_lengths()
{
    std::printf("test_vorbis_lengths\n");
    for(const auto frames : AWKWARD_FRAME_COUNTS)
    {
        const auto tone = make_tone(TEST_CHANNELS, frames);
        const auto decoded = decode_vorbis(encode_tone(tone, audio_importer_meta::compression_quality::normal_quality));
        check(decoded.info.frames == frames, "a stream of " + std::to_string(frames) + " frame(s) decodes to as many");
    }
}

void test_vorbis_quality_order()
{
    std::printf("test_vorbis_quality_order\n");
    const auto tone = make_tone(TEST_CHANNELS, TONE_FRAMES);
    const auto low = encode_tone(tone, audio_importer_meta::compression_quality::low_quality).size();
    const auto normal = encode_tone(tone, audio_importer_meta::compression_quality::normal_quality).size();
    const auto high = encode_tone(tone, audio_importer_meta::compression_quality::high_quality).size();
    std::printf("  low %zu, normal %zu, high %zu bytes\n", low, normal, high);
    check(low < normal && normal < high, "a higher quality makes a larger stream");
}

void test_compression_setting()
{
    std::printf("test_compression_setting\n");
    using quality = audio_importer_meta::compression_quality;
    using asset_compiler::get_audio_compile_parameters;
    using asset_compiler::is_compressed_to_vorbis;
    using asset_compiler::resolve_audio_compression;
    settings::asset_settings::audio_importer_settings defaults;
    audio_importer_meta importer;
    check(importer.compression == quality::project_default && resolve_audio_compression(importer, defaults) == quality::normal_quality,
          "a new clip follows the project's default, Normal Quality");
    defaults.default_compression = quality::none;
    check(resolve_audio_compression(importer, defaults) == quality::none, "it follows a changed project default");
    defaults.default_compression = quality::project_default;
    check(resolve_audio_compression(importer, defaults) == quality::normal_quality,
          "a project default of Project Default falls back to Normal Quality");
    importer.compression = quality::high_quality;
    defaults.default_compression = quality::none;
    check(resolve_audio_compression(importer, defaults) == quality::high_quality, "a clip's own setting wins");
    check(is_compressed_to_vorbis(quality::normal_quality, "wav") && is_compressed_to_vorbis(quality::low_quality, "flac"),
          "WAV and FLAC sources at a quality compress to Vorbis");
    check(!is_compressed_to_vorbis(quality::high_quality, "ogg") && !is_compressed_to_vorbis(quality::high_quality, "mp3"),
          "OGG and MP3 sources keep their own bytes");
    check(!is_compressed_to_vorbis(quality::none, "wav"), "None keeps a WAV's bytes");
    check(get_audio_compile_parameters(quality::none) != get_audio_compile_parameters(quality::normal_quality) &&
              get_audio_compile_parameters(quality::low_quality) != get_audio_compile_parameters(quality::high_quality),
          "each setting records its own compile parameters, so a changed default compiles the clips again");
}

void test_clip_compression()
{
    std::printf("test_clip_compression\n");
    auto clip = make_wav_clip();
    clip.force_to_mono = true;
    audio::sound_data decoded;
    std::string error;
    check(decode_sound(clip, decoded, error), "the WAV clip decodes: " + error);
    check(asset_compiler::compress_clip_to_vorbis(decoded, audio_importer_meta::compression_quality::normal_quality, clip, error),
          "a WAV clip mixed to mono compresses: " + error);
    check(clip.codec == "ogg" && !clip.force_to_mono, "the clip becomes a Vorbis stream of the mixed sound");
    audio::sound_data actual;
    check(decode_sound(clip, actual, error), "the compressed clip decodes: " + error);
    check(actual.info.channels == 1 && actual.info.frames == TEST_FRAMES, "it stays mono with every frame");
}

void test_vorbis_refusals()
{
    std::printf("test_vorbis_refusals\n");
    std::vector<std::uint8_t> encoded;
    std::string error;
    auto eight_bit = make_tone(TEST_CHANNELS, TONE_FRAMES);
    eight_bit.info.bits_per_sample = UNSUPPORTED_BITS_PER_SAMPLE;
    check(!asset_compiler::encode_vorbis(eight_bit, asset_compiler::get_vorbis_quality({}), encoded, error) && !error.empty(),
          "a sound that is not 16-bit is refused with a reason");
    error.clear();
    const auto empty = make_tone(TEST_CHANNELS, 0);
    check(!asset_compiler::encode_vorbis(empty, asset_compiler::get_vorbis_quality({}), encoded, error) && !error.empty(),
          "a sound without frames is refused with a reason");
}

void test_filtered_database_save(rtti::context& ctx)
{
    std::printf("test_filtered_database_save\n");
    auto& am = ctx.get_cached<asset_manager>();
    const std::string kept_key = std::string(TEST_PROTOCOL) + "data/kept.png";
    const std::string dropped_key = std::string(TEST_PROTOCOL) + "data/dropped.cs";
    am.add_asset(kept_key, true);
    am.add_asset(dropped_key, true);
    const auto path = (get_temp_folder() / ex::get_asset_index_file_name()).string();
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
    test_vorbis_round_trip();
    test_vorbis_lengths();
    test_vorbis_quality_order();
    test_compression_setting();
    test_clip_compression();
    test_vorbis_refusals();
    test_filtered_database_save(ctx);
    fs::error_code ec;
    fs::remove_all(get_temp_folder(), ec);
    std::printf("audio clip encoding: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("audio clip encoding", run_audio_clip_encoding_suite)
