#include "audio_source_component.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(audio_bus)
{
    entt::meta_factory<audio_bus>{}
        .type("audio_bus"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "audio_bus"},
            entt::attribute{"pretty_name", "Audio Bus"},
        })
        .data<audio_bus::sfx>("sfx"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sfx"},
            entt::attribute{"pretty_name", "SFX"},
        })
        .data<audio_bus::music>("music"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "music"},
            entt::attribute{"pretty_name", "Music"},
        })
        .data<audio_bus::ui>("ui"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ui"},
            entt::attribute{"pretty_name", "UI"},
        });
}

REFLECT(audio_source_component)
{
    entt::meta_factory<audio_source_component>{}
        .type("audio_source_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "audio_source_component"},
            entt::attribute{"category", "AUDIO"},
            entt::attribute{"pretty_name", "Audio Source"},
        })
        .func<&component_meta<audio_source_component>::exists>("component_exists"_hs)
        .func<&component_meta<audio_source_component>::add>("component_add"_hs)
        .func<&component_meta<audio_source_component>::remove>("component_remove"_hs)
        .func<&component_meta<audio_source_component>::save>("component_save"_hs)
        .func<&component_meta<audio_source_component>::load>("component_load"_hs)
        .data<&audio_source_component::set_autoplay, &audio_source_component::get_autoplay>("auto_play"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto_play"},
            entt::attribute{"pretty_name", "Auto Play"},
        })
        .data<&audio_source_component::set_loop, &audio_source_component::is_looping>("loop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "loop"},
            entt::attribute{"pretty_name", "Loop"},
        })
        .data<&audio_source_component::set_mute, &audio_source_component::is_muted>("mute"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mute"},
            entt::attribute{"pretty_name", "Mute"},
        })
        .data<&audio_source_component::set_spatial, &audio_source_component::is_spatial>("spatial"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spatial"},
            entt::attribute{"pretty_name", "Spatial (3D)"},
            entt::attribute{"tooltip", "When off, audio is listener-relative (2D / UI / music)."},
        })
        .data<&audio_source_component::set_bus, &audio_source_component::get_bus>("bus"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bus"},
            entt::attribute{"pretty_name", "Bus"},
        })
        .data<&audio_source_component::set_priority, &audio_source_component::get_priority>("priority"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "priority"},
            entt::attribute{"pretty_name", "Priority"},
            entt::attribute{"tooltip", "Higher priority voices are kept when the voice cap is reached."},
        })
        .data<&audio_source_component::set_volume, &audio_source_component::get_volume>("volume"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volume"},
            entt::attribute{"pretty_name", "Volume"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&audio_source_component::set_pitch, &audio_source_component::get_pitch>("pitch"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pitch"},
            entt::attribute{"pretty_name", "Pitch"},
            entt::attribute{"tooltip", "A multiplier for the frequency (sample rate) of the source's buffer."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 5.0f},
        })
        .data<&audio_source_component::set_volume_rolloff, &audio_source_component::get_volume_rolloff>("volume_rolloff"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volume_rolloff"},
            entt::attribute{"pretty_name", "Volume Rolloff"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 10.0f},
        })
        .data<&audio_source_component::set_range, &audio_source_component::get_range>("range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "range"},
            entt::attribute{"pretty_name", "Range"},
        })
        .data<&audio_source_component::set_clip, &audio_source_component::get_clip>("clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clip"},
            entt::attribute{"pretty_name", "Clip"},
        });
}

SAVE(audio_source_component)
{
    try_save(ar, ser20::make_nvp("auto_play", obj.get_autoplay()));
    try_save(ar, ser20::make_nvp("loop", obj.is_looping()));
    try_save(ar, ser20::make_nvp("mute", obj.is_muted()));
    try_save(ar, ser20::make_nvp("spatial", obj.is_spatial()));
    try_save(ar, ser20::make_nvp("bus", obj.get_bus()));
    try_save(ar, ser20::make_nvp("priority", obj.get_priority()));
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

    bool mute{};
    if(try_load(ar, ser20::make_nvp("mute", mute)))
    {
        obj.set_mute(mute);
    }

    bool spatial{false};
    if(try_load(ar, ser20::make_nvp("spatial", spatial)))
    {
        obj.set_spatial(spatial);
    }

    audio_bus bus{audio_bus::sfx};
    if(try_load(ar, ser20::make_nvp("bus", bus)))
    {
        obj.set_bus(bus);
    }

    int priority{0};
    if(try_load(ar, ser20::make_nvp("priority", priority)))
    {
        obj.set_priority(priority);
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
