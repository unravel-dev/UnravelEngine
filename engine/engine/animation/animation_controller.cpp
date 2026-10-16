#include "animation_controller.h"

#include <logging/logging.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace unravel
{

namespace
{
auto is_numeric(animation_parameter_type type) -> bool
{
    return type == animation_parameter_type::float_value || type == animation_parameter_type::int_value;
}

auto is_boolean(animation_parameter_type type) -> bool
{
    return type == animation_parameter_type::bool_value || type == animation_parameter_type::trigger;
}

auto is_condition_compatible(animation_condition_mode mode, animation_parameter_type type) -> bool
{
    switch(mode)
    {
        case animation_condition_mode::greater:
        case animation_condition_mode::less:
        case animation_condition_mode::equals:
        case animation_condition_mode::not_equals:
            return is_numeric(type);
        case animation_condition_mode::is_true:
            return is_boolean(type);
        case animation_condition_mode::is_false:
            return type == animation_parameter_type::bool_value;
    }
    return false;
}

/// Index of the item whose name matches, or -1.
template<typename Items>
auto find_by_name(const Items& items, hpp::string_view name) -> int
{
    const auto it = std::find_if(items.begin(),
                                 items.end(),
                                 [&](const auto& item)
                                 {
                                     return item.name == name;
                                 });
    return it == items.end() ? -1 : int(it - items.begin());
}

/// Whether one of the transition's conditions is on a trigger, which firing the transition consumes.
auto has_trigger_condition(const animation_controller& controller, const animation_transition_def& transition) -> bool
{
    return std::any_of(transition.conditions.begin(),
                       transition.conditions.end(),
                       [&](const animation_condition_def& condition)
                       {
                           const int index = controller.find_parameter(condition.parameter);
                           return index >= 0 &&
                                  controller.parameters[size_t(index)].type == animation_parameter_type::trigger;
                       });
}

auto describe_source(const animation_transition_def& transition) -> std::string
{
    return transition.source.empty() ? std::string("Any State") : transition.source;
}

void validate_float_parameter(const animation_controller& controller,
                              const std::string& name,
                              const std::string& where,
                              std::vector<std::string>& problems)
{
    if(name.empty())
    {
        return;
    }
    const int index = controller.find_parameter(name);
    if(index < 0)
    {
        problems.push_back(fmt::format("{} uses unknown parameter '{}'", where, name));
        return;
    }
    if(!is_numeric(controller.parameters[size_t(index)].type))
    {
        problems.push_back(fmt::format("{} uses parameter '{}', which is not a float or int", where, name));
    }
}

void validate_state(const animation_controller& controller,
                    const animation_state_def& state,
                    const std::string& where,
                    bool is_base,
                    std::vector<std::string>& problems)
{
    switch(state.motion)
    {
        case animation_motion_type::clip:
            // An empty state on a layer above the base is how a layer rests: it fades out and
            // leaves the layers below it alone. The base layer has nothing below it.
            if(!state.clip && is_base)
            {
                problems.push_back(fmt::format("{} has no clip", where));
            }
            break;
        case animation_motion_type::blend_1d:
        case animation_motion_type::blend_2d_cartesian:
        case animation_motion_type::blend_2d_directional:
        {
            if(state.points.empty())
            {
                problems.push_back(fmt::format("{} has no blend points", where));
            }
            for(const auto& point : state.points)
            {
                if(!point.clip)
                {
                    problems.push_back(fmt::format("{} has a blend point without a clip", where));
                    break;
                }
            }
            if(state.parameter_x.empty())
            {
                problems.push_back(fmt::format("{} has no X parameter", where));
            }
            validate_float_parameter(controller, state.parameter_x, where, problems);
            if(state.motion != animation_motion_type::blend_1d)
            {
                if(state.parameter_y.empty())
                {
                    problems.push_back(fmt::format("{} has no Y parameter", where));
                }
                validate_float_parameter(controller, state.parameter_y, where, problems);
            }
            break;
        }
    }
    validate_float_parameter(controller, state.speed_parameter, where, problems);
}

void validate_transition(const animation_controller& controller,
                         const animation_layer_def& layer,
                         const animation_transition_def& transition,
                         const std::string& layer_label,
                         std::vector<std::string>& problems)
{
    const std::string where =
        fmt::format("{} transition {} -> {}", layer_label, describe_source(transition), transition.destination);
    const int source = transition.source.empty() ? -1 : find_animation_state(layer, transition.source);
    if(!transition.source.empty() && source < 0)
    {
        problems.push_back(fmt::format("{}: unknown source state", where));
    }
    if(find_animation_state(layer, transition.destination) < 0)
    {
        problems.push_back(fmt::format("{}: unknown destination state", where));
    }
    if(!transition.has_exit_time && transition.conditions.empty())
    {
        problems.push_back(fmt::format("{}: no conditions and no exit time, it can never fire", where));
    }
    if(transition.has_exit_time && transition.exit_time > 1.0f && source >= 0 && !layer.states[size_t(source)].loop)
    {
        // A state that plays once stops at its end: its time never passes 1.
        problems.push_back(fmt::format("{}: exit time above 1 on a state that does not loop, it can never fire", where));
    }
    if(transition.source.empty() && transition.can_transition_to_self && !has_trigger_condition(controller, transition))
    {
        problems.push_back(fmt::format(
            "{}: Any State to itself without a trigger restarts the state on every frame its conditions hold", where));
    }
    if(!transition.source.empty() && transition.source == transition.destination && !transition.has_exit_time &&
       !has_trigger_condition(controller, transition))
    {
        problems.push_back(fmt::format(
            "{}: a state to itself without an exit time or a trigger restarts it on every frame its conditions hold",
            where));
    }
    for(const auto& condition : transition.conditions)
    {
        const int index = controller.find_parameter(condition.parameter);
        if(index < 0)
        {
            problems.push_back(fmt::format("{}: condition on unknown parameter '{}'", where, condition.parameter));
            continue;
        }
        if(!is_condition_compatible(condition.mode, controller.parameters[size_t(index)].type))
        {
            problems.push_back(fmt::format("{}: condition mode does not suit parameter '{}'", where, condition.parameter));
        }
    }
}

void validate_layer_settings(const animation_layer_def& layer,
                             const std::string& layer_label,
                             bool is_base,
                             std::vector<std::string>& problems)
{
    if(is_base)
    {
        // The base layer has nothing below it to blend with or mask against.
        if(!layer.mask.empty())
        {
            problems.push_back(fmt::format("{} is the base layer: its bone mask is ignored", layer_label));
        }
        if(layer.weight != 1.0f)
        {
            problems.push_back(fmt::format("{} is the base layer: it always plays at full weight", layer_label));
        }
        if(layer.blend_mode == animation_layer_blend_mode::additive)
        {
            problems.push_back(fmt::format("{} is the base layer: it cannot be additive", layer_label));
        }
    }
    std::unordered_set<std::string> mask_bones;
    for(const auto& entry : layer.mask)
    {
        if(!mask_bones.insert(entry.bone).second)
        {
            problems.push_back(fmt::format("{} masks bone '{}' twice; the last entry counts", layer_label, entry.bone));
        }
    }
}

void validate_layer(const animation_controller& controller,
                    const animation_layer_def& layer,
                    bool is_base,
                    std::vector<std::string>& problems)
{
    const std::string layer_label = fmt::format("layer '{}'", layer.name);
    if(layer.states.empty())
    {
        problems.push_back(fmt::format("{} has no states", layer_label));
    }
    validate_layer_settings(layer, layer_label, is_base, problems);
    std::unordered_set<std::string> state_names;
    for(const auto& state : layer.states)
    {
        if(state.name.empty())
        {
            problems.push_back(fmt::format("{} has a state without a name", layer_label));
        }
        else if(!state_names.insert(state.name).second)
        {
            problems.push_back(fmt::format("{} has two states named '{}'", layer_label, state.name));
        }
        validate_state(controller, state, fmt::format("{} state '{}'", layer_label, state.name), is_base, problems);
    }

    if(!layer.default_state.empty() && find_animation_state(layer, layer.default_state) < 0)
    {
        problems.push_back(fmt::format("{} default state '{}' does not exist", layer_label, layer.default_state));
    }

    for(const auto& transition : layer.transitions)
    {
        validate_transition(controller, layer, transition, layer_label, problems);
    }
}
} // namespace

auto animation_controller::find_parameter(hpp::string_view name) const -> int
{
    return find_by_name(parameters, name);
}

auto animation_controller::find_layer(hpp::string_view name) const -> int
{
    return find_by_name(layers, name);
}

auto find_animation_state(const animation_layer_def& layer, hpp::string_view name) -> int
{
    return find_by_name(layer.states, name);
}

auto validate_animation_controller(const animation_controller& controller) -> std::vector<std::string>
{
    std::vector<std::string> problems;
    std::unordered_set<std::string> parameter_names;
    for(const auto& parameter : controller.parameters)
    {
        if(parameter.name.empty())
        {
            problems.push_back("a parameter has no name");
        }
        else if(!parameter_names.insert(parameter.name).second)
        {
            problems.push_back(fmt::format("two parameters are named '{}'", parameter.name));
        }
    }
    if(controller.layers.empty())
    {
        problems.push_back("the controller has no layers");
    }
    std::unordered_set<std::string> layer_names;
    for(size_t index = 0; index < controller.layers.size(); ++index)
    {
        const auto& layer = controller.layers[index];
        if(layer.name.empty())
        {
            // Running controllers keep each layer's state by name across edits.
            problems.push_back("a layer has no name");
        }
        else if(!layer_names.insert(layer.name).second)
        {
            problems.push_back(fmt::format("two layers are named '{}'", layer.name));
        }
        validate_layer(controller, layer, index == 0, problems);
    }
    return problems;
}

void rename_animation_state(animation_layer_def& layer, size_t state_index, const std::string& new_name)
{
    if(state_index >= layer.states.size())
    {
        return;
    }
    const std::string old_name = layer.states[state_index].name;
    layer.states[state_index].name = new_name;
    if(layer.default_state == old_name)
    {
        layer.default_state = new_name;
    }
    for(auto& transition : layer.transitions)
    {
        if(transition.source == old_name)
        {
            transition.source = new_name;
        }
        if(transition.destination == old_name)
        {
            transition.destination = new_name;
        }
    }
}

void remove_animation_state(animation_layer_def& layer, size_t state_index)
{
    if(state_index >= layer.states.size())
    {
        return;
    }
    const std::string name = layer.states[state_index].name;
    layer.states.erase(layer.states.begin() + std::ptrdiff_t(state_index));
    layer.transitions.erase(std::remove_if(layer.transitions.begin(),
                                           layer.transitions.end(),
                                           [&](const animation_transition_def& transition)
                                           {
                                               return transition.source == name || transition.destination == name;
                                           }),
                            layer.transitions.end());
    if(layer.default_state == name)
    {
        layer.default_state.clear();
    }
}

void rename_animation_parameter(animation_controller& controller, size_t parameter_index, const std::string& new_name)
{
    if(parameter_index >= controller.parameters.size())
    {
        return;
    }
    const std::string old_name = controller.parameters[parameter_index].name;
    controller.parameters[parameter_index].name = new_name;
    const auto rename = [&](std::string& reference)
    {
        if(reference == old_name)
        {
            reference = new_name;
        }
    };
    for(auto& layer : controller.layers)
    {
        for(auto& state : layer.states)
        {
            rename(state.parameter_x);
            rename(state.parameter_y);
            rename(state.speed_parameter);
        }
        for(auto& transition : layer.transitions)
        {
            for(auto& condition : transition.conditions)
            {
                rename(condition.parameter);
            }
        }
    }
}

auto compute_animation_mask_weights(const std::vector<animation_mask_entry>& mask,
                                    const std::vector<std::string>& names,
                                    const std::vector<int>& parents) -> std::vector<float>
{
    std::unordered_map<std::string, const animation_mask_entry*> entries;
    for(const auto& entry : mask)
    {
        entries[entry.bone] = &entry;
    }
    std::vector<float> weights(names.size(), 0.0f);
    for(size_t bone = 0; bone < names.size(); ++bone)
    {
        int current = int(bone);
        // Bounded walk: a malformed parent list must not loop forever.
        for(size_t depth = 0; current >= 0 && size_t(current) < names.size() && depth <= names.size(); ++depth)
        {
            const auto it = entries.find(names[size_t(current)]);
            if(it != entries.end() && (size_t(current) == bone || it->second->include_children))
            {
                weights[bone] = math::clamp(it->second->weight, 0.0f, 1.0f);
                break;
            }
            current = size_t(current) < parents.size() ? parents[size_t(current)] : -1;
        }
    }
    return weights;
}

} // namespace unravel
