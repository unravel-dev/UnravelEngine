#include "audio_clip.h"

#include <audiopp/loaders/loader.h>

namespace unravel
{
namespace
{
using decode_function = bool (*)(const std::uint8_t*, std::size_t, audio::sound_data&, std::string&);

auto find_decoder(const std::string& codec) -> decode_function
{
    if(codec == "ogg")
    {
        return audio::load_from_memory_ogg;
    }
    if(codec == "mp3")
    {
        return audio::load_from_memory_mp3;
    }
    if(codec == "flac")
    {
        return audio::load_from_memory_flac;
    }
    if(codec == "wav" || codec == "wave")
    {
        return audio::load_from_memory_wav;
    }
    return nullptr;
}
} // namespace

auto decode_sound(const encoded_sound& encoded, audio::sound_data& result, std::string& err) -> bool
{
    const decode_function decoder = find_decoder(encoded.codec);
    if(decoder == nullptr)
    {
        err = "Unsupported audio codec: " + encoded.codec;
        return false;
    }
    if(!decoder(encoded.bytes.data(), encoded.bytes.size(), result, err))
    {
        return false;
    }
    if(encoded.force_to_mono)
    {
        result.convert_to_mono();
    }
    return true;
}

} // namespace unravel
