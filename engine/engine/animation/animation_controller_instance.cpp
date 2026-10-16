#include "animation_controller_instance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace unravel
{

namespace
{
/// Tolerance for equals / not_equals conditions on float parameters.
constexpr float FLOAT_EQUALS_TOLERANCE = 1e-4f;

/// mask_version of a layer whose (empty) mask was already applied.
constexpr uint64_t MASK_APPLIED_EMPTY = std::numeric_limits<uint64_t>::max();

auto make_blend_space(const animation_state_def& def) -> std::shared_ptr<blend_space_def>
{
    if(def.motion == animation_motion_type::clip)
    {
        return {};
    }
    auto space = std::make_shared<blend_space_def>();
    if(def.motion == animation_motion_type::blend_2d_directional)
    {
        space->set_2d_mode(blend_space_2d_mode::freeform_directional);
    }
    for(const auto& point : def.points)
    {
        if(!point.clip)
        {
            continue;
        }
        if(def.motion == animation_motion_type::blend_1d)
        {
            space->add_clip({point.position.x}, point.clip);
        }
        else
        {
            space->add_clip({point.position.x, point.position.y}, point.clip);
        }
    }
    return space;
}

/// A clip state without a clip: the layer rests, contributing nothing.
auto is_empty_state(const animation_state_def& def) -> bool
{
    return def.motion == animation_motion_type::clip && !def.clip;
}

/// Whether some cycle k >= 0 puts k + mark inside (previous, current].
auto has_crossed(double previous, double current, double mark) -> bool
{
    if(current <= previous)
    {
        return false;
    }
    double candidate = std::max(std::ceil(previous - mark), 0.0) + mark;
    if(candidate <= previous)
    {
        candidate += 1.0;
    }
    return candidate <= current;
}
} // namespace

void animation_controller_instance::bind(const std::shared_ptr<const animation_controller>& controller)
{
    if(controller_ == controller)
    {
        if(controller_ && controller_->revision != bound_revision_)
        {
            rebuild_preserving_state();
        }
        return;
    }
    const bool reloads = controller_ != nullptr && controller != nullptr;
    controller_ = controller;
    if(reloads)
    {
        rebuild_preserving_state();
        return;
    }
    bound_revision_ = controller_ ? controller_->revision : 0;
    build_runtime();
    reset();
}

auto animation_controller_instance::matches_definition_shape() const -> bool
{
    if(controller_->layers.size() != layers_.size() || controller_->parameters.size() != values_.size())
    {
        return false;
    }
    for(size_t i = 0; i < layers_.size(); ++i)
    {
        if(controller_->layers[i].states.size() != layers_[i].states.size() ||
           controller_->layers[i].transitions.size() != layers_[i].transitions.size())
        {
            return false;
        }
    }
    return true;
}

void animation_controller_instance::rebuild_preserving_state()
{
    struct kept_layer
    {
        std::string name;
        std::string current;
        std::string next;
        std::string measured;
        double measured_time{};
        bool started{};
        std::optional<float> weight_override;
        std::optional<animation_layer_blend_mode> blend_mode_override;
    };
    const auto get_state_name = [](const std::vector<std::string>& names, int state) -> std::string
    {
        return state >= 0 && size_t(state) < names.size() ? names[size_t(state)] : std::string{};
    };
    std::vector<std::pair<std::string, parameter_value>> kept_values;
    std::vector<kept_layer> kept_layers;
    // The names come from the definition as it was bound; it was edited in place, so read them
    // from the runtime's own indices before rebuilding.
    for(size_t i = 0; i < values_.size() && i < parameter_names_.size(); ++i)
    {
        kept_values.emplace_back(parameter_names_[i], values_[i]);
    }
    for(size_t i = 0; i < layers_.size() && i < layer_names_.size(); ++i)
    {
        const auto& layer = layers_[i];
        const auto& names = state_names_[i];
        kept_layers.push_back({layer_names_[i],
                               get_state_name(names, layer.current_state),
                               get_state_name(names, layer.next_state),
                               get_state_name(names, layer.measured_state),
                               layer.measured_time,
                               layer.started,
                               layer.weight_override,
                               layer.blend_mode_override});
    }
    stale_layer_count_ = std::max(stale_layer_count_, layers_.size());

    bound_revision_ = controller_->revision;
    build_runtime();
    reset_values();

    for(const auto& [name, value] : kept_values)
    {
        const int index = controller_->find_parameter(name);
        if(index >= 0 && size_t(index) < values_.size())
        {
            values_[size_t(index)] = value;
        }
    }
    for(const auto& kept : kept_layers)
    {
        const int layer_index = controller_->find_layer(kept.name);
        if(layer_index < 0)
        {
            continue;
        }
        const auto& def = controller_->layers[size_t(layer_index)];
        auto& layer = layers_[size_t(layer_index)];
        layer.weight_override = kept.weight_override;
        layer.blend_mode_override = kept.blend_mode_override;
        const int current = kept.started ? find_animation_state(def, kept.current) : -1;
        if(current < 0)
        {
            // Not started, or its state is gone: the layer starts over at its default state.
            continue;
        }
        layer.started = true;
        layer.current_state = current;
        layer.next_state = kept.next.empty() ? -1 : find_animation_state(def, kept.next);
        layer.measured_state = find_animation_state(def, kept.measured);
        layer.measured_time = kept.measured_time;
        // Transitions have no names: a crossfade still running finishes without being interrupted.
        layer.transition = -1;
        layer.interruptible = false;
        layer.motion_check_pending = true;
    }
}

void animation_controller_instance::adopt_playing_motion(animation_player& player, size_t layer_index)
{
    auto& layer = layers_[layer_index];
    layer.motion_check_pending = false;
    const auto& player_layers = player.get_layers();
    if(!layer.started || layer_index >= player_layers.size())
    {
        return;
    }
    const auto& player_layer = player_layers[layer_index];
    const bool crossfading = player_layer.target_state.is_valid();
    if(crossfading && layer.next_state < 0)
    {
        // The crossfade's destination is gone: settle on the current state where it is.
        enter_state(player, layer_index, layer.current_state, 0.0f, float(player_layer.current_state.state.get_progress()), false);
        return;
    }
    const int shown = crossfading ? layer.next_state : layer.current_state;
    const auto& state_def = controller_->layers[layer_index].states[size_t(shown)];
    if(is_empty_state(state_def))
    {
        return;
    }
    const auto& playing = crossfading ? player_layer.target_state.state : player_layer.current_state.state;
    auto& runtime = layer.states[size_t(shown)];
    bool same_motion = playing.loop == state_def.loop;
    if(state_def.motion == animation_motion_type::clip)
    {
        same_motion = same_motion && !playing.blend_space && playing.clip == state_def.clip;
    }
    else
    {
        same_motion = same_motion && playing.blend_space && runtime.blend_space && *playing.blend_space == *runtime.blend_space;
        if(same_motion)
        {
            // The player keeps the definition it plays; inputs keep flowing to it.
            runtime.blend_space = playing.blend_space;
        }
    }
    if(!same_motion)
    {
        enter_state(player, layer_index, shown, 0.0f, playing.get_progress(), false);
    }
}

auto animation_controller_instance::get_controller() const -> const std::shared_ptr<const animation_controller>&
{
    return controller_;
}

auto animation_controller_instance::is_bound() const -> bool
{
    return controller_ != nullptr;
}

void animation_controller_instance::build_runtime()
{
    layers_.clear();
    curve_ids_.clear();
    values_.clear();
    parameter_names_.clear();
    layer_names_.clear();
    state_names_.clear();
    skeleton_ = {};
    if(!controller_)
    {
        return;
    }
    for(const auto& parameter : controller_->parameters)
    {
        parameter_names_.push_back(parameter.name);
    }
    for(const auto& layer_def : controller_->layers)
    {
        layer_names_.push_back(layer_def.name);
        auto& names = state_names_.emplace_back();
        for(const auto& state_def : layer_def.states)
        {
            names.push_back(state_def.name);
        }
    }

    curve_ids_.reserve(controller_->parameters.size());
    for(const auto& parameter : controller_->parameters)
    {
        curve_ids_.push_back(parameter.type == animation_parameter_type::float_value
                                 ? get_animation_curve_id(parameter.name)
                                 : 0u);
    }

    layers_.resize(controller_->layers.size());
    for(size_t layer_index = 0; layer_index < controller_->layers.size(); ++layer_index)
    {
        const auto& def = controller_->layers[layer_index];
        auto& layer = layers_[layer_index];
        layer.states.resize(def.states.size());
        for(size_t state_index = 0; state_index < def.states.size(); ++state_index)
        {
            const auto& state_def = def.states[state_index];
            auto& state = layer.states[state_index];
            state.blend_space = make_blend_space(state_def);
            state.parameter_x = controller_->find_parameter(state_def.parameter_x);
            state.parameter_y = controller_->find_parameter(state_def.parameter_y);
            state.speed_parameter = controller_->find_parameter(state_def.speed_parameter);
        }
        layer.transitions.resize(def.transitions.size());
        for(size_t transition_index = 0; transition_index < def.transitions.size(); ++transition_index)
        {
            const auto& transition_def = def.transitions[transition_index];
            auto& transition = layer.transitions[transition_index];
            transition.source = transition_def.source.empty() ? -1 : find_animation_state(def, transition_def.source);
            transition.destination = find_animation_state(def, transition_def.destination);
            if(!transition_def.source.empty() && transition.source < 0)
            {
                // An unknown source must never act as Any State.
                transition.destination = -1;
            }
            for(const auto& condition : transition_def.conditions)
            {
                transition.condition_parameters.push_back(controller_->find_parameter(condition.parameter));
            }
        }
        const int default_state = find_animation_state(def, def.default_state);
        layer.default_state = default_state >= 0 ? default_state : 0;
    }
}

void animation_controller_instance::reset_values()
{
    values_.clear();
    consumed_triggers_.clear();
    if(!controller_)
    {
        return;
    }
    values_.reserve(controller_->parameters.size());
    for(const auto& parameter : controller_->parameters)
    {
        parameter_value value;
        value.float_value = parameter.default_float;
        value.int_value = parameter.default_int;
        value.bool_value = parameter.type == animation_parameter_type::trigger ? false : parameter.default_bool;
        values_.push_back(value);
    }
}

void animation_controller_instance::reset()
{
    reset_values();
    for(auto& layer : layers_)
    {
        layer.current_state = -1;
        layer.next_state = -1;
        layer.transition = -1;
        layer.interruptible = false;
        layer.started = false;
        layer.measured_state = -1;
        layer.measured_time = 0.0;
        layer.mask_version = 0;
        layer.pending_state = -1;
        layer.weight_override.reset();
        layer.blend_mode_override.reset();
        layer.motion_check_pending = false;
        layer.stopped = false;
    }
}

void animation_controller_instance::update(animation_player& player,
                                           uint64_t skeleton_version,
                                           const skeleton_provider_t& skeleton)
{
    if(!controller_)
    {
        return;
    }
    if(controller_->revision != bound_revision_ || !matches_definition_shape())
    {
        // Edited in place: runtime indices may point past the edited lists.
        rebuild_preserving_state();
    }
    apply_curves(player);
    // Layers an edit removed stop playing what they played.
    for(size_t layer_index = layers_.size(); layer_index < stale_layer_count_; ++layer_index)
    {
        player.clear(layer_index);
    }
    stale_layer_count_ = 0;

    for(size_t layer_index = 0; layer_index < layers_.size(); ++layer_index)
    {
        auto& layer = layers_[layer_index];
        const auto& def = controller_->layers[layer_index];
        if(layer.stopped && layer.pending_state < 0)
        {
            continue;
        }
        if(def.states.empty())
        {
            if(layer.started)
            {
                // Its states were all deleted: nothing plays on the layer any more.
                player.clear(layer_index);
                layer.started = false;
                layer.current_state = -1;
                layer.next_state = -1;
            }
            continue;
        }
        resolve_mask(player, layer_index, skeleton_version, skeleton);

        if(!layer.started)
        {
            start_layer(player, layer_index);
        }
        else
        {
            if(layer.motion_check_pending)
            {
                adopt_playing_motion(player, layer_index);
            }
            track_transition_completion(player, layer_index);
            const auto& player_layers = player.get_layers();
            const bool lost_motion = layer_index >= player_layers.size() || !player_layers[layer_index].has_motion();
            if(layer.pending_state >= 0)
            {
                const int state = layer.pending_state;
                layer.pending_state = -1;
                enter_state(player, layer_index, state, layer.pending_duration, layer.pending_offset, false);
                layer.transition = -1;
                layer.interruptible = false;
            }
            else if(lost_motion && layer.current_state >= 0 &&
                    !is_empty_state(controller_->layers[layer_index].states[size_t(layer.current_state)]))
            {
                // Something outside the controller cleared the layer: resume the current state.
                enter_state(player, layer_index, layer.current_state, 0.0f, 0.0f, false);
            }
            else
            {
                evaluate_transitions(player, layer_index);
            }
        }
        push_state_inputs(player, layer_index);
    }
    // A trigger fires one transition per layer that tests it, then resets.
    for(const int parameter : consumed_triggers_)
    {
        values_[size_t(parameter)].bool_value = false;
    }
    consumed_triggers_.clear();
}

void animation_controller_instance::sync(const animation_player& player)
{
    for(size_t layer_index = 0; layer_index < layers_.size(); ++layer_index)
    {
        if(layers_[layer_index].started)
        {
            track_transition_completion(player, layer_index);
        }
    }
}

void animation_controller_instance::start_layer(animation_player& player, size_t layer_index)
{
    auto& layer = layers_[layer_index];
    const auto& def = controller_->layers[layer_index];
    const float weight = layer_index == 0 ? 1.0f : def.weight;
    const auto blend_mode = layer_index == 0 ? animation_layer_blend_mode::override_pose : def.blend_mode;
    player.set_layer_weight(layer_index, layer.weight_override.value_or(weight));
    player.set_layer_blend_mode(layer_index, layer.blend_mode_override.value_or(blend_mode));
    const int state = layer.pending_state >= 0 ? layer.pending_state : layer.default_state;
    const float offset = layer.pending_state >= 0 ? layer.pending_offset
                                                  : def.states[size_t(state)].cycle_offset;
    layer.pending_state = -1;
    // Started layers begin from nothing: enter the state without a crossfade from whatever a
    // previous controller or script left on the layer.
    player.clear(layer_index);
    enter_state(player, layer_index, state, 0.0f, offset, false);
    layer.started = true;
}

void animation_controller_instance::enter_state(animation_player& player,
                                                size_t layer_index,
                                                int state,
                                                float duration,
                                                float start_progress,
                                                bool phase_sync)
{
    auto& layer = layers_[layer_index];
    const auto& def = controller_->layers[layer_index];
    if(state < 0 || size_t(state) >= def.states.size())
    {
        return;
    }
    const auto& state_def = def.states[size_t(state)];
    const auto& state_runtime = layer.states[size_t(state)];

    const bool in_transition = layer_index < player.get_layers().size() && player.get_layers()[layer_index].target_state.is_valid();

    animation_play_params params;
    params.duration = seconds_t(std::max(0.0f, duration));
    params.loop = state_def.loop;
    params.phase_sync = phase_sync;
    params.start_progress = math::clamp(start_progress, 0.0f, 1.0f);
    params.speed = get_state_speed(state_def, state_runtime);
    // The controller decides what plays: entering a state starts its motion at the transition's
    // offset even when the layer shows the same clip (another state's, or the state's own fading
    // out). The player's same-motion shortcuts are for scripts that blend every frame.
    params.restart = true;

    if(state_def.motion == animation_motion_type::clip)
    {
        player.play_clip(layer_index, state_def.clip, params);
    }
    else
    {
        player.play_blend_space(layer_index, state_runtime.blend_space, params);
    }

    if(in_transition && layer.next_state >= 0)
    {
        // The interrupted destination is what the layer leaves from now.
        layer.current_state = layer.next_state;
    }

    const auto& player_layer = player.get_layers()[layer_index];
    const bool crossfading = player_layer.target_state.is_valid();
    if(crossfading)
    {
        layer.next_state = state;
    }
    else
    {
        layer.current_state = state;
        layer.next_state = -1;
        layer.transition = -1;
        layer.interruptible = false;
    }
    layer.measured_state = state;
    layer.measured_time =
        (crossfading ? player_layer.target_state.state : player_layer.current_state.state).get_cycle_position();
}

void animation_controller_instance::resolve_mask(animation_player& player,
                                                 size_t layer_index,
                                                 uint64_t skeleton_version,
                                                 const skeleton_provider_t& skeleton)
{
    auto& layer = layers_[layer_index];
    const auto& def = controller_->layers[layer_index];
    if(def.mask.empty() || layer_index == 0)
    {
        if(layer.mask_version != MASK_APPLIED_EMPTY)
        {
            player.set_layer_bone_mask(layer_index, {});
            layer.mask_version = MASK_APPLIED_EMPTY;
        }
        return;
    }
    if(layer.mask_version == skeleton_version || !skeleton)
    {
        return;
    }
    if(skeleton_.version != skeleton_version || skeleton_.names.empty())
    {
        skeleton_ = {};
        skeleton(skeleton_);
        skeleton_.version = skeleton_version;
    }

    auto weights = compute_animation_mask_weights(def.mask, skeleton_.names, skeleton_.parents);
    player.set_layer_bone_mask(layer_index, std::move(weights));
    layer.mask_version = skeleton_version;
}

void animation_controller_instance::apply_curves(const animation_player& player)
{
    for(size_t i = 0; i < curve_ids_.size() && i < values_.size(); ++i)
    {
        if(curve_ids_[i] == 0u)
        {
            continue;
        }
        if(const auto value = player.find_curve_value(curve_ids_[i]))
        {
            values_[i].float_value = *value;
        }
    }
}

void animation_controller_instance::track_transition_completion(const animation_player& player, size_t layer_index)
{
    auto& layer = layers_[layer_index];
    if(layer.next_state < 0)
    {
        return;
    }
    const auto& player_layers = player.get_layers();
    if(layer_index < player_layers.size() && player_layers[layer_index].target_state.is_valid())
    {
        return;
    }
    layer.current_state = layer.next_state;
    layer.next_state = -1;
    layer.transition = -1;
    layer.interruptible = false;
    // During the crossfade the destination was measured already: keeping that position lets the
    // step that finished the crossfade still count a crossing of an exit time.
    if(layer.measured_state != layer.current_state && layer_index < player_layers.size())
    {
        layer.measured_state = layer.current_state;
        layer.measured_time = player_layers[layer_index].current_state.state.get_cycle_position();
    }
}

void animation_controller_instance::evaluate_transitions(animation_player& player, size_t layer_index)
{
    auto& layer = layers_[layer_index];
    const auto& def = controller_->layers[layer_index];
    const bool in_transition = layer.next_state >= 0;
    const int evaluated_state = in_transition ? layer.next_state : layer.current_state;

    const auto finish_measure = [&]()
    {
        const auto& player_layers = player.get_layers();
        if(layer_index >= player_layers.size())
        {
            return;
        }
        const auto& player_layer = player_layers[layer_index];
        layer.measured_state = evaluated_state;
        layer.measured_time =
            (in_transition ? player_layer.target_state.state : player_layer.current_state.state).get_cycle_position();
    };

    if(in_transition && !layer.interruptible)
    {
        finish_measure();
        return;
    }

    const auto try_fire = [&](size_t transition_index) -> bool
    {
        if(!is_transition_ready(player, layer_index, transition_index))
        {
            return false;
        }
        const auto& transition_def = def.transitions[transition_index];
        const auto& transition = layer.transitions[transition_index];
        consume_triggers(transition_def, transition);
        enter_state(player,
                    layer_index,
                    transition.destination,
                    transition_def.duration,
                    transition_def.offset,
                    transition_def.phase_sync);
        if(layer.next_state >= 0)
        {
            layer.transition = int(transition_index);
            layer.interruptible = transition_def.interruptible;
        }
        return true;
    };

    // Any State transitions take priority, in authored order.
    for(size_t i = 0; i < layer.transitions.size(); ++i)
    {
        const auto& transition = layer.transitions[i];
        if(transition.source != -1 || transition.destination < 0)
        {
            continue;
        }
        if(!def.transitions[i].can_transition_to_self && transition.destination == evaluated_state)
        {
            continue;
        }
        if(in_transition && transition.destination == layer.next_state)
        {
            continue;
        }
        if(try_fire(i))
        {
            return;
        }
    }

    for(size_t i = 0; i < layer.transitions.size(); ++i)
    {
        const auto& transition = layer.transitions[i];
        if(transition.source < 0 || transition.source != evaluated_state || transition.destination < 0)
        {
            continue;
        }
        if(try_fire(i))
        {
            return;
        }
    }

    finish_measure();
}

auto animation_controller_instance::is_transition_ready(const animation_player& player,
                                                        size_t layer_index,
                                                        size_t transition_index) const -> bool
{
    const auto& layer = layers_[layer_index];
    const auto& def = controller_->layers[layer_index];
    const auto& transition_def = def.transitions[transition_index];
    const auto& transition = layer.transitions[transition_index];

    if(!are_conditions_met(transition_def, transition))
    {
        return false;
    }
    if(!transition_def.has_exit_time)
    {
        return !transition_def.conditions.empty();
    }

    const auto& player_layers = player.get_layers();
    if(layer_index >= player_layers.size())
    {
        return false;
    }
    const bool in_transition = layer.next_state >= 0;
    const int state = in_transition ? layer.next_state : layer.current_state;
    if(state < 0)
    {
        return false;
    }
    const auto& player_state = in_transition ? player_layers[layer_index].target_state.state
                                             : player_layers[layer_index].current_state.state;
    const double current = player_state.get_cycle_position();
    const double previous = layer.measured_state == state ? layer.measured_time : current;
    const double exit_time = double(std::max(0.0f, transition_def.exit_time));
    if(!def.states[size_t(state)].loop)
    {
        return current >= exit_time;
    }
    // Once the exit time is reached: from that point of the cycle to its end, in this cycle and
    // every later one. A crossing between two updates counts even when the update lands past it.
    // A whole number (1, 2) exits at the end of a cycle: only crossing a cycle end counts there.
    const double fraction = current - std::floor(current);
    const double exit_fraction = exit_time - std::floor(exit_time);
    if(exit_time > 0.0 && exit_fraction <= 0.0)
    {
        return has_crossed(previous, current, exit_time);
    }
    return (current >= exit_time && fraction >= exit_fraction) || has_crossed(previous, current, exit_time);
}

auto animation_controller_instance::are_conditions_met(const animation_transition_def& def,
                                                       const transition_runtime& runtime) const -> bool
{
    for(size_t i = 0; i < def.conditions.size(); ++i)
    {
        const int parameter = i < runtime.condition_parameters.size() ? runtime.condition_parameters[i] : -1;
        if(parameter < 0 || size_t(parameter) >= values_.size())
        {
            return false;
        }
        const auto& condition = def.conditions[i];
        const auto& value = values_[size_t(parameter)];
        const auto type = controller_->parameters[size_t(parameter)].type;
        const float number = read_number(parameter);
        bool met = false;
        switch(condition.mode)
        {
            case animation_condition_mode::greater:
                met = number > condition.threshold;
                break;
            case animation_condition_mode::less:
                met = number < condition.threshold;
                break;
            case animation_condition_mode::equals:
                met = type == animation_parameter_type::int_value
                          ? value.int_value == int(std::lround(condition.threshold))
                          : std::fabs(number - condition.threshold) <= FLOAT_EQUALS_TOLERANCE;
                break;
            case animation_condition_mode::not_equals:
                met = type == animation_parameter_type::int_value
                          ? value.int_value != int(std::lround(condition.threshold))
                          : std::fabs(number - condition.threshold) > FLOAT_EQUALS_TOLERANCE;
                break;
            case animation_condition_mode::is_true:
                met = value.bool_value;
                break;
            case animation_condition_mode::is_false:
                met = !value.bool_value;
                break;
        }
        if(!met)
        {
            return false;
        }
    }
    return true;
}

void animation_controller_instance::consume_triggers(const animation_transition_def& def, const transition_runtime& runtime)
{
    for(size_t i = 0; i < def.conditions.size() && i < runtime.condition_parameters.size(); ++i)
    {
        const int parameter = runtime.condition_parameters[i];
        if(parameter >= 0 && controller_->parameters[size_t(parameter)].type == animation_parameter_type::trigger)
        {
            consumed_triggers_.push_back(parameter);
        }
    }
}

auto animation_controller_instance::get_state_speed(const animation_state_def& def, const state_runtime& runtime) const
    -> float
{
    float speed = def.speed;
    if(runtime.speed_parameter >= 0 && size_t(runtime.speed_parameter) < values_.size())
    {
        speed *= read_number(runtime.speed_parameter);
    }
    return std::max(0.0f, speed);
}

void animation_controller_instance::push_state_inputs(animation_player& player, size_t layer_index)
{
    const auto& layer = layers_[layer_index];
    const auto& def = controller_->layers[layer_index];
    const auto& player_layers = player.get_layers();
    if(layer_index >= player_layers.size())
    {
        return;
    }
    const auto push = [&](int state, bool target)
    {
        if(state < 0 || size_t(state) >= def.states.size())
        {
            return;
        }
        const auto& state_def = def.states[size_t(state)];
        const auto& state_runtime = layer.states[size_t(state)];
        if(state_runtime.blend_space)
        {
            const float values[] = {read_number(state_runtime.parameter_x), read_number(state_runtime.parameter_y)};
            const size_t count = state_def.motion == animation_motion_type::blend_1d ? 1 : 2;
            player.set_state_parameters(layer_index, target, hpp::span<const float>(values, count));
        }
        player.set_state_speed(layer_index, target, get_state_speed(state_def, state_runtime));
    };

    const auto& player_layer = player_layers[layer_index];
    if(layer.next_state >= 0 && player_layer.target_state.is_valid())
    {
        push(layer.next_state, true);
        // The source keeps playing during the crossfade unless an interruption froze it.
        if(player_layer.current_state.is_valid())
        {
            push(layer.current_state, false);
        }
    }
    else if(player_layer.current_state.is_valid())
    {
        push(layer.current_state, false);
    }
}

auto animation_controller_instance::get_parameter_index(hpp::string_view name) const -> int
{
    if(!controller_)
    {
        return -1;
    }
    const int index = controller_->find_parameter(name);
    // A definition edited since the last update may name parameters the values do not cover yet.
    return index >= 0 && size_t(index) < values_.size() ? index : -1;
}

auto animation_controller_instance::set_float(hpp::string_view name, float value) -> bool
{
    const int index = find_typed_parameter(name, animation_parameter_type::float_value, animation_parameter_type::int_value);
    if(index < 0 || !std::isfinite(value))
    {
        return false;
    }
    set_parameter_from_float(size_t(index), value);
    return true;
}

auto animation_controller_instance::set_int(hpp::string_view name, int value) -> bool
{
    const int index = find_typed_parameter(name, animation_parameter_type::int_value, animation_parameter_type::float_value);
    if(index < 0)
    {
        return false;
    }
    auto& entry = values_[size_t(index)];
    entry.int_value = value;
    entry.float_value = float(value);
    entry.bool_value = value != 0;
    return true;
}

auto animation_controller_instance::set_bool(hpp::string_view name, bool value) -> bool
{
    const int index =
        find_typed_parameter(name, animation_parameter_type::bool_value);
    if(index < 0)
    {
        return false;
    }
    values_[size_t(index)].bool_value = value;
    return true;
}

auto animation_controller_instance::set_trigger(hpp::string_view name) -> bool
{
    const int index = find_typed_parameter(name, animation_parameter_type::trigger);
    if(index < 0)
    {
        return false;
    }
    values_[size_t(index)].bool_value = true;
    return true;
}

auto animation_controller_instance::reset_trigger(hpp::string_view name) -> bool
{
    const int index = find_typed_parameter(name, animation_parameter_type::trigger);
    if(index < 0)
    {
        return false;
    }
    values_[size_t(index)].bool_value = false;
    return true;
}

auto animation_controller_instance::get_float(hpp::string_view name) const -> float
{
    return read_number(get_parameter_index(name));
}

auto animation_controller_instance::get_int(hpp::string_view name) const -> int
{
    const int index = get_parameter_index(name);
    if(index < 0)
    {
        return 0;
    }
    return int(std::lround(read_number(index)));
}

auto animation_controller_instance::get_bool(hpp::string_view name) const -> bool
{
    const int index = find_typed_parameter(name, animation_parameter_type::bool_value, animation_parameter_type::trigger);
    return index >= 0 && values_[size_t(index)].bool_value;
}

auto animation_controller_instance::read_number(int parameter) const -> float
{
    if(!controller_ || parameter < 0 || size_t(parameter) >= values_.size())
    {
        return 0.0f;
    }
    const auto& value = values_[size_t(parameter)];
    switch(controller_->parameters[size_t(parameter)].type)
    {
        case animation_parameter_type::int_value:
            return float(value.int_value);
        case animation_parameter_type::bool_value:
        case animation_parameter_type::trigger:
            return value.bool_value ? 1.0f : 0.0f;
        case animation_parameter_type::float_value:
            break;
    }
    return value.float_value;
}

auto animation_controller_instance::find_typed_parameter(hpp::string_view name,
                                                         animation_parameter_type type,
                                                         std::optional<animation_parameter_type> other_type) const -> int
{
    const int index = get_parameter_index(name);
    if(index < 0)
    {
        return -1;
    }
    const auto actual = controller_->parameters[size_t(index)].type;
    return actual == type || actual == other_type ? index : -1;
}

auto animation_controller_instance::get_parameter_values() const -> const std::vector<parameter_value>&
{
    return values_;
}

void animation_controller_instance::set_parameter_from_float(size_t index, float value)
{
    if(!controller_ || index >= values_.size())
    {
        return;
    }
    auto& entry = values_[index];
    switch(controller_->parameters[index].type)
    {
        case animation_parameter_type::float_value:
            entry.float_value = value;
            break;
        case animation_parameter_type::int_value:
            entry.int_value = int(std::lround(value));
            entry.float_value = float(entry.int_value);
            break;
        case animation_parameter_type::bool_value:
        case animation_parameter_type::trigger:
            entry.bool_value = value != 0.0f;
            break;
    }
}

void animation_controller_instance::set_layer_weight(animation_player& player, size_t layer_index, float weight)
{
    if(layer_index >= layers_.size())
    {
        return;
    }
    layers_[layer_index].weight_override = math::clamp(weight, 0.0f, 1.0f);
    player.set_layer_weight(layer_index, *layers_[layer_index].weight_override);
}

void animation_controller_instance::set_layer_blend_mode(animation_player& player,
                                                         size_t layer_index,
                                                         animation_layer_blend_mode mode)
{
    if(layer_index >= layers_.size())
    {
        return;
    }
    layers_[layer_index].blend_mode_override = mode;
    player.set_layer_blend_mode(layer_index, mode);
}

auto animation_controller_instance::get_layer_count() const -> size_t
{
    return layers_.size();
}

auto animation_controller_instance::get_layer_index(hpp::string_view name) const -> int
{
    return controller_ ? controller_->find_layer(name) : -1;
}

auto animation_controller_instance::get_layer_status(size_t layer_index) const -> layer_status
{
    layer_status status;
    if(layer_index >= layers_.size())
    {
        return status;
    }
    const auto& layer = layers_[layer_index];
    status.current_state = layer.current_state;
    status.next_state = layer.next_state;
    status.transition = layer.transition;
    return status;
}

auto animation_controller_instance::get_layer_status(size_t layer_index, const animation_player& player) const
    -> layer_status
{
    layer_status status = get_layer_status(layer_index);
    const auto& player_layers = player.get_layers();
    if(layer_index >= player_layers.size())
    {
        return status;
    }
    const auto& player_layer = player_layers[layer_index];
    if(status.next_state >= 0 && player_layer.target_state.is_valid())
    {
        status.transition_progress = player.get_blend_progress(player_layer);
        status.next_state_time = float(player_layer.target_state.state.get_cycle_position());
    }
    status.state_time = float(player_layer.current_state.state.get_cycle_position());
    return status;
}

auto animation_controller_instance::get_state_name(size_t layer_index, int state) const -> const std::string&
{
    static const std::string empty;
    if(!controller_ || layer_index >= controller_->layers.size())
    {
        return empty;
    }
    const auto& states = controller_->layers[layer_index].states;
    if(state < 0 || size_t(state) >= states.size())
    {
        return empty;
    }
    return states[size_t(state)].name;
}

auto animation_controller_instance::is_in_state(size_t layer_index, hpp::string_view state_name) const -> bool
{
    if(layer_index >= layers_.size())
    {
        return false;
    }
    return get_state_name(layer_index, layers_[layer_index].current_state) == state_name;
}

auto animation_controller_instance::is_in_tag(size_t layer_index, hpp::string_view tag) const -> bool
{
    if(!controller_ || layer_index >= layers_.size())
    {
        return false;
    }
    const int state = layers_[layer_index].current_state;
    const auto& states = controller_->layers[layer_index].states;
    return state >= 0 && size_t(state) < states.size() && states[size_t(state)].tag == tag;
}

auto animation_controller_instance::cross_fade(size_t layer_index,
                                               hpp::string_view state_name,
                                               float duration,
                                               float normalized_time) -> bool
{
    if(!controller_ || layer_index >= layers_.size())
    {
        return false;
    }
    const int state = find_animation_state(controller_->layers[layer_index], state_name);
    if(state < 0)
    {
        return false;
    }
    auto& layer = layers_[layer_index];
    layer.pending_state = state;
    layer.pending_duration = std::isfinite(duration) ? std::max(0.0f, duration) : 0.0f;
    layer.pending_offset = std::isfinite(normalized_time) ? normalized_time : 0.0f;
    layer.stopped = false;
    return true;
}

auto animation_controller_instance::play_state(size_t layer_index, hpp::string_view state_name, float normalized_time)
    -> bool
{
    return cross_fade(layer_index, state_name, 0.0f, normalized_time);
}

void animation_controller_instance::stop_layer(size_t layer_index)
{
    if(layer_index < layers_.size())
    {
        layers_[layer_index].stopped = true;
    }
}

} // namespace unravel
