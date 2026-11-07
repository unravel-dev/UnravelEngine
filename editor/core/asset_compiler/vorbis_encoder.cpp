#include "vorbis_encoder.h"

#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <cstring>

namespace unravel::asset_compiler
{
namespace
{
/// Vorbis qualities of the import settings: about 96, 128 and 192 kbit/s for 44.1 kHz stereo.
constexpr float LOW_QUALITY = 0.2f;
constexpr float NORMAL_QUALITY = 0.4f;
constexpr float HIGH_QUALITY = 0.6f;
/// Raised when a change here changes what a sound encodes to: the qualities above, the stream's layout.
constexpr int ENCODING_REVISION = 1;
/// Frames handed to the encoder at a time.
constexpr std::uint64_t FRAMES_PER_WRITE = 1024;
/// A file holds one logical stream: a fixed serial number keeps the output the same for the same sound.
constexpr int STREAM_SERIAL_NUMBER = 1;
constexpr std::uint8_t PCM_BITS_PER_SAMPLE = 16;
constexpr float PCM_SCALE = 1.0f / 32768.0f;
constexpr const char* VORBIS_CODEC = "ogg";

void append_page(const ogg_page& page, std::vector<std::uint8_t>& out)
{
    out.insert(out.end(), page.header, page.header + page.header_len);
    out.insert(out.end(), page.body, page.body + page.body_len);
}

/// libvorbis' and libogg's encoder state, released in the reverse order of its setup.
class vorbis_stream
{
public:
    vorbis_stream()
    {
        vorbis_info_init(&info_);
        vorbis_comment_init(&comment_);
    }

    ~vorbis_stream()
    {
        if(is_started_)
        {
            ogg_stream_clear(&stream_);
            vorbis_block_clear(&block_);
            vorbis_dsp_clear(&dsp_);
        }
        vorbis_comment_clear(&comment_);
        vorbis_info_clear(&info_);
    }

    vorbis_stream(const vorbis_stream&) = delete;
    auto operator=(const vorbis_stream&) -> vorbis_stream& = delete;

    /// False when libvorbis has no mode for the channel count, rate and quality.
    auto start(int channels, long sample_rate, float vorbis_quality) -> bool
    {
        if(vorbis_encode_init_vbr(&info_, channels, sample_rate, vorbis_quality) != 0)
        {
            return false;
        }
        vorbis_analysis_init(&dsp_, &info_);
        vorbis_block_init(&dsp_, &block_);
        ogg_stream_init(&stream_, STREAM_SERIAL_NUMBER);
        is_started_ = true;
        return true;
    }

    /// The three header packets, on pages of their own as the format wants: the audio starts on a new page.
    void write_headers(std::vector<std::uint8_t>& out)
    {
        ogg_packet identification{};
        ogg_packet comments{};
        ogg_packet codebooks{};
        vorbis_analysis_headerout(&dsp_, &comment_, &identification, &comments, &codebooks);
        ogg_stream_packetin(&stream_, &identification);
        ogg_stream_packetin(&stream_, &comments);
        ogg_stream_packetin(&stream_, &codebooks);
        flush_pages(out);
    }

    /// The frames of 16-bit interleaved PCM, read in place: each sample is copied out, since the bytes need not be
    /// aligned for int16.
    void write_frames(const audio::sound_data& pcm, std::vector<std::uint8_t>& out)
    {
        const auto channels = std::uint64_t(pcm.info.channels);
        const std::uint64_t frames = pcm.data.size() / (sizeof(std::int16_t) * channels);
        const auto* bytes = pcm.data.data();
        for(std::uint64_t first = 0; first < frames; first += FRAMES_PER_WRITE)
        {
            const int count = int(std::min(FRAMES_PER_WRITE, frames - first));
            float** buffer = vorbis_analysis_buffer(&dsp_, count);
            for(int frame = 0; frame < count; ++frame)
            {
                for(std::uint64_t channel = 0; channel < channels; ++channel)
                {
                    const std::uint64_t index = (first + std::uint64_t(frame)) * channels + channel;
                    std::int16_t sample{};
                    std::memcpy(&sample, bytes + index * sizeof(std::int16_t), sizeof(sample));
                    buffer[channel][frame] = float(sample) * PCM_SCALE;
                }
            }
            vorbis_analysis_wrote(&dsp_, count);
            write_ready_packets(out);
        }
    }

    /// Ends the stream: its last page carries the frame count, which trims the decoded sound to it.
    void finish(std::vector<std::uint8_t>& out)
    {
        vorbis_analysis_wrote(&dsp_, 0);
        write_ready_packets(out);
        flush_pages(out);
    }

private:
    void write_ready_packets(std::vector<std::uint8_t>& out)
    {
        while(vorbis_analysis_blockout(&dsp_, &block_) == 1)
        {
            vorbis_analysis(&block_, nullptr);
            vorbis_bitrate_addblock(&block_);
            ogg_packet packet{};
            while(vorbis_bitrate_flushpacket(&dsp_, &packet) == 1)
            {
                ogg_stream_packetin(&stream_, &packet);
                ogg_page page{};
                while(ogg_stream_pageout(&stream_, &page) != 0)
                {
                    append_page(page, out);
                }
            }
        }
    }

    void flush_pages(std::vector<std::uint8_t>& out)
    {
        ogg_page page{};
        while(ogg_stream_flush(&stream_, &page) != 0)
        {
            append_page(page, out);
        }
    }

    vorbis_info info_{};
    vorbis_comment comment_{};
    vorbis_dsp_state dsp_{};
    vorbis_block block_{};
    ogg_stream_state stream_{};
    bool is_started_{};
};

auto describe_mismatch(const audio::sound_data& expected, const audio::sound_data& actual) -> std::string
{
    return "the Vorbis stream decodes to " + std::to_string(actual.info.frames) + " frames of " +
           std::to_string(actual.info.channels) + " channel(s) instead of " + std::to_string(expected.info.frames) +
           " of " + std::to_string(expected.info.channels);
}
} // namespace

auto get_vorbis_quality(audio_importer_meta::compression_quality compression) -> float
{
    switch(compression)
    {
        case audio_importer_meta::compression_quality::low_quality:
            return LOW_QUALITY;
        case audio_importer_meta::compression_quality::high_quality:
            return HIGH_QUALITY;
        case audio_importer_meta::compression_quality::normal_quality:
        default:
            return NORMAL_QUALITY;
    }
}

auto get_vorbis_encoder_version() -> std::string
{
    return std::to_string(ENCODING_REVISION) + " " + vorbis_version_string();
}

auto encode_vorbis(const audio::sound_data& pcm, float vorbis_quality, std::vector<std::uint8_t>& encoded, std::string& error)
    -> bool
{
    const int channels = pcm.info.channels;
    if(pcm.info.bits_per_sample != PCM_BITS_PER_SAMPLE || channels == 0 || pcm.info.frames == 0)
    {
        error = "only 16-bit sounds with at least one frame are encoded to Vorbis";
        return false;
    }
    vorbis_stream stream;
    if(!stream.start(channels, long(pcm.info.sample_rate), vorbis_quality))
    {
        error = "libvorbis has no encoding for " + std::to_string(channels) + " channel(s) at " +
                std::to_string(pcm.info.sample_rate) + " Hz";
        return false;
    }
    encoded.clear();
    stream.write_headers(encoded);
    stream.write_frames(pcm, encoded);
    stream.finish(encoded);
    return true;
}

auto compress_clip_to_vorbis(const audio::sound_data& decoded,
                             audio_importer_meta::compression_quality compression,
                             encoded_sound& clip,
                             std::string& error) -> bool
{
    encoded_sound compressed;
    compressed.codec = VORBIS_CODEC;
    if(!encode_vorbis(decoded, get_vorbis_quality(compression), compressed.bytes, error))
    {
        return false;
    }
    // A clip plays its frames from the first to the last, as a loop does: the stream must hold exactly those.
    audio::sound_data round_trip;
    if(!decode_sound(compressed, round_trip, error))
    {
        error = "the Vorbis stream does not decode: " + error;
        return false;
    }
    if(round_trip.info.frames != decoded.info.frames || round_trip.info.channels != decoded.info.channels)
    {
        error = describe_mismatch(decoded, round_trip);
        return false;
    }
    clip = std::move(compressed);
    return true;
}

} // namespace unravel::asset_compiler
