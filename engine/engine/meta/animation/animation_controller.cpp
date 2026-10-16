#include "animation_controller.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/rendering/mesh.hpp>

#include <filesystem/file_istream.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <fstream>
#include <sstream>

namespace unravel
{

REFLECT(animation_layer_blend_mode)
{
    entt::meta_factory<animation_layer_blend_mode>{}
        .type("animation_layer_blend_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_layer_blend_mode"},
            entt::attribute{"pretty_name", "Layer Blend Mode"},
        })
        .data<animation_layer_blend_mode::override_pose>("override_pose"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "override_pose"},
            entt::attribute{"pretty_name", "Override"},
        })
        .data<animation_layer_blend_mode::additive>("additive"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "additive"},
            entt::attribute{"pretty_name", "Additive"},
        });
}

REFLECT(animation_parameter_type)
{
    entt::meta_factory<animation_parameter_type>{}
        .type("animation_parameter_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_parameter_type"},
            entt::attribute{"pretty_name", "Parameter Type"},
        })
        .data<animation_parameter_type::float_value>("float_value"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "float_value"},
            entt::attribute{"pretty_name", "Float"},
        })
        .data<animation_parameter_type::int_value>("int_value"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "int_value"},
            entt::attribute{"pretty_name", "Int"},
        })
        .data<animation_parameter_type::bool_value>("bool_value"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bool_value"},
            entt::attribute{"pretty_name", "Bool"},
        })
        .data<animation_parameter_type::trigger>("trigger"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "trigger"},
            entt::attribute{"pretty_name", "Trigger"},
        });
}

REFLECT(animation_condition_mode)
{
    entt::meta_factory<animation_condition_mode>{}
        .type("animation_condition_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_condition_mode"},
            entt::attribute{"pretty_name", "Condition Mode"},
        })
        .data<animation_condition_mode::greater>("greater"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "greater"},
            entt::attribute{"pretty_name", "Greater"},
        })
        .data<animation_condition_mode::less>("less"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "less"},
            entt::attribute{"pretty_name", "Less"},
        })
        .data<animation_condition_mode::equals>("equals"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "equals"},
            entt::attribute{"pretty_name", "Equals"},
        })
        .data<animation_condition_mode::not_equals>("not_equals"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "not_equals"},
            entt::attribute{"pretty_name", "Not Equal"},
        })
        .data<animation_condition_mode::is_true>("is_true"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_true"},
            entt::attribute{"pretty_name", "True"},
        })
        .data<animation_condition_mode::is_false>("is_false"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_false"},
            entt::attribute{"pretty_name", "False"},
        });
}

REFLECT(animation_motion_type)
{
    entt::meta_factory<animation_motion_type>{}
        .type("animation_motion_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_motion_type"},
            entt::attribute{"pretty_name", "Motion"},
        })
        .data<animation_motion_type::clip>("clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clip"},
            entt::attribute{"pretty_name", "Clip"},
        })
        .data<animation_motion_type::blend_1d>("blend_1d"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_1d"},
            entt::attribute{"pretty_name", "Blend 1D"},
        })
        .data<animation_motion_type::blend_2d_cartesian>("blend_2d_cartesian"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_2d_cartesian"},
            entt::attribute{"pretty_name", "Blend 2D Cartesian"},
        })
        .data<animation_motion_type::blend_2d_directional>("blend_2d_directional"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_2d_directional"},
            entt::attribute{"pretty_name", "Blend 2D Directional"},
        });
}

REFLECT(animation_parameter_def)
{
    // Only the default of the parameter's own type is shown; a trigger always starts unset.
    const auto is_type = [](animation_parameter_type type)
    {
        return entt::property_predicate<bool>(
            [type](const entt::meta_any& object)
            {
                const auto* parameter = object.try_cast<animation_parameter_def>();
                return parameter != nullptr && parameter->type == type;
            });
    };

    entt::meta_factory<animation_parameter_def>{}
        .type("animation_parameter_def"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_parameter_def"},
            entt::attribute{"pretty_name", "Parameter"},
        })
        .data<&animation_parameter_def::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
            entt::attribute{"pretty_name", "Name"},
        })
        .data<&animation_parameter_def::type>("type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "type"},
            entt::attribute{"pretty_name", "Type"},
            entt::attribute{"tooltip", "Float and Int drive blends and compare with thresholds; Bool is on or off; a "
                                       "Trigger is set once and consumed by the transition it fires."},
        })
        .data<&animation_parameter_def::default_float>("default_float"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "default_float"},
            entt::attribute{"pretty_name", "Default"},
            entt::attribute{"tooltip", "Value the parameter starts with."},
            entt::attribute{"predicate", is_type(animation_parameter_type::float_value)},
        })
        .data<&animation_parameter_def::default_int>("default_int"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "default_int"},
            entt::attribute{"pretty_name", "Default"},
            entt::attribute{"tooltip", "Value the parameter starts with."},
            entt::attribute{"predicate", is_type(animation_parameter_type::int_value)},
        })
        .data<&animation_parameter_def::default_bool>("default_bool"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "default_bool"},
            entt::attribute{"pretty_name", "Default"},
            entt::attribute{"tooltip", "Value the parameter starts with."},
            entt::attribute{"predicate", is_type(animation_parameter_type::bool_value)},
        });
}

SERIALIZE(animation_parameter_def)
{
    try_serialize(ar, ser20::make_nvp("name", obj.name));
    try_serialize(ar, ser20::make_nvp("type", obj.type));
    try_serialize(ar, ser20::make_nvp("default_float", obj.default_float));
    try_serialize(ar, ser20::make_nvp("default_int", obj.default_int));
    try_serialize(ar, ser20::make_nvp("default_bool", obj.default_bool));
}
SERIALIZE_INSTANTIATE(animation_parameter_def, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_parameter_def, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(animation_parameter_def, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_parameter_def, ser20::iarchive_binary_t);

REFLECT(animation_condition_def)
{
    entt::meta_factory<animation_condition_def>{}
        .type("animation_condition_def"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_condition_def"},
            entt::attribute{"pretty_name", "Condition"},
        })
        .data<&animation_condition_def::parameter>("parameter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "parameter"},
            entt::attribute{"pretty_name", "Parameter"},
        })
        .data<&animation_condition_def::mode>("mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mode"},
            entt::attribute{"pretty_name", "Mode"},
        })
        .data<&animation_condition_def::threshold>("threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "threshold"},
            entt::attribute{"pretty_name", "Threshold"},
        });
}

SERIALIZE(animation_condition_def)
{
    try_serialize(ar, ser20::make_nvp("parameter", obj.parameter));
    try_serialize(ar, ser20::make_nvp("mode", obj.mode));
    try_serialize(ar, ser20::make_nvp("threshold", obj.threshold));
}
SERIALIZE_INSTANTIATE(animation_condition_def, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_condition_def, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(animation_condition_def, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_condition_def, ser20::iarchive_binary_t);

REFLECT(animation_blend_point_def)
{
    entt::meta_factory<animation_blend_point_def>{}
        .type("animation_blend_point_def"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_blend_point_def"},
            entt::attribute{"pretty_name", "Blend Point"},
        })
        .data<&animation_blend_point_def::clip>("clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clip"},
            entt::attribute{"pretty_name", "Clip"},
        })
        .data<&animation_blend_point_def::position>("position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "position"},
            entt::attribute{"pretty_name", "Position"},
            entt::attribute{"tooltip", "Place on the blend axes; a 1D blend uses X only."},
        });
}

SAVE(animation_blend_point_def)
{
    try_save(ar, ser20::make_nvp("clip", obj.clip));
    try_save(ar, ser20::make_nvp("position", obj.position));
}
SAVE_INSTANTIATE(animation_blend_point_def, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_blend_point_def, ser20::oarchive_binary_t);

LOAD(animation_blend_point_def)
{
    try_load(ar, ser20::make_nvp("clip", obj.clip));
    try_load(ar, ser20::make_nvp("position", obj.position));
}
LOAD_INSTANTIATE(animation_blend_point_def, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_blend_point_def, ser20::iarchive_binary_t);

REFLECT(animation_state_def)
{
    const auto plays = [](auto&& is_shown)
    {
        return entt::property_predicate<bool>(
            [is_shown](const entt::meta_any& object)
            {
                const auto* state = object.try_cast<animation_state_def>();
                return state != nullptr && is_shown(state->motion);
            });
    };
    const auto clip_motion = plays(
        [](animation_motion_type motion)
        {
            return motion == animation_motion_type::clip;
        });
    const auto blend_motion = plays(
        [](animation_motion_type motion)
        {
            return motion != animation_motion_type::clip;
        });
    const auto blend_2d_motion = plays(
        [](animation_motion_type motion)
        {
            return motion == animation_motion_type::blend_2d_cartesian || motion == animation_motion_type::blend_2d_directional;
        });

    entt::meta_factory<animation_state_def>{}
        .type("animation_state_def"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_state_def"},
            entt::attribute{"pretty_name", "State"},
        })
        .data<&animation_state_def::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
            entt::attribute{"pretty_name", "Name"},
        })
        .data<&animation_state_def::motion>("motion"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "motion"},
            entt::attribute{"pretty_name", "Motion"},
            entt::attribute{"tooltip", "A clip, or a blend of clips placed on one or two parameter axes."},
        })
        .data<&animation_state_def::clip>("clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clip"},
            entt::attribute{"pretty_name", "Clip"},
            entt::attribute{"predicate", clip_motion},
        })
        .data<&animation_state_def::parameter_x>("parameter_x"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "parameter_x"},
            entt::attribute{"pretty_name", "Parameter X"},
            entt::attribute{"tooltip", "Float or int parameter the blend reads along X."},
            entt::attribute{"group", "Blend"},
            entt::attribute{"predicate", blend_motion},
        })
        .data<&animation_state_def::parameter_y>("parameter_y"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "parameter_y"},
            entt::attribute{"pretty_name", "Parameter Y"},
            entt::attribute{"tooltip", "Float or int parameter the blend reads along Y."},
            entt::attribute{"group", "Blend"},
            entt::attribute{"predicate", blend_2d_motion},
        })
        .data<&animation_state_def::points>("points"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "points"},
            entt::attribute{"pretty_name", "Blend Points"},
            entt::attribute{"element_label", "Point"},
            entt::attribute{"group", "Blend"},
            entt::attribute{"predicate", blend_motion},
        })
        .data<&animation_state_def::speed>("speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "speed"},
            entt::attribute{"pretty_name", "Speed"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Playback"},
        })
        .data<&animation_state_def::speed_parameter>("speed_parameter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "speed_parameter"},
            entt::attribute{"pretty_name", "Speed Parameter"},
            entt::attribute{"tooltip", "Optional float parameter the speed is multiplied by."},
            entt::attribute{"group", "Playback"},
        })
        .data<&animation_state_def::loop>("loop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "loop"},
            entt::attribute{"pretty_name", "Loop"},
            entt::attribute{"tooltip", "Play the motion in cycles; off holds its last frame."},
            entt::attribute{"group", "Playback"},
        })
        .data<&animation_state_def::cycle_offset>("cycle_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cycle_offset"},
            entt::attribute{"pretty_name", "Cycle Offset"},
            entt::attribute{"tooltip", "Normalized time the state starts at when entered without phase sync."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Playback"},
        })
        .data<&animation_state_def::tag>("tag"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tag"},
            entt::attribute{"pretty_name", "Tag"},
            entt::attribute{"tooltip", "Scripts ask whether a layer is in a tagged state (IsInTag)."},
            entt::attribute{"group", "Playback"},
        })
        .data<&animation_state_def::editor_position>("editor_position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "editor_position"},
            entt::attribute{"pretty_name", "Graph Position"},
        });
}

SAVE(animation_state_def)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("motion", obj.motion));
    try_save(ar, ser20::make_nvp("clip", obj.clip));
    try_save(ar, ser20::make_nvp("parameter_x", obj.parameter_x));
    try_save(ar, ser20::make_nvp("parameter_y", obj.parameter_y));
    try_save(ar, ser20::make_nvp("points", obj.points));
    try_save(ar, ser20::make_nvp("speed", obj.speed));
    try_save(ar, ser20::make_nvp("speed_parameter", obj.speed_parameter));
    try_save(ar, ser20::make_nvp("loop", obj.loop));
    try_save(ar, ser20::make_nvp("cycle_offset", obj.cycle_offset));
    try_save(ar, ser20::make_nvp("tag", obj.tag));
    try_save(ar, ser20::make_nvp("editor_position", obj.editor_position));
}
SAVE_INSTANTIATE(animation_state_def, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_state_def, ser20::oarchive_binary_t);

LOAD(animation_state_def)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("motion", obj.motion));
    try_load(ar, ser20::make_nvp("clip", obj.clip));
    try_load(ar, ser20::make_nvp("parameter_x", obj.parameter_x));
    try_load(ar, ser20::make_nvp("parameter_y", obj.parameter_y));
    try_load(ar, ser20::make_nvp("points", obj.points));
    try_load(ar, ser20::make_nvp("speed", obj.speed));
    try_load(ar, ser20::make_nvp("speed_parameter", obj.speed_parameter));
    try_load(ar, ser20::make_nvp("loop", obj.loop));
    try_load(ar, ser20::make_nvp("cycle_offset", obj.cycle_offset));
    try_load(ar, ser20::make_nvp("tag", obj.tag));
    try_load(ar, ser20::make_nvp("editor_position", obj.editor_position));
}
LOAD_INSTANTIATE(animation_state_def, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_state_def, ser20::iarchive_binary_t);

REFLECT(animation_transition_def)
{
    const auto without_exit_time = entt::property_predicate<bool>(
        [](const entt::meta_any& object)
        {
            const auto* transition = object.try_cast<animation_transition_def>();
            return transition != nullptr && !transition->has_exit_time;
        });
    const auto from_any_state = entt::property_predicate<bool>(
        [](const entt::meta_any& object)
        {
            const auto* transition = object.try_cast<animation_transition_def>();
            return transition != nullptr && transition->source.empty();
        });

    entt::meta_factory<animation_transition_def>{}
        .type("animation_transition_def"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_transition_def"},
            entt::attribute{"pretty_name", "Transition"},
        })
        .data<&animation_transition_def::source>("source"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "source"},
            entt::attribute{"pretty_name", "Source"},
            entt::attribute{"tooltip", "Source state; empty = Any State."},
        })
        .data<&animation_transition_def::destination>("destination"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "destination"},
            entt::attribute{"pretty_name", "Destination"},
        })
        .data<&animation_transition_def::conditions>("conditions"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "conditions"},
            entt::attribute{"pretty_name", "Conditions"},
            entt::attribute{"tooltip", "All of them must hold for the transition to fire."},
            entt::attribute{"element_label", "Condition"},
        })
        .data<&animation_transition_def::has_exit_time>("has_exit_time"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "has_exit_time"},
            entt::attribute{"pretty_name", "Has Exit Time"},
            entt::attribute{"tooltip", "Fire only once the source has played to Exit Time (and the conditions hold)."},
            entt::attribute{"group", "Timing"},
        })
        .data<&animation_transition_def::exit_time>("exit_time"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exit_time"},
            entt::attribute{"pretty_name", "Exit Time"},
            entt::attribute{"tooltip", "Normalized source time; above 1 counts whole loops."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Timing"},
            entt::attribute{"readonly_predicate", without_exit_time},
        })
        .data<&animation_transition_def::duration>("duration"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "duration"},
            entt::attribute{"pretty_name", "Duration (s)"},
            entt::attribute{"tooltip", "Crossfade length in seconds."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Timing"},
        })
        .data<&animation_transition_def::offset>("offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "offset"},
            entt::attribute{"pretty_name", "Offset"},
            entt::attribute{"tooltip", "Normalized time the destination starts at."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Timing"},
        })
        .data<&animation_transition_def::phase_sync>("phase_sync"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "phase_sync"},
            entt::attribute{"pretty_name", "Phase Sync"},
            entt::attribute{"tooltip", "Start the destination at the source's normalized phase (locomotion cycles)."},
            entt::attribute{"group", "Timing"},
        })
        .data<&animation_transition_def::interruptible>("interruptible"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "interruptible"},
            entt::attribute{"pretty_name", "Interruptible"},
            entt::attribute{"tooltip", "Transitions of the destination (and Any State) may cut this crossfade short."},
            entt::attribute{"group", "Timing"},
        })
        .data<&animation_transition_def::can_transition_to_self>("can_transition_to_self"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "can_transition_to_self"},
            entt::attribute{"pretty_name", "Can Transition To Self"},
            entt::attribute{"tooltip", "An Any State transition may restart the state it leads to."},
            entt::attribute{"group", "Timing"},
            entt::attribute{"predicate", from_any_state},
        });
}

SERIALIZE(animation_transition_def)
{
    try_serialize(ar, ser20::make_nvp("source", obj.source));
    try_serialize(ar, ser20::make_nvp("destination", obj.destination));
    try_serialize(ar, ser20::make_nvp("conditions", obj.conditions));
    try_serialize(ar, ser20::make_nvp("has_exit_time", obj.has_exit_time));
    try_serialize(ar, ser20::make_nvp("exit_time", obj.exit_time));
    try_serialize(ar, ser20::make_nvp("duration", obj.duration));
    try_serialize(ar, ser20::make_nvp("offset", obj.offset));
    try_serialize(ar, ser20::make_nvp("phase_sync", obj.phase_sync));
    try_serialize(ar, ser20::make_nvp("interruptible", obj.interruptible));
    try_serialize(ar, ser20::make_nvp("can_transition_to_self", obj.can_transition_to_self));
}
SERIALIZE_INSTANTIATE(animation_transition_def, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_transition_def, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(animation_transition_def, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_transition_def, ser20::iarchive_binary_t);

REFLECT(animation_mask_entry)
{
    entt::meta_factory<animation_mask_entry>{}
        .type("animation_mask_entry"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_mask_entry"},
            entt::attribute{"pretty_name", "Mask Entry"},
        })
        .data<&animation_mask_entry::bone>("bone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone"},
            entt::attribute{"pretty_name", "Bone"},
        })
        .data<&animation_mask_entry::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&animation_mask_entry::include_children>("include_children"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "include_children"},
            entt::attribute{"pretty_name", "Include Children"},
        });
}

SERIALIZE(animation_mask_entry)
{
    try_serialize(ar, ser20::make_nvp("bone", obj.bone));
    try_serialize(ar, ser20::make_nvp("weight", obj.weight));
    try_serialize(ar, ser20::make_nvp("include_children", obj.include_children));
}
SERIALIZE_INSTANTIATE(animation_mask_entry, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_mask_entry, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(animation_mask_entry, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(animation_mask_entry, ser20::iarchive_binary_t);

REFLECT(animation_layer_def)
{
    entt::meta_factory<animation_layer_def>{}
        .type("animation_layer_def"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_layer_def"},
            entt::attribute{"pretty_name", "Layer"},
        })
        .data<&animation_layer_def::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
            entt::attribute{"pretty_name", "Name"},
        })
        .data<&animation_layer_def::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"tooltip", "Starting weight; scripts change it with SetLayerWeight."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&animation_layer_def::blend_mode>("blend_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_mode"},
            entt::attribute{"pretty_name", "Blend Mode"},
            entt::attribute{"tooltip", "Override replaces the pose below; Additive adds its clips' change from their "
                                       "first frame."},
        })
        .data<&animation_layer_def::mask>("mask"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mask"},
            entt::attribute{"pretty_name", "Mask"},
            entt::attribute{"tooltip", "Bones the layer affects; empty = every bone."},
            entt::attribute{"element_label", "Bone"},
        })
        .data<&animation_layer_def::default_state>("default_state"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "default_state"},
            entt::attribute{"pretty_name", "Default State"},
        })
        .data<&animation_layer_def::states>("states"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "states"},
            entt::attribute{"pretty_name", "States"},
        })
        .data<&animation_layer_def::transitions>("transitions"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "transitions"},
            entt::attribute{"pretty_name", "Transitions"},
        })
        .data<&animation_layer_def::entry_position>("entry_position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "entry_position"},
            entt::attribute{"pretty_name", "Entry Position"},
        })
        .data<&animation_layer_def::any_state_position>("any_state_position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "any_state_position"},
            entt::attribute{"pretty_name", "Any State Position"},
        });
}

SAVE(animation_layer_def)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("weight", obj.weight));
    try_save(ar, ser20::make_nvp("blend_mode", obj.blend_mode));
    try_save(ar, ser20::make_nvp("mask", obj.mask));
    try_save(ar, ser20::make_nvp("default_state", obj.default_state));
    try_save(ar, ser20::make_nvp("states", obj.states));
    try_save(ar, ser20::make_nvp("transitions", obj.transitions));
    try_save(ar, ser20::make_nvp("entry_position", obj.entry_position));
    try_save(ar, ser20::make_nvp("any_state_position", obj.any_state_position));
}
SAVE_INSTANTIATE(animation_layer_def, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_layer_def, ser20::oarchive_binary_t);

LOAD(animation_layer_def)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("weight", obj.weight));
    try_load(ar, ser20::make_nvp("blend_mode", obj.blend_mode));
    try_load(ar, ser20::make_nvp("mask", obj.mask));
    try_load(ar, ser20::make_nvp("default_state", obj.default_state));
    try_load(ar, ser20::make_nvp("states", obj.states));
    try_load(ar, ser20::make_nvp("transitions", obj.transitions));
    try_load(ar, ser20::make_nvp("entry_position", obj.entry_position));
    try_load(ar, ser20::make_nvp("any_state_position", obj.any_state_position));
}
LOAD_INSTANTIATE(animation_layer_def, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_layer_def, ser20::iarchive_binary_t);

REFLECT(animation_controller)
{
    entt::meta_factory<animation_controller>{}
        .type("animation_controller"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animation_controller"},
            entt::attribute{"pretty_name", "Animation Controller"},
        })
        .data<&animation_controller::parameters>("parameters"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "parameters"},
            entt::attribute{"pretty_name", "Parameters"},
        })
        .data<&animation_controller::layers>("layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "layers"},
            entt::attribute{"pretty_name", "Layers"},
        })
        .data<&animation_controller::preview_model>("preview_model"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "preview_model"},
            entt::attribute{"pretty_name", "Preview Model"},
            entt::attribute{"tooltip", "Editor only: the model whose skeleton masks are authored against."},
        });
}

SAVE(animation_controller)
{
    try_save(ar, ser20::make_nvp("parameters", obj.parameters));
    try_save(ar, ser20::make_nvp("layers", obj.layers));
    try_save(ar, ser20::make_nvp("preview_model", obj.preview_model));
}
SAVE_INSTANTIATE(animation_controller, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_controller, ser20::oarchive_binary_t);

LOAD(animation_controller)
{
    try_load(ar, ser20::make_nvp("parameters", obj.parameters));
    try_load(ar, ser20::make_nvp("layers", obj.layers));
    try_load(ar, ser20::make_nvp("preview_model", obj.preview_model));
}
LOAD_INSTANTIATE(animation_controller, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_controller, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const animation_controller::sptr& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("animation_controller", *obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const animation_controller::sptr& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("animation_controller", *obj));
    }
}

void load_from_file(const std::string& absolute_path, animation_controller::sptr& obj)
{
    fs::file_istream input(absolute_path);
    if(!input.is_open())
    {
        return;
    }
    auto ar = ser20::create_iarchive_associative(input);
    try_load(ar, ser20::make_nvp("animation_controller", *obj));
}

void load_from_file_bin(const std::string& absolute_path, animation_controller::sptr& obj)
{
    fs::file_istream input(absolute_path, std::ios::binary);
    if(!input.is_open())
    {
        return;
    }
    ser20::iarchive_binary_t ar(input);
    try_load(ar, ser20::make_nvp("animation_controller", *obj));
}

auto save_to_string(const animation_controller& obj) -> std::string
{
    std::stringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("animation_controller", obj));
    }
    return stream.str();
}

auto load_from_string(const std::string& text, animation_controller& obj) -> bool
{
    std::stringstream stream(text);
    try
    {
        auto ar = ser20::create_iarchive_associative(stream);
        return try_load(ar, ser20::make_nvp("animation_controller", obj));
    }
    catch(const std::exception&)
    {
        return false;
    }
}

} // namespace unravel
