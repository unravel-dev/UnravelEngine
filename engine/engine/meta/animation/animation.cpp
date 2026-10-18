#include "animation.hpp"
#include <engine/meta/core/math/quaternion.hpp>
#include <engine/meta/core/math/transform.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <fstream>
#include <filesystem/file_istream.h>
#include <filesystem/pack.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <algorithm>

namespace unravel
{

REFLECT(root_motion_params)
{
    entt::meta_factory<root_motion_params>{}
        .type("root_motion_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "root_motion_params"},
            entt::attribute{"pretty_name", "Root Motion Params"},
        })
        .data<&root_motion_params::keep_position_y>("keep_position_y"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "keep_position_y"},
            entt::attribute{"pretty_name", "Keep Position Y"},
            entt::attribute{"tooltip", "Root position y component is not affected by animation."},
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
            entt::attribute{"tooltip", "Root rotation is not affected by animation."},
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

REFLECT(animation_event)
{
    entt::meta_factory<animation_event>{}
        .type("animation_event"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_event"},
            entt::attribute{"pretty_name", "Animation Event"},
        })
        .data<&animation_event::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
            entt::attribute{"pretty_name", "Name"},
            entt::attribute{"tooltip", "Name scripts receive in OnAnimationEvent. A public script method with this exact name is called too."},
        })
        .data<&animation_event::position>("position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "position"},
            entt::attribute{"pretty_name", "Position"},
            entt::attribute{"tooltip", "Normalized position on the clip: 0 = first frame, 1 = last frame."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&animation_event::string_parameter>("string_parameter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "string_parameter"},
            entt::attribute{"pretty_name", "String"},
        })
        .data<&animation_event::float_parameter>("float_parameter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "float_parameter"},
            entt::attribute{"pretty_name", "Float"},
        })
        .data<&animation_event::int_parameter>("int_parameter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "int_parameter"},
            entt::attribute{"pretty_name", "Int"},
        })
        .data<&animation_event::min_weight>("min_weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_weight"},
            entt::attribute{"pretty_name", "Min Weight"},
            entt::attribute{"tooltip", "Smallest share of the final pose (layer x crossfade weight) at which the event fires. 0.5 makes a crossfade report it from one side only; 0 fires whenever the clip plays at all."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        });
}

SERIALIZE(animation_event)
{
    try_serialize(ar, ser20::make_nvp("name", obj.name));
    try_serialize(ar, ser20::make_nvp("position", obj.position));
    try_serialize(ar, ser20::make_nvp("string_parameter", obj.string_parameter));
    try_serialize(ar, ser20::make_nvp("float_parameter", obj.float_parameter));
    try_serialize(ar, ser20::make_nvp("int_parameter", obj.int_parameter));
    try_serialize(ar, ser20::make_nvp("min_weight", obj.min_weight));
}
SERIALIZE_INSTANTIATE(animation_event, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_event, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(animation_event, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_event, ser20::iarchive_binary_t);

REFLECT(animation_curve)
{
    entt::meta_factory<animation_curve>{}
        .type("animation_curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_curve"},
            entt::attribute{"pretty_name", "Animation Curve"},
        })
        .data<&animation_curve::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
            entt::attribute{"pretty_name", "Name"},
            entt::attribute{"tooltip", "Scripts read the blended value with GetCurve(name). A controller float parameter of the same name follows it. IK components can scale their weight by it."},
        })
        .data<&animation_curve::curve>("curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "curve"},
            entt::attribute{"pretty_name", "Curve"},
            entt::attribute{"tooltip", "Value over the clip's normalized time (0 = first frame, 1 = last frame)."},
        });
}

REFLECT(humanoid_bone)
{
    entt::meta_factory<humanoid_bone>{}
        .type("humanoid_bone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "humanoid_bone"},
            entt::attribute{"pretty_name", "Humanoid Bone"},
        })
        .data<humanoid_bone::hips>("hips"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hips"},
            entt::attribute{"pretty_name", "Hips"},
        })
        .data<humanoid_bone::spine>("spine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spine"},
            entt::attribute{"pretty_name", "Spine"},
        })
        .data<humanoid_bone::chest>("chest"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "chest"},
            entt::attribute{"pretty_name", "Chest"},
        })
        .data<humanoid_bone::upper_chest>("upper_chest"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upper_chest"},
            entt::attribute{"pretty_name", "Upper Chest"},
        })
        .data<humanoid_bone::neck>("neck"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "neck"},
            entt::attribute{"pretty_name", "Neck"},
        })
        .data<humanoid_bone::head>("head"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "head"},
            entt::attribute{"pretty_name", "Head"},
        })
        .data<humanoid_bone::left_shoulder>("left_shoulder"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_shoulder"},
            entt::attribute{"pretty_name", "Left Shoulder"},
        })
        .data<humanoid_bone::left_upper_arm>("left_upper_arm"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_upper_arm"},
            entt::attribute{"pretty_name", "Left Upper Arm"},
        })
        .data<humanoid_bone::left_lower_arm>("left_lower_arm"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_lower_arm"},
            entt::attribute{"pretty_name", "Left Lower Arm"},
        })
        .data<humanoid_bone::left_hand>("left_hand"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_hand"},
            entt::attribute{"pretty_name", "Left Hand"},
        })
        .data<humanoid_bone::right_shoulder>("right_shoulder"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_shoulder"},
            entt::attribute{"pretty_name", "Right Shoulder"},
        })
        .data<humanoid_bone::right_upper_arm>("right_upper_arm"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_upper_arm"},
            entt::attribute{"pretty_name", "Right Upper Arm"},
        })
        .data<humanoid_bone::right_lower_arm>("right_lower_arm"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_lower_arm"},
            entt::attribute{"pretty_name", "Right Lower Arm"},
        })
        .data<humanoid_bone::right_hand>("right_hand"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_hand"},
            entt::attribute{"pretty_name", "Right Hand"},
        })
        .data<humanoid_bone::left_upper_leg>("left_upper_leg"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_upper_leg"},
            entt::attribute{"pretty_name", "Left Upper Leg"},
        })
        .data<humanoid_bone::left_lower_leg>("left_lower_leg"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_lower_leg"},
            entt::attribute{"pretty_name", "Left Lower Leg"},
        })
        .data<humanoid_bone::left_foot>("left_foot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_foot"},
            entt::attribute{"pretty_name", "Left Foot"},
        })
        .data<humanoid_bone::left_toes>("left_toes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "left_toes"},
            entt::attribute{"pretty_name", "Left Toes"},
        })
        .data<humanoid_bone::right_upper_leg>("right_upper_leg"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_upper_leg"},
            entt::attribute{"pretty_name", "Right Upper Leg"},
        })
        .data<humanoid_bone::right_lower_leg>("right_lower_leg"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_lower_leg"},
            entt::attribute{"pretty_name", "Right Lower Leg"},
        })
        .data<humanoid_bone::right_foot>("right_foot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_foot"},
            entt::attribute{"pretty_name", "Right Foot"},
        })
        .data<humanoid_bone::right_toes>("right_toes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "right_toes"},
            entt::attribute{"pretty_name", "Right Toes"},
        });
}

REFLECT(humanoid_bone_override)
{
    entt::meta_factory<humanoid_bone_override>{}
        .type("humanoid_bone_override"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "humanoid_bone_override"},
            entt::attribute{"pretty_name", "Humanoid Role"},
        })
        .data<&humanoid_bone_override::role>("role"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "role"},
            entt::attribute{"pretty_name", "Role"},
        })
        .data<&humanoid_bone_override::bone>("bone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone"},
            entt::attribute{"pretty_name", "Bone"},
            entt::attribute{"tooltip", "Name of the bone that plays the role. Empty takes the role away."},
        });
}

SERIALIZE(humanoid_bone_override)
{
    try_serialize(ar, ser20::make_nvp("role", obj.role));
    try_serialize(ar, ser20::make_nvp("bone", obj.bone));
}
SERIALIZE_INSTANTIATE(humanoid_bone_override, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(humanoid_bone_override, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(humanoid_bone_override, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(humanoid_bone_override, ser20::iarchive_binary_t);

REFLECT(humanoid_pose_offset)
{
    entt::meta_factory<humanoid_pose_offset>{}
        .type("humanoid_pose_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "humanoid_pose_offset"},
            entt::attribute{"pretty_name", "Retarget Pose Offset"},
        })
        .data<&humanoid_pose_offset::role>("role"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "role"},
            entt::attribute{"pretty_name", "Role"},
        })
        .data<&humanoid_pose_offset::rotation>("rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rotation"},
            entt::attribute{"pretty_name", "Rotation"},
            entt::attribute{"tooltip", "Degrees about the character's side axis (towards its left), up and forward. Right-side roles are mirrored: (0, 0, 20) on both upper arms raises both."},
        });
}

SERIALIZE(humanoid_pose_offset)
{
    try_serialize(ar, ser20::make_nvp("role", obj.role));
    try_serialize(ar, ser20::make_nvp("rotation", obj.rotation));
}
SERIALIZE_INSTANTIATE(humanoid_pose_offset, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(humanoid_pose_offset, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(humanoid_pose_offset, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(humanoid_pose_offset, ser20::iarchive_binary_t);

SAVE(animation_curve)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("curve", obj.curve));
}
SAVE_INSTANTIATE(animation_curve, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_curve, ser20::oarchive_binary_t);

LOAD(animation_curve)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("curve", obj.curve));
}
LOAD_INSTANTIATE(animation_curve, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_curve, ser20::iarchive_binary_t);

void sanitize_animation_events(std::vector<animation_event>& events)
{
    for(auto& event : events)
    {
        event.position = math::clamp(event.position, 0.0f, 1.0f);
        event.min_weight = math::clamp(event.min_weight, 0.0f, 1.0f);
    }
    std::stable_sort(events.begin(),
                     events.end(),
                     [](const animation_event& lhs, const animation_event& rhs)
                     {
                         return lhs.position < rhs.position;
                     });
}

void sanitize_animation_curves(std::vector<animation_curve>& curves)
{
    curves.erase(std::remove_if(curves.begin(),
                                curves.end(),
                                [](const animation_curve& curve)
                                {
                                    return curve.name.empty();
                                }),
                 curves.end());
}

REFLECT(animation_channel)
{
    // Register animation_channel with entt
    entt::meta_factory<animation_channel>{}
        .type("animation_channel"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_channel"},
            entt::attribute{"pretty_name", "Animation Channel"},
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
        .data<nullptr, &animation_channel::get_scaling_keys_count>("scaling_keys_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scaling_keys_count"},
            entt::attribute{"pretty_name", "Scalings"},
        });
}

REFLECT(animation_clip)
{
    entt::meta_factory<animation_clip>{}
        .type("animation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation"},
            entt::attribute{"pretty_name", "Animation"},
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
        })
        .data<nullptr, &animation_clip::events>("events"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "events"},
            entt::attribute{"pretty_name", "Events"},
        })
        .data<nullptr, &animation_clip::curves>("curves"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "curves"},
            entt::attribute{"pretty_name", "Curves"},
        });
}

SAVE(animation_channel)
{
    try_save(ar, ser20::make_nvp("node_name", obj.node_name));
    try_save(ar, ser20::make_nvp("node_index", obj.node_index));
    try_save(ar, ser20::make_nvp("position_keys", obj.position_keys));
    try_save(ar, ser20::make_nvp("rotation_keys", obj.rotation_keys));
    try_save(ar, ser20::make_nvp("scaling_keys", obj.scaling_keys));
    try_save(ar, ser20::make_nvp("has_rest", obj.has_rest));
    if(obj.has_rest)
    {
        try_save(ar, ser20::make_nvp("rest_local", obj.rest_local));
        try_save(ar, ser20::make_nvp("parent_channel", obj.parent_channel));
        try_save(ar, ser20::make_nvp("parent_offset", obj.parent_offset));
    }
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
    obj.has_rest = false;
    try_load(ar, ser20::make_nvp("has_rest", obj.has_rest));
    if(obj.has_rest)
    {
        try_load(ar, ser20::make_nvp("rest_local", obj.rest_local));
        try_load(ar, ser20::make_nvp("parent_channel", obj.parent_channel));
        try_load(ar, ser20::make_nvp("parent_offset", obj.parent_offset));
    }
}
LOAD_INSTANTIATE(animation_channel, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_channel, ser20::iarchive_binary_t);

SAVE(animation_clip)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("duration", obj.duration));
    try_save(ar, ser20::make_nvp("channels", obj.channels));
    try_save(ar, ser20::make_nvp("root_motion", obj.root_motion));
    try_save(ar, ser20::make_nvp("events", obj.events));
    try_save(ar, ser20::make_nvp("curves", obj.curves));
    // Fields added later go last: binary archives read by position.
    try_save(ar, ser20::make_nvp("humanoid_overrides", obj.humanoid_overrides));
    
    // Changes here should be reflected in ex::get_format_version<animation_clip>() in asset_extensions.h
}
SAVE_INSTANTIATE(animation_clip, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_clip, ser20::oarchive_binary_t);

LOAD(animation_clip)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("duration", obj.duration));
    try_load(ar, ser20::make_nvp("channels", obj.channels));
    try_load(ar, ser20::make_nvp("root_motion", obj.root_motion));
    try_load(ar, ser20::make_nvp("events", obj.events));
    try_load(ar, ser20::make_nvp("curves", obj.curves));
    try_load(ar, ser20::make_nvp("humanoid_overrides", obj.humanoid_overrides));
    
    // Changes here should be reflected in ex::get_format_version<animation_clip>() in asset_extensions.h
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
    fs::file_istream input(absolute_path);
    if(!input.is_open())
    {
        return;
    }
    auto ar = ser20::create_iarchive_associative(input);
    try_load(ar, ser20::make_nvp("animation", obj));
}

void load_from_file_bin(const std::string& absolute_path, animation_clip& obj)
{
    auto input = fs::open_readable_stream(absolute_path);
    if(!input)
    {
        return;
    }
    ser20::iarchive_binary_t ar(*input);
    try_load(ar, ser20::make_nvp("animation", obj));
}
} // namespace unravel
