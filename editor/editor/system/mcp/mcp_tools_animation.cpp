#include "mcp_asset_lookup.h"
#include "mcp_meta_properties.h"
#include "mcp_tools_common.h"

#include <editor/editing/animation_retarget_check.h>
#include <editor/system/mcp_manager.h>
#include <engine/animation/animation_controller_instance.h>
#include <engine/animation/animation_humanoid.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace unravel::mcp
{
namespace
{
/// Crossfade length of animation_set_parameters' state switch when none is given, in seconds.
constexpr double DEFAULT_CROSS_FADE_SECONDS = 0.2;

auto json_float(float value) -> std::string
{
    return std::isfinite(value) ? fmt::format("{:.4f}", value) : std::string("0");
}

/// A reflected enum value as its reflected name in JSON: the spelling assets_set_properties_batch takes.
template<typename Enum>
auto enum_name_json(rtti::context& ctx, Enum value) -> std::string
{
    entt::meta_any any{value};
    const auto json = meta_value_to_json(ctx, any);
    return json ? *json : std::string("null");
}

/// Clips of a player state with their weights, as JSON.
auto state_clips_json(const animation_state& state) -> std::string
{
    std::string json = "[";
    const auto add = [&](const asset_handle<animation_clip>& clip, float weight)
    {
        if(json.size() > 1)
        {
            json += ",";
        }
        json += fmt::format(R"({{"clip":{},"weight":{}}})", make_json_string(clip ? clip.id() : std::string{}), json_float(weight));
    };
    if(state.blend_space)
    {
        for(const auto& clip_weight : state.blend_clips)
        {
            add(clip_weight.clip, clip_weight.weight);
        }
    }
    else if(state.clip)
    {
        add(state.clip, 1.0f);
    }
    return json + "]";
}

/// Curve names of every loaded clip the player plays, by id, so curve values can be reported by name.
void collect_curve_names(const animation_state& state, std::unordered_map<uint32_t, std::string>& names)
{
    const auto add = [&](const asset_handle<animation_clip>& handle)
    {
        auto clip = handle.try_get();
        if(!clip || asset_handle<animation_clip>::is_empty(clip))
        {
            return;
        }
        for(const auto& curve : clip->curves)
        {
            names[get_animation_curve_id(curve.name)] = curve.name;
        }
    };
    if(state.blend_space)
    {
        for(const auto& clip_weight : state.blend_clips)
        {
            add(clip_weight.clip);
        }
    }
    else if(state.clip)
    {
        add(state.clip);
    }
}

auto describe_animation(rtti::context& ctx, const animation_component& animation) -> std::string
{
    const auto& player = animation.get_player();
    const auto& layers = player.get_layers();
    std::unordered_map<uint32_t, std::string> curve_names;

    std::string player_layers = "[";
    for(size_t i = 0; i < layers.size(); ++i)
    {
        const auto& layer = layers[i];
        collect_curve_names(layer.current_state.state, curve_names);
        collect_curve_names(layer.target_state.state, curve_names);
        player_layers += fmt::format(
            R"({}{{"index":{},"weight":{},"fade":{},"mode":{},"current":{{"progress":{},"loops":{},"speed":{},"clips":{}}},"crossfade":{}}})",
            i > 0 ? "," : "",
            i,
            json_float(player.get_layer_weight(i)),
            json_float(layer.motion_weight),
            enum_name_json(ctx, layer.blend_mode),
            json_float(layer.current_state.state.get_progress()),
            layer.current_state.state.loop_count,
            json_float(layer.current_state.state.speed),
            state_clips_json(layer.current_state.state),
            layer.target_state.is_valid()
                ? fmt::format(R"({{"progress":{},"weight":{},"target_clips":{}}})",
                              json_float(player.get_blend_progress(layer)),
                              json_float(player.get_blend_weight(layer)),
                              state_clips_json(layer.target_state.state))
                : std::string("null"));
    }
    player_layers += "]";

    std::string curves = "[";
    for(const auto& value : player.get_curve_values())
    {
        auto it = curve_names.find(value.id);
        curves += fmt::format(R"({}{{"name":{},"value":{}}})",
                              curves.size() > 1 ? "," : "",
                              make_json_string(it != curve_names.end() ? it->second : std::to_string(value.id)),
                              json_float(value.value));
    }
    curves += "]";

    // The controller binds when the entity first plays (play mode or a preview); a read reports it, never binds it.
    std::string controller_json = "null";
    const auto* instance = animation.get_controller_instance();
    if(animation.get_controller() && instance == nullptr)
    {
        controller_json = fmt::format(R"({{"asset":{},"bound":false}})", make_json_string(animation.get_controller().id()));
    }
    if(instance != nullptr)
    {
        const auto& definition = *instance->get_controller();
        std::string parameters = "[";
        const auto& values = instance->get_parameter_values();
        for(size_t i = 0; i < definition.parameters.size() && i < values.size(); ++i)
        {
            const auto& parameter = definition.parameters[i];
            std::string value;
            switch(parameter.type)
            {
                case animation_parameter_type::float_value:
                    value = json_float(values[i].float_value);
                    break;
                case animation_parameter_type::int_value:
                    value = std::to_string(values[i].int_value);
                    break;
                case animation_parameter_type::bool_value:
                case animation_parameter_type::trigger:
                    value = bool_to_json(values[i].bool_value);
                    break;
            }
            parameters += fmt::format(R"({}{{"name":{},"type":{},"value":{}}})",
                                      i > 0 ? "," : "",
                                      make_json_string(parameter.name),
                                      enum_name_json(ctx, parameter.type),
                                      value);
        }
        parameters += "]";

        std::string controller_layers = "[";
        for(size_t i = 0; i < instance->get_layer_count(); ++i)
        {
            const auto status = instance->get_layer_status(i, player);
            const auto& layer_def = definition.layers[i];
            const auto state_name = [&](int state)
            {
                return state >= 0 ? make_json_string(instance->get_state_name(i, state)) : std::string("null");
            };
            std::string transition = "null";
            if(status.transition >= 0 && size_t(status.transition) < layer_def.transitions.size())
            {
                const auto& def = layer_def.transitions[size_t(status.transition)];
                transition = fmt::format(R"({{"index":{},"source":{},"destination":{},"progress":{}}})",
                                         status.transition,
                                         make_json_string(def.source.empty() ? std::string("Any State") : def.source),
                                         make_json_string(def.destination),
                                         json_float(status.transition_progress));
            }
            std::string motion = "null";
            if(status.current_state >= 0 && size_t(status.current_state) < layer_def.states.size())
            {
                motion = enum_name_json(ctx, layer_def.states[size_t(status.current_state)].motion);
            }
            controller_layers += fmt::format(
                R"({}{{"index":{},"name":{},"state":{},"motion":{},"state_time":{},"next_state":{},"next_state_time":{},"transition":{}}})",
                i > 0 ? "," : "",
                i,
                make_json_string(layer_def.name),
                state_name(status.current_state),
                motion,
                json_float(status.state_time),
                state_name(status.next_state),
                json_float(status.next_state_time),
                transition);
        }
        controller_layers += "]";
        controller_json = fmt::format(R"({{"asset":{},"bound":true,"parameters":{},"layers":{}}})",
                                      make_json_string(animation.get_controller().id()),
                                      parameters,
                                      controller_layers);
    }

    return fmt::format(R"({{"playing":{},"paused":{},"speed":{},"root_motion":{},"controller":{},"player_layers":{},"curves":{}}})",
                       bool_to_json(player.is_playing()),
                       bool_to_json(player.is_paused()),
                       json_float(animation.get_speed()),
                       bool_to_json(animation.get_apply_root_motion()),
                       controller_json,
                       player_layers,
                       curves);
}

auto resolve_animation(rtti::context& ctx,
                       const simdjson::dom::object& args,
                       entt::handle& entity,
                       std::string& error) -> animation_component*
{
    scene* scn = nullptr;
    std::string id;
    if(!require_active_scene(ctx, scn, error))
    {
        return nullptr;
    }
    if(!read_string(args, "entity_id", id))
    {
        error = "entity_id is required";
        return nullptr;
    }
    entity = resolve_entity(*scn, id, error);
    if(!entity)
    {
        return nullptr;
    }
    auto* animation = entity.try_get<animation_component>();
    if(animation == nullptr)
    {
        error = "Entity " + id + " has no Animation component";
    }
    return animation;
}

/// What animation_set_parameters is asked to do.
struct parameter_request
{
    simdjson::dom::object values;
    bool has_values{};
    std::vector<std::string> triggers;
    std::vector<std::string> reset_triggers;
    std::string state;
    size_t layer{};
    double duration{DEFAULT_CROSS_FADE_SECONDS};
    double normalized_time{};
};

/// Reads an optional number argument; false with `error` set when it is given but not a finite number in range.
auto read_number_in_range(const simdjson::dom::object& args,
                          const char* key,
                          double min_value,
                          double max_value,
                          const char* expected,
                          double& out,
                          std::string& error) -> bool
{
    simdjson::dom::element element;
    if(args[key].get(element) != simdjson::SUCCESS)
    {
        return true;
    }
    double value = 0.0;
    if(element.get_double().get(value) != simdjson::SUCCESS || !std::isfinite(value) || value < min_value ||
       value > max_value)
    {
        error = std::string(key) + " must be " + expected;
        return false;
    }
    out = value;
    return true;
}

/// Reads and checks every argument of animation_set_parameters against a controller of `layer_count` layers.
auto read_parameter_request(const simdjson::dom::object& args,
                            size_t layer_count,
                            parameter_request& out,
                            std::string& error) -> bool
{
    simdjson::dom::element element;
    if(args["values"].get(element) == simdjson::SUCCESS)
    {
        if(element.get_object().get(out.values) != simdjson::SUCCESS)
        {
            error = "values must be an object of parameter names to values";
            return false;
        }
        out.has_values = true;
    }
    if(!read_string_array(args, "triggers", out.triggers, error, false) ||
       !read_string_array(args, "reset_triggers", out.reset_triggers, error, false))
    {
        return false;
    }
    if(args["state"].get(element) == simdjson::SUCCESS && !read_string(args, "state", out.state))
    {
        error = "state must be a state name";
        return false;
    }
    if(args["layer"].get(element) == simdjson::SUCCESS)
    {
        int64_t layer = 0;
        if(element.get_int64().get(layer) != simdjson::SUCCESS || layer < 0 || uint64_t(layer) >= layer_count)
        {
            error = fmt::format("layer must be an integer below {}, the controller's layer count", layer_count);
            return false;
        }
        out.layer = size_t(layer);
    }
    return read_number_in_range(args,
                                "duration",
                                0.0,
                                double(std::numeric_limits<float>::max()),
                                "a number of seconds, 0 or more",
                                out.duration,
                                error) &&
           read_number_in_range(args, "normalized_time", 0.0, 1.0, "a number from 0 to 1", out.normalized_time, error);
}

/// A bone of the check's skeleton as a JSON string, or null for none.
auto retarget_bone_json(const animation_retarget_check& check, int bone) -> std::string
{
    const bool is_named = check.skeleton && bone >= 0 && size_t(bone) < check.skeleton->names.size();
    return is_named ? make_json_string(check.skeleton->names[size_t(bone)]) : std::string("null");
}

/// A root motion channel and the bone it moves, as JSON; null when the clip has none.
auto retarget_root_json(const animation_retarget_check& check, int channel, int bone) -> std::string
{
    if(channel < 0 || size_t(channel) >= check.report.channels.size())
    {
        return "null";
    }
    return fmt::format(R"({{"channel":{},"bone":{}}})",
                       make_json_string(check.report.channels[size_t(channel)].name),
                       retarget_bone_json(check, bone));
}

/// The channels of a check as JSON: every channel when `lists_all`, else only those reaching no bone (as names).
auto retarget_channels_json(const animation_retarget_check& check, bool lists_all) -> std::string
{
    std::string json = "[";
    for(const auto& channel : check.report.channels)
    {
        const bool is_matched = channel.match != animation_binding_report::match_kind::none;
        if(!lists_all && is_matched)
        {
            continue;
        }
        json += json.size() > 1 ? "," : "";
        json += lists_all ? fmt::format(R"({{"channel":{},"bone":{},"match":{}}})",
                                        make_json_string(channel.name),
                                        retarget_bone_json(check, channel.bone),
                                        make_json_string(get_match_kind_name(channel.match)))
                          : make_json_string(channel.name);
    }
    return json + "]";
}

auto describe_retarget_check(rtti::context& ctx,
                             const animation_component& animation,
                             const std::string& clip_key,
                             const animation_retarget_check& check,
                             bool lists_all_channels) -> std::string
{
    const auto& report = check.report;
    std::string matches = "{";
    for(size_t kind = 0; kind < RETARGET_MATCH_KIND_COUNT; ++kind)
    {
        matches += fmt::format(R"({}{}:{})",
                               kind > 0 ? "," : "",
                               make_json_string(get_match_kind_name(animation_binding_report::match_kind(kind))),
                               check.match_counts[kind]);
    }
    matches += "}";
    std::vector<std::string> unreached;
    for(const int bone : report.unreached_bones)
    {
        unreached.push_back(get_retarget_bone_name(check, bone));
    }
    std::string rest = "[";
    for(const auto& difference : report.rest_differences)
    {
        rest += fmt::format(R"({}{{"role":{},"degrees":{}}})",
                            rest.size() > 1 ? "," : "",
                            make_json_string(get_humanoid_bone_name(difference.role)),
                            json_float(difference.degrees));
    }
    rest += "]";
    return fmt::format(
        R"({{"clip":{},"retargeting_mode":{},"pose_retargeting":{},"rest_aligned":{},"clip_has_rest":{},)"
        R"("channel_count":{},"matched":{},"matches":{},"{}":{},"unreached_bones":{},"height_ratio":{},)"
        R"("frame_turn_degrees":{},"frame_turn_axis":[{},{},{}],"root_motion":{{"position":{},"rotation":{}}},)"
        R"("rest_mismatches":{},"rest_differences":{},"warnings":{}}})",
        make_json_string(clip_key),
        enum_name_json(ctx, animation.get_retargeting_mode()),
        enum_name_json(ctx, animation.get_pose_retargeting()),
        bool_to_json(report.is_rest_aligned),
        bool_to_json(report.has_source_rest),
        report.channels.size(),
        count_matched_channels(check),
        matches,
        lists_all_channels ? "channels" : "unmatched_channels",
        retarget_channels_json(check, lists_all_channels),
        strings_to_json_array(unreached),
        json_float(report.height_ratio),
        json_float(report.frame_degrees),
        json_float(report.frame_axis.x),
        json_float(report.frame_axis.y),
        json_float(report.frame_axis.z),
        retarget_root_json(check, report.root_position_channel, report.root_position_bone),
        retarget_root_json(check, report.root_rotation_channel, report.root_rotation_bone),
        report.rest_mismatches,
        rest,
        strings_to_json_array(check.warnings));
}

} // namespace

void register_animation_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "animation_controller_validate",
         .description =
             "Check an animation controller asset (.actrl; key or uid) for problems that make part of it "
             "unusable at runtime: duplicate or empty names, transitions to unknown states, conditions on "
             "unknown or mistyped parameters, blend states without points or axes, transitions that can never "
             "fire. Edit controllers with assets_get_properties_batch / assets_set_properties_batch (type "
             "animation_controller); this reports what is left to fix, with per-layer state counts.",
         .input_schema_json = R"json({"type":"object","properties":{"asset":{"type":"string"}},"required":["asset"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string reference;
             std::string error;
             if(!read_string(args, "asset", reference))
             {
                 return {.text = "asset is required", .is_error = true};
             }
             auto handle = find_existing_asset<animation_controller>(ctx, reference, error);
             if(!handle)
             {
                 return {.text = error.empty() ? "Animation controller not found: " + reference : error, .is_error = true};
             }
             const auto loaded = handle.get();
             if(!loaded || asset_handle<animation_controller>::is_empty(loaded))
             {
                 return {.text = "The animation controller could not be loaded: " + handle.id(), .is_error = true};
             }
             const auto& controller = *loaded;
             const auto problems = validate_animation_controller(controller);
             std::string layers = "[";
             for(size_t i = 0; i < controller.layers.size(); ++i)
             {
                 const auto& layer = controller.layers[i];
                 layers += fmt::format(R"({}{{"name":{},"states":{},"transitions":{},"default_state":{}}})",
                                       i > 0 ? "," : "",
                                       make_json_string(layer.name),
                                       layer.states.size(),
                                       layer.transitions.size(),
                                       make_json_string(layer.default_state));
             }
             layers += "]";
             return {.text = fmt::format(R"({{"asset":{},"valid":{},"problems":{},"parameters":{},"layers":{}}})",
                                         make_json_string(handle.id()),
                                         bool_to_json(problems.empty()),
                                         strings_to_json_array(problems),
                                         controller.parameters.size(),
                                         layers),
                     .is_error = false};
         },
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "animation_get_state",
         .description =
             "Runtime animation state of an entity's Animation component in the active scene (the play scene while "
             "playing): playing/paused, every player layer (weight, fade, blend mode, clips with weights, progress, "
             "running crossfade), blended curve values, and - with a controller - every parameter value and per "
             "layer the current state, its normalized time, the destination and progress of a running transition. "
             "Enum values (blend mode, parameter type, motion) use their reflected names, as "
             "assets_set_properties_batch does. A controller binds when the entity first plays (play mode or a "
             "preview); before that it reports bound false. Use it to verify a state machine reacts to parameters.",
         .input_schema_json = R"json({"type":"object","properties":{"entity_id":{"type":"string"}},"required":["entity_id"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             entt::handle entity;
             auto* animation = resolve_animation(ctx, args, entity, error);
             if(animation == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             return {.text = describe_animation(ctx, *animation), .is_error = false};
         },
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "animation_get_humanoid",
         .description =
             "The humanoid roles (hips, spine, chest, upper chest, neck, head, shoulders, arms, hands, legs, feet, "
             "toes) Rest Aligned retargeting matches bones by when their names differ (Mixamo, Rigify, VRM, "
             "Biped...): with entity_id, the bones of the entity's model (the Animation inspector's Humanoid "
             "Mapping); with clip (an animation clip key), the bones of the skeleton the clip was authored on. Each "
             "role maps to a bone name or null; a clip imported before rest poses were recorded reports none.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"clip":{"type":"string"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             humanoid_map roles = make_empty_humanoid_map();
             std::vector<std::string> names;
             std::string key;
             std::string entity_reference;
             if(!read_string(args, "clip", key) && !read_string(args, "entity_id", entity_reference))
             {
                 return {.text = "entity_id or clip is required", .is_error = true};
             }
             if(!key.empty())
             {
                 std::string error;
                 const auto handle = find_existing_asset<animation_clip>(ctx, key, error);
                 auto clip = handle ? handle.get() : nullptr;
                 if(!clip || asset_handle<animation_clip>::is_empty(clip))
                 {
                     return {.text = error.empty() ? "No animation clip at " + key : error, .is_error = true};
                 }
                 roles = find_clip_humanoid_bones(*clip);
                 for(const auto& channel : clip->channels)
                 {
                     names.push_back(channel.node_name);
                 }
             }
             else
             {
                 std::string error;
                 entt::handle entity;
                 auto* animation = resolve_animation(ctx, args, entity, error);
                 if(animation == nullptr)
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto* model = entity.try_get<model_component>();
                 if(model == nullptr)
                 {
                     return {.text = "The entity has no Model component", .is_error = true};
                 }
                 if(const auto& skeleton = animation_system::get_target_skeleton(*animation, *model))
                 {
                     roles = skeleton->humanoid;
                     names = skeleton->names;
                 }
             }
             std::string json = "{";
             for(size_t role = 0; role < HUMANOID_BONE_COUNT; ++role)
             {
                 const int bone = roles[role];
                 const bool named = bone >= 0 && size_t(bone) < names.size();
                 json += fmt::format(R"({}{}:{})",
                                     role > 0 ? "," : "",
                                     make_json_string(get_humanoid_bone_name(humanoid_bone(role))),
                                     named ? make_json_string(names[size_t(bone)]) : std::string("null"));
             }
             json += "}";
             return {.text = fmt::format(R"({{"found":{},"of":{},"roles":{}}})",
                                         count_humanoid_bones(roles),
                                         HUMANOID_BONE_COUNT,
                                         json),
                     .is_error = false};
         },
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "animation_get_retarget",
         .description =
             "How an animation clip lands on an entity's model with its Animation component's retargeting "
             "settings - the Animation inspector's Retarget Check. clip is an animation clip key; without it, the "
             "clip the base layer plays, else the clip the controller starts in, else the component's Animation "
             "clip. Reports whether it plays Rest "
             "Aligned or copied, how many channels reach a bone and how (index, name, name without namespace, "
             "role), the channels that reach none (or every channel with channels true), the bones no channel "
             "reaches (they keep the bind pose), height ratio and frame turn, which bone the root motion moves, "
             "rest pose differences per limb, and warnings in words: wrong settings, a different rest pose "
             "(T-pose against A-pose), unmatched roles, lost root motion. Check it after assigning clips from "
             "another skeleton.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"clip":{"type":"string"},"channels":{"type":"boolean"}},"required":["entity_id"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             entt::handle entity;
             auto* animation = resolve_animation(ctx, args, entity, error);
             if(animation == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             const auto* model = entity.try_get<model_component>();
             if(model == nullptr)
             {
                 return {.text = "The entity has no Model component", .is_error = true};
             }
             std::string key;
             asset_handle<animation_clip> handle;
             if(read_string(args, "clip", key))
             {
                 handle = find_existing_asset<animation_clip>(ctx, key, error);
             }
             else
             {
                 handle = get_default_retarget_clip(*animation, true);
             }
             const auto clip = handle ? handle.get() : nullptr;
             if(!clip || asset_handle<animation_clip>::is_empty(clip))
             {
                 const std::string missing = key.empty() ? "The component plays no clip, its controller starts in none and it has none assigned; pass clip"
                                                         : "No animation clip at " + key;
                 return {.text = error.empty() ? missing : error, .is_error = true};
             }
             if(!animation_system::get_target_skeleton(*animation, *model))
             {
                 return {.text = "The model has no skeleton to retarget onto (no armature yet)", .is_error = true};
             }
             bool lists_all_channels = false;
             read_bool(args, "channels", lists_all_channels);
             const auto check = check_animation_retarget(*animation, *model, clip);
             return {.text = describe_retarget_check(ctx, *animation, handle.id(), check, lists_all_channels),
                     .is_error = false};
         },
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "animation_set_parameters",
         .description =
             "Drive an entity's animation controller at runtime (play mode, or the controller editor's preview): "
             "values sets parameters by name (numbers for float/int, true/false for bool), triggers sets trigger "
             "parameters, reset_triggers clears them, and state (+ optional layer, duration seconds, "
             "normalized_time 0..1) crossfades a layer straight to a state. A malformed argument fails the call "
             "before anything changes. Returns the new animation state, like animation_get_state. Not undoable: "
             "it changes the running simulation, not the scene.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"values":{"type":"object"},"triggers":{"type":"array","items":{"type":"string"}},"reset_triggers":{"type":"array","items":{"type":"string"}},"state":{"type":"string"},"layer":{"type":"integer","minimum":0},"duration":{"type":"number","minimum":0},"normalized_time":{"type":"number","minimum":0,"maximum":1}},"required":["entity_id"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             entt::handle entity;
             auto* animation = resolve_animation(ctx, args, entity, error);
             if(animation == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             auto* instance = animation->get_controller_instance();
             if(instance == nullptr)
             {
                 return {.text = "The Animation component has no controller", .is_error = true};
             }
             // Everything is resolved before anything changes, so a malformed request changes nothing.
             parameter_request request;
             if(!read_parameter_request(args, instance->get_layer_count(), request, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto& definition = instance->get_controller();
             const auto find_parameter = [&](const std::string& name) -> const animation_parameter_def*
             {
                 const int index = instance->get_parameter_index(name);
                 return index >= 0 && definition && size_t(index) < definition->parameters.size()
                            ? &definition->parameters[size_t(index)]
                            : nullptr;
             };
             std::vector<std::pair<size_t, float>> assignments;
             std::vector<std::string> unknown;
             std::vector<std::string> mistyped;
             if(request.has_values)
             {
                 for(auto field : request.values)
                 {
                     const std::string name(field.key);
                     const auto* parameter = find_parameter(name);
                     if(parameter == nullptr)
                     {
                         unknown.push_back(name);
                         continue;
                     }
                     const auto type = parameter->type;
                     const bool is_number = type == animation_parameter_type::float_value ||
                                            type == animation_parameter_type::int_value;
                     const size_t index = size_t(instance->get_parameter_index(name));
                     bool flag = false;
                     double number = 0.0;
                     if(field.value.get_bool().get(flag) == simdjson::SUCCESS && type == animation_parameter_type::bool_value)
                     {
                         assignments.emplace_back(index, flag ? 1.0f : 0.0f);
                     }
                     else if(field.value.get_double().get(number) == simdjson::SUCCESS && is_number)
                     {
                         assignments.emplace_back(index, float(number));
                     }
                     else
                     {
                         // A bool takes true / false, a float or an int a number; triggers go in triggers.
                         mistyped.push_back(name);
                     }
                 }
             }
             for(const auto* names : {&request.triggers, &request.reset_triggers})
             {
                 for(const auto& name : *names)
                 {
                     const auto* parameter = find_parameter(name);
                     if(parameter == nullptr)
                     {
                         unknown.push_back(name);
                     }
                     else if(parameter->type != animation_parameter_type::trigger)
                     {
                         mistyped.push_back(name);
                     }
                 }
             }
             const bool has_state = request.state.empty() ||
                                    (definition && request.layer < definition->layers.size() &&
                                     find_animation_state(definition->layers[request.layer], request.state) >= 0);
             if(!unknown.empty() || !mistyped.empty() || !has_state)
             {
                 return {.text = fmt::format(R"({{"applied":false,"unknown_parameters":{},"mistyped_parameters":{},"unknown_state":{}}})",
                                             strings_to_json_array(unknown),
                                             strings_to_json_array(mistyped),
                                             has_state ? std::string("null") : make_json_string(request.state)),
                         .is_error = true};
             }
             for(const auto& [index, value] : assignments)
             {
                 instance->set_parameter_from_float(index, value);
             }
             for(const auto& name : request.triggers)
             {
                 instance->set_trigger(name);
             }
             for(const auto& name : request.reset_triggers)
             {
                 instance->reset_trigger(name);
             }
             if(!request.state.empty())
             {
                 instance->cross_fade(request.layer, request.state, float(request.duration), float(request.normalized_time));
             }
             return {.text = fmt::format(R"({{"applied":true,"state":{}}})", describe_animation(ctx, *animation)),
                     .is_error = false};
         },
         .effect = tool_effect::changes});
}

} // namespace unravel::mcp
