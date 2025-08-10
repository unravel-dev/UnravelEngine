#include "audio_clip.hpp"
#include <audiopp/loaders/loader.h>
#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/chrono.hpp>

namespace audio
{
REFLECT_EXTERN(sound_info);
REFLECT(sound_info)
{
    rttr::registration::class_<sound_info>("sound_info")
        .property_readonly("bytes_per_sample",
                           &sound_info::bits_per_sample)(rttr::metadata("pretty_name", "Bits per sample"),
                                                         rttr::metadata("tooltip", "Bit depth."))
        .property_readonly("sample_rate", &sound_info::sample_rate)(rttr::metadata("pretty_name", "Sample rate"),
                                                                    rttr::metadata("tooltip", "Sample rate."))
        .property_readonly("channels", &sound_info::channels)(rttr::metadata("pretty_name", "Channels"),
                                                              rttr::metadata("tooltip", "Mono or Stereo."))
        .property_readonly("duration", &sound_info::duration)(rttr::metadata("pretty_name", "Duration"),
                                                              rttr::metadata("tooltip", "Duration in seconds."))
        .property_readonly("frames", &sound_info::frames)(rttr::metadata("pretty_name", "Frames"),
                                                              rttr::metadata("tooltip", "Fames count (samples per channel).&"));

    // Register sound_info with entt
    entt::meta_factory<sound_info>{}
        .type("sound_info"_hs)
        .data<nullptr, &sound_info::bits_per_sample>("bytes_per_sample"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Bits per sample"},
            entt::attribute{"tooltip", "Bit depth."},
        })
        .data<nullptr, &sound_info::sample_rate>("sample_rate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Sample rate"},
            entt::attribute{"tooltip", "Sample rate."},
        })
        .data<nullptr, &sound_info::channels>("channels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Channels"},
            entt::attribute{"tooltip", "Mono or Stereo."},
        })
        .data<nullptr, &sound_info::duration>("duration"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Duration"},
            entt::attribute{"tooltip", "Duration in seconds."},
        })
        .data<nullptr, &sound_info::frames>("frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Frames"},
            entt::attribute{"tooltip", "Fames count (samples per channel).&"},
        });
}

SAVE(audio::sound_info)
{
    try_save(ar, ser20::make_nvp("bits_per_sample", obj.bits_per_sample));
    try_save(ar, ser20::make_nvp("sample_rate", obj.sample_rate));
    try_save(ar, ser20::make_nvp("channels", obj.channels));
    try_save(ar, ser20::make_nvp("duration", obj.duration));
    try_save(ar, ser20::make_nvp("frames", obj.frames));
}
// SAVE_INSTANTIATE(sound_info, ser20::oarchive_binary_t);

LOAD(audio::sound_info)
{
    try_load(ar, ser20::make_nvp("bits_per_sample", obj.bits_per_sample));
    try_load(ar, ser20::make_nvp("sample_rate", obj.sample_rate));
    try_load(ar, ser20::make_nvp("channels", obj.channels));
    try_load(ar, ser20::make_nvp("duration", obj.duration));
    try_load(ar, ser20::make_nvp("frames", obj.frames));
}
// LOAD_INSTANTIATE(sound_info, ser20::iarchive_binary_t);

SAVE(audio::sound_data)
{
    try_save(ar, ser20::make_nvp("info", obj.info));
    try_save(ar, ser20::make_nvp("data", obj.data));
}
// SAVE_INSTANTIATE(sound_data, ser20::oarchive_binary_t);

LOAD(audio::sound_data)
{
    try_load(ar, ser20::make_nvp("info", obj.info));
    try_load(ar, ser20::make_nvp("data", obj.data));
}
// LOAD_INSTANTIATE(sound_data, ser20::iarchive_binary_t);

} // namespace audio

namespace unravel
{

REFLECT(audio_clip)
{
    rttr::registration::class_<audio_clip>("audio_clip")(rttr::metadata("pretty_name", "Audio Clip")).constructor<>()();

    // Register audio_clip with entt
    entt::meta_factory<audio_clip>{}
        .type("audio_clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Audio Clip"},
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

void save_to_file(const std::string& absolute_path, const audio::sound_data& obj)
{
    // std::ofstream stream(absolute_path);
    // if(stream.good())
    // {
    //     auto ar = ser20::create_oarchive_associative(stream);
    //     try_save(ar, ser20::make_nvp("audio_clip", obj));
    // }
}

void save_to_file_bin(const std::string& absolute_path, const audio::sound_data& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("sound_data", obj));
    }
}

auto load_from_file(const std::string& absolute_path, audio::sound_data& obj, std::string& err) -> bool
{
    return audio::load_from_file(absolute_path, obj, err);

    // std::ifstream stream(absolute_path);
    // if(stream.good())
    // {
    //     auto ar = ser20::create_iarchive_associative(stream);
    //     try_load(ar, ser20::make_nvp("audio_clip", obj));
    // }
}

void load_from_file_bin(const std::string& absolute_path, audio::sound_data& obj)
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("sound_data", obj));
    }
}
} // namespace unravel
