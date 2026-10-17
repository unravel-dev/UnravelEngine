#include "audio_clip.hpp"
#include <fstream>
#include <filesystem/file_istream.h>
#include <filesystem/pack.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

namespace audio
{
REFLECT_EXTERN(sound_info);
REFLECT(sound_info)
{
    entt::meta_factory<sound_info>{}
        .type("sound_info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sound_info"},
            entt::attribute{"pretty_name", "Sound Info"},
        })
        .data<nullptr, &sound_info::bits_per_sample>("bytes_per_sample"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bytes_per_sample"},
            entt::attribute{"pretty_name", "Bits per sample"},
            entt::attribute{"tooltip", "Bit depth."},
        })
        .data<nullptr, &sound_info::sample_rate>("sample_rate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sample_rate"},
            entt::attribute{"pretty_name", "Sample rate"},
            entt::attribute{"tooltip", "Sample rate."},
        })
        .data<nullptr, &sound_info::channels>("channels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "channels"},
            entt::attribute{"pretty_name", "Channels"},
            entt::attribute{"tooltip", "Mono or Stereo."},
        })
        .data<nullptr, &sound_info::duration>("duration"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "duration"},
            entt::attribute{"pretty_name", "Duration"},
            entt::attribute{"tooltip", "Duration in seconds."},
        })
        .data<nullptr, &sound_info::frames>("frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "frames"},
            entt::attribute{"pretty_name", "Frames"},
            entt::attribute{"tooltip", "Fames count (samples per channel)."},
        });
}

} // namespace audio

namespace unravel
{

REFLECT(audio_clip)
{
    entt::meta_factory<audio_clip>{}
        .type("audio_clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "audio_clip"},
            entt::attribute{"pretty_name", "Audio Clip"},
        })
        .data<nullptr, &audio_clip::get_info>("info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "info"},
            entt::attribute{"pretty_name", "Info"},
        });
}

SAVE(audio_clip)
{
    // try_save(ar, ser20::make_nvp("info", obj.info));
    // try_save(ar, ser20::make_nvp("data", obj.data));
}
SAVE_INSTANTIATE(audio_clip, ser20::oarchive_binary_t);

LOAD(audio_clip)
{
    // try_load(ar, ser20::make_nvp("info", obj.info));
    // try_load(ar, ser20::make_nvp("data", obj.data));
}
LOAD_INSTANTIATE(audio_clip, ser20::iarchive_binary_t);

SAVE(encoded_sound)
{
    try_save(ar, ser20::make_nvp("codec", obj.codec));
    try_save(ar, ser20::make_nvp("bytes", obj.bytes));
    try_save(ar, ser20::make_nvp("force_to_mono", obj.force_to_mono));

    // Changes here should be reflected in ex::get_format_version<audio_clip>() in asset_extensions.h
}
SAVE_INSTANTIATE(encoded_sound, ser20::oarchive_binary_t);

LOAD(encoded_sound)
{
    try_load(ar, ser20::make_nvp("codec", obj.codec));
    try_load(ar, ser20::make_nvp("bytes", obj.bytes));
    try_load(ar, ser20::make_nvp("force_to_mono", obj.force_to_mono));

    // Changes here should be reflected in ex::get_format_version<audio_clip>() in asset_extensions.h
}
LOAD_INSTANTIATE(encoded_sound, ser20::iarchive_binary_t);

auto save_to_file_bin(const std::string& absolute_path, const encoded_sound& obj) -> bool
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(!stream.good())
    {
        return false;
    }
    ser20::oarchive_binary_t ar(stream);
    return try_save(ar, ser20::make_nvp("encoded_sound", obj)) && stream.good();
}

auto load_from_file_bin(const std::string& absolute_path, encoded_sound& obj) -> bool
{
    auto input = fs::open_readable_stream(absolute_path);
    if(!input)
    {
        return false;
    }
    ser20::iarchive_binary_t ar(*input);
    return try_load(ar, ser20::make_nvp("encoded_sound", obj));
}
} // namespace unravel
