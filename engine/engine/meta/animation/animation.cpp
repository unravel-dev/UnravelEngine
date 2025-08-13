#include "animation.hpp"
#include <engine/meta/core/math/quaternion.hpp>
#include <engine/meta/core/math/transform.hpp>

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(root_motion_params)
{
    rttr::registration::class_<root_motion_params>("root_motion_params")
        .constructor<>()()
        .property("keep_position_y", &root_motion_params::keep_position_y)(
            rttr::metadata("pretty_name", "Keep Position Y"),
            rttr::metadata("tooltip", "Root position t components are not affected by animation."))
        .property("keep_position_xz", &root_motion_params::keep_position_xz)(
            rttr::metadata("pretty_name", "Keep Position XZ"),
            rttr::metadata("tooltip", "Root position x,z components are not affected by animation."))
        .property("keep_rotation", &root_motion_params::keep_rotation)(
            rttr::metadata("pretty_name", "Keep Rotation"),
            rttr::metadata("tooltip", "Root rotation is not affected by animaation."))
        .property("keep_in_place", &root_motion_params::keep_in_place)(
            rttr::metadata("pretty_name", "Keep In Place"),
            rttr::metadata("tooltip", "Keep the animation in place even if it has root motion in it."))
        .property_readonly("position_node_name", &root_motion_params::position_node_name)(
            rttr::metadata("pretty_name", "Root Motion Position Node"),
            rttr::metadata("tooltip", "Transform node that will be used for root motion."))
        .property_readonly("rotation_node_name", &root_motion_params::rotation_node_name)(
            rttr::metadata("pretty_name", "Root Motion Rotation Node"),
            rttr::metadata("tooltip", "Rotation node that will be used for root motion."));

    // Register root_motion_params with entt
    entt::meta_factory<root_motion_params>{}
        .type("root_motion_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "root_motion_params"},
        })
        .data<&root_motion_params::keep_position_y>("keep_position_y"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "keep_position_y"},
            entt::attribute{"pretty_name", "Keep Position Y"},
            entt::attribute{"tooltip", "Root position t components are not affected by animation."},
        })
        .data<&root_motion_params::keep_position_xz>("keep_position_xz"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "keep_position_xz"},
            entt::attribute{"pretty_name", "Keep Position XZ"},
            entt::attribute{"tooltip", "Root position x,z components are not affected by animation."},
        })
        .data<&root_motion_params::keep_rotation>("keep_rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "keep_rotation"},
            entt::attribute{"pretty_name", "Keep Rotation"},
            entt::attribute{"tooltip", "Root rotation is not affected by animaation."},
        })
        .data<&root_motion_params::keep_in_place>("keep_in_place"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "keep_in_place"},
            entt::attribute{"pretty_name", "Keep In Place"},
            entt::attribute{"tooltip", "Keep the animation in place even if it has root motion in it."},
        })
        .data<nullptr, &root_motion_params::position_node_name>("position_node_name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "position_node_name"},
            entt::attribute{"pretty_name", "Root Motion Position Node"},
            entt::attribute{"tooltip", "Transform node that will be used for root motion."},
        })
        .data<nullptr, &root_motion_params::rotation_node_name>("rotation_node_name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rotation_node_name"},
            entt::attribute{"pretty_name", "Root Motion Rotation Node"},
            entt::attribute{"tooltip", "Rotation node that will be used for root motion."},
        });
}

SERIALIZE(root_motion_params)
{
    try_serialize(ar, ser20::make_nvp("keep_position_y", obj.keep_position_y));
    try_serialize(ar, ser20::make_nvp("keep_position_xz", obj.keep_position_xz));
    try_serialize(ar, ser20::make_nvp("keep_rotation", obj.keep_rotation));
    try_serialize(ar, ser20::make_nvp("keep_in_place", obj.keep_in_place));

    try_serialize(ar, ser20::make_nvp("position_node_name", obj.position_node_name));
    try_serialize(ar, ser20::make_nvp("position_node_index", obj.position_node_index));
    try_serialize(ar, ser20::make_nvp("rotation_node_name", obj.rotation_node_name));
    try_serialize(ar, ser20::make_nvp("rotation_node_index", obj.rotation_node_index));

}
SERIALIZE_INSTANTIATE(root_motion_params, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(root_motion_params, ser20::oarchive_binary_t);

REFLECT(animation_channel)
{
    rttr::registration::class_<animation_channel>("animation_channel")
        .property_readonly("node_name", &animation_channel::node_name)(rttr::metadata("pretty_name", "Name"))
        .property_readonly("position_keys_count", &animation_channel::get_position_keys_count)(rttr::metadata("pretty_name", "Positions"))
        .property_readonly("rotation_keys_count", &animation_channel::get_rotation_keys_count)(rttr::metadata("pretty_name", "Rotations"))
        .property_readonly("scaling_keys_count", &animation_channel::get_position_keys_count)(rttr::metadata("pretty_name", "Scalings"));

    // Register animation_channel with entt
    entt::meta_factory<animation_channel>{}
        .type("animation_channel"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_channel"},
        })
        .data<nullptr, &animation_channel::node_name>("node_name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "node_name"},
            entt::attribute{"pretty_name", "Name"},
        })
        .data<nullptr, &animation_channel::get_position_keys_count>("position_keys_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "position_keys_count"},
            entt::attribute{"pretty_name", "Positions"},
        })
        .data<nullptr, &animation_channel::get_rotation_keys_count>("rotation_keys_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rotation_keys_count"},
            entt::attribute{"pretty_name", "Rotations"},
        })
        .data<nullptr, &animation_channel::get_position_keys_count>("scaling_keys_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scaling_keys_count"},
            entt::attribute{"pretty_name", "Scalings"},
        });
}

REFLECT(animation_clip)
{
    rttr::registration::class_<animation_clip>("animation")
        .property_readonly("name", &animation_clip::name)(rttr::metadata("pretty_name", "Name"))
        .property_readonly("duration", &animation_clip::duration)(rttr::metadata("pretty_name", "Duration"))
        .property_readonly("root_motion", &animation_clip::root_motion)(rttr::metadata("pretty_name", "Root Motion"))
        .property_readonly("channels", &animation_clip::channels)(rttr::metadata("pretty_name", "Channels"));

    // Register animation_clip with entt
    entt::meta_factory<animation_clip>{}
        .type("animation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation"},
        })
        .data<nullptr, &animation_clip::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
            entt::attribute{"pretty_name", "Name"},
        })
        .data<nullptr, &animation_clip::duration>("duration"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "duration"},
            entt::attribute{"pretty_name", "Duration"},
        })
        .data<nullptr, &animation_clip::root_motion>("root_motion"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "root_motion"},
            entt::attribute{"pretty_name", "Root Motion"},
        })
        .data<nullptr, &animation_clip::channels>("channels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "channels"},
            entt::attribute{"pretty_name", "Channels"},
        });
}

SAVE(animation_channel)
{
    try_save(ar, ser20::make_nvp("node_name", obj.node_name));
    try_save(ar, ser20::make_nvp("node_index", obj.node_index));
    try_save(ar, ser20::make_nvp("position_keys", obj.position_keys));
    try_save(ar, ser20::make_nvp("rotation_keys", obj.rotation_keys));
    try_save(ar, ser20::make_nvp("scaling_keys", obj.scaling_keys));
}
SAVE_INSTANTIATE(animation_channel, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_channel, ser20::oarchive_binary_t);

LOAD(animation_channel)
{
    try_load(ar, ser20::make_nvp("node_name", obj.node_name));
    try_load(ar, ser20::make_nvp("node_index", obj.node_index));
    try_load(ar, ser20::make_nvp("position_keys", obj.position_keys));
    try_load(ar, ser20::make_nvp("rotation_keys", obj.rotation_keys));
    try_load(ar, ser20::make_nvp("scaling_keys", obj.scaling_keys));
}
LOAD_INSTANTIATE(animation_channel, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_channel, ser20::iarchive_binary_t);

SAVE(animation_clip)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("duration", obj.duration));
    try_save(ar, ser20::make_nvp("channels", obj.channels));
    try_save(ar, ser20::make_nvp("root_motion", obj.root_motion));
}
SAVE_INSTANTIATE(animation_clip, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_clip, ser20::oarchive_binary_t);

LOAD(animation_clip)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("duration", obj.duration));
    try_load(ar, ser20::make_nvp("channels", obj.channels));
    try_load(ar, ser20::make_nvp("root_motion", obj.root_motion));
}
LOAD_INSTANTIATE(animation_clip, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_clip, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const animation_clip& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("animation", obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const animation_clip& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("animation", obj));
    }
}

void load_from_file(const std::string& absolute_path, animation_clip& obj)
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_iarchive_associative(stream);
        try_load(ar, ser20::make_nvp("animation", obj));
    }
}

void load_from_file_bin(const std::string& absolute_path, animation_clip& obj)
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("animation", obj));
    }
}
} // namespace unravel
