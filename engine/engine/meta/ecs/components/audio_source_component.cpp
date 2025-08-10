#include "audio_source_component.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(audio_source_component)
{
    rttr::registration::class_<audio_source_component>(
        "audio_source_component")(rttr::metadata("category", "AUDIO"), rttr::metadata("pretty_name", "Audio Source"))
        .constructor<>()()
        .method("component_exists", &component_exists<audio_source_component>)

        .property("auto_play", &audio_source_component::get_autoplay, &audio_source_component::set_autoplay)(
            rttr::metadata("pretty_name", "Auto Play"))
        .property("loop", &audio_source_component::is_looping, &audio_source_component::set_loop)(
            rttr::metadata("pretty_name", "Loop"))
        .property("mute", &audio_source_component::is_muted, &audio_source_component::set_mute)(
            rttr::metadata("pretty_name", "Mute"))
        .property("volume", &audio_source_component::get_volume, &audio_source_component::set_volume)(
            rttr::metadata("pretty_name", "Volume"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f))
        .property("pitch", &audio_source_component::get_pitch, &audio_source_component::set_pitch)(
            rttr::metadata("pretty_name", "Pitch"),
            rttr::metadata("tooltip", "A multiplier for the frequency (sample rate) of the source's buffer."),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 5.0f))
        .property("volume_rolloff",
                  &audio_source_component::get_volume_rolloff,
                  &audio_source_component::set_volume_rolloff)(rttr::metadata("pretty_name", "Volume Rolloff"),
                                                               rttr::metadata("min", 0.0f),
                                                               rttr::metadata("max", 10.0f))
        .property("range", &audio_source_component::get_range, &audio_source_component::set_range)(
            rttr::metadata("pretty_name", "Range"))
        .property("clip", &audio_source_component::get_clip, &audio_source_component::set_clip)(
            rttr::metadata("pretty_name", "Clip"));

    // Register audio_source_component class with entt
    entt::meta_factory<audio_source_component>{}
        .type("audio_source_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"category", "AUDIO"},
            entt::attribute{"pretty_name", "Audio Source"},
        })
        .func<&component_exists<audio_source_component>>("component_exists"_hs)
        .data<&audio_source_component::set_autoplay, &audio_source_component::get_autoplay>("auto_play"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Auto Play"},
        })
        .data<&audio_source_component::set_loop, &audio_source_component::is_looping>("loop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Loop"},
        })
        .data<&audio_source_component::set_mute, &audio_source_component::is_muted>("mute"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Mute"},
        })
        .data<&audio_source_component::set_volume, &audio_source_component::get_volume>("volume"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Volume"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&audio_source_component::set_pitch, &audio_source_component::get_pitch>("pitch"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Pitch"},
            entt::attribute{"tooltip", "A multiplier for the frequency (sample rate) of the source's buffer."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 5.0f},
        })
        .data<&audio_source_component::set_volume_rolloff, &audio_source_component::get_volume_rolloff>("volume_rolloff"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Volume Rolloff"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 10.0f},
        })
        .data<&audio_source_component::set_range, &audio_source_component::get_range>("range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Range"},
        })
        .data<&audio_source_component::set_clip, &audio_source_component::get_clip>("clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Clip"},
        });
}

SAVE(audio_source_component)
{
    try_save(ar, ser20::make_nvp("auto_play", obj.get_autoplay()));
    try_save(ar, ser20::make_nvp("loop", obj.is_looping()));
    try_save(ar, ser20::make_nvp("volume", obj.get_volume()));
    try_save(ar, ser20::make_nvp("pitch", obj.get_pitch()));
    try_save(ar, ser20::make_nvp("volume_rolloff", obj.get_volume_rolloff()));
    try_save(ar, ser20::make_nvp("range", obj.get_range()));
    try_save(ar, ser20::make_nvp("clip", obj.get_clip()));
}
SAVE_INSTANTIATE(audio_source_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(audio_source_component, ser20::oarchive_binary_t);

LOAD(audio_source_component)
{
    bool auto_play{};
    if(try_load(ar, ser20::make_nvp("auto_play", auto_play)))
    {
        obj.set_autoplay(auto_play);
    }

    bool loop{};
    if(try_load(ar, ser20::make_nvp("loop", loop)))
    {
        obj.set_loop(loop);
    }

    float volume{1.0f};
    if(try_load(ar, ser20::make_nvp("volume", volume)))
    {
        obj.set_volume(volume);
    }

    float pitch{1.0f};
    if(try_load(ar, ser20::make_nvp("pitch", pitch)))
    {
        obj.set_pitch(pitch);
    }

    float volume_rolloff{1.0f};
    if(try_load(ar, ser20::make_nvp("volume_rolloff", volume_rolloff)))
    {
        obj.set_volume_rolloff(volume_rolloff);
    }

    frange_t range;
    if(try_load(ar, ser20::make_nvp("range", range)))
    {
        obj.set_range(range);
    }

    asset_handle<audio_clip> clip;
    if(try_load(ar, ser20::make_nvp("clip", clip)))
    {
        obj.set_clip(clip);
    }
}
LOAD_INSTANTIATE(audio_source_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(audio_source_component, ser20::iarchive_binary_t);
} // namespace unravel
