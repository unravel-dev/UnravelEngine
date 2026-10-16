#pragma once
#include <engine/engine_export.h>

#include <engine/animation/animation_controller.h>
#include <engine/animation/animation_player.h>

#include <hpp/string_view.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief The target skeleton as layer masks see it: bone names and parent indices (-1 for roots),
 * indexed like the armature. `version` changes when the skeleton changes.
 */
struct animation_skeleton_desc
{
    uint64_t version{};
    std::vector<std::string> names;
    std::vector<int> parents;
};

/**
 * @brief Runtime of one animation_controller on one entity: parameter values, the active state
 * and transition of every layer, and the player calls that realise them.
 *
 * Per frame the animation system calls update() BEFORE advancing the player: it starts the
 * default states, lets curves drive same-named float parameters, fires the transitions whose
 * conditions and exit times hold, and pushes blend parameters and speeds into the player.
 */
class animation_controller_instance
{
public:
    using seconds_t = animation_clip::seconds_t;
    using skeleton_provider_t = std::function<void(animation_skeleton_desc& desc)>;

    struct parameter_value
    {
        float float_value{};
        int int_value{};
        bool bool_value{};
    };

    /// Runtime view of a layer, for scripts and the graph editor's live debugging.
    struct layer_status
    {
        /// The state being played - during a transition, the state being left.
        int current_state{-1};
        /// Destination of the running transition, or -1.
        int next_state{-1};
        /// Index of the running transition in the layer's transitions, or -1.
        int transition{-1};
        /// Crossfade progress of the running transition, 0..1.
        float transition_progress{};
        /// Normalized time of the current state: loops count whole cycles.
        float state_time{};
        /// Normalized time of the destination state.
        float next_state_time{};
    };

    /**
     * @brief Binds the definition. Binding after an unbind (or for the first time) starts from the
     * defaults. Moving from one definition to another without unbinding in between is a reload of
     * the same controller - a recompile, an edit saved while playing - and keeps parameter values,
     * script layer settings and the active states by name. Binding the same one again keeps
     * everything.
     */
    void bind(const std::shared_ptr<const animation_controller>& controller);
    auto get_controller() const -> const std::shared_ptr<const animation_controller>&;
    auto is_bound() const -> bool;

    /**
     * @brief Parameters back to their defaults, layers back to their default states (entered on
     * the next update).
     */
    void reset();

    /**
     * @brief Advances the state machines and drives the player for this frame.
     *
     * @param skeleton Called when masks must be resolved against a new skeleton version; may be
     * empty when no layer has a mask.
     */
    void update(animation_player& player, uint64_t skeleton_version, const skeleton_provider_t& skeleton);

    /**
     * @brief Catches up with crossfades the player finished since the last update, so state
     * queries made between updates (scripts, tools) report the state the pose already shows.
     */
    void sync(const animation_player& player);

    auto get_parameter_index(hpp::string_view name) const -> int;
    auto set_float(hpp::string_view name, float value) -> bool;
    auto set_int(hpp::string_view name, int value) -> bool;
    auto set_bool(hpp::string_view name, bool value) -> bool;
    auto set_trigger(hpp::string_view name) -> bool;
    auto reset_trigger(hpp::string_view name) -> bool;
    auto get_float(hpp::string_view name) const -> float;
    auto get_int(hpp::string_view name) const -> int;
    auto get_bool(hpp::string_view name) const -> bool;
    auto get_parameter_values() const -> const std::vector<parameter_value>&;
    /// Sets a parameter by index from a float, converted to its type (tools, debugging).
    void set_parameter_from_float(size_t index, float value);

    /**
     * @brief A layer's weight or blend mode set from outside the definition (scripts). It applies
     * now and whenever the layer starts again; the definition's own value applies until one is set.
     */
    void set_layer_weight(animation_player& player, size_t layer, float weight);
    void set_layer_blend_mode(animation_player& player, size_t layer, animation_layer_blend_mode mode);

    auto get_layer_count() const -> size_t;
    auto get_layer_index(hpp::string_view name) const -> int;
    auto get_layer_status(size_t layer) const -> layer_status;
    /// Same, with crossfade progress and state times read from the player the instance drives.
    auto get_layer_status(size_t layer, const animation_player& player) const -> layer_status;
    auto get_state_name(size_t layer, int state) const -> const std::string&;
    auto is_in_state(size_t layer, hpp::string_view state_name) const -> bool;
    auto is_in_tag(size_t layer, hpp::string_view tag) const -> bool;

    /**
     * @brief Crossfades a layer to a state now, regardless of transitions.
     */
    auto cross_fade(size_t layer, hpp::string_view state_name, float duration, float normalized_time = 0.0f) -> bool;

    /**
     * @brief Jumps a layer to a state now, at a normalized time.
     */
    auto play_state(size_t layer, hpp::string_view state_name, float normalized_time = 0.0f) -> bool;

    /**
     * @brief Leaves a layer stopped (a script faded it out): no transitions run on it and it does
     * not resume until cross_fade or play_state starts a state on it.
     */
    void stop_layer(size_t layer);

private:
    struct state_runtime
    {
        std::shared_ptr<blend_space_def> blend_space;
        int parameter_x{-1};
        int parameter_y{-1};
        int speed_parameter{-1};
    };

    struct transition_runtime
    {
        int source{-1};
        int destination{-1};
        /// Parameter index per condition, -1 when unknown.
        std::vector<int> condition_parameters;
    };

    struct layer_runtime
    {
        std::vector<state_runtime> states;
        std::vector<transition_runtime> transitions;
        int default_state{};
        int current_state{-1};
        int next_state{-1};
        int transition{-1};
        bool interruptible{};
        bool started{};
        /// State and cycle position exit times were last measured against.
        int measured_state{-1};
        double measured_time{};
        uint64_t mask_version{};
        /// State requested by play_state / cross_fade, entered on the next update.
        int pending_state{-1};
        float pending_duration{};
        float pending_offset{};
        /// Set from scripts; the definition's weight and blend mode apply while empty.
        std::optional<float> weight_override;
        std::optional<animation_layer_blend_mode> blend_mode_override;
        /// Rebuilt after an edit: the next update checks the playing motion still is the state's.
        bool motion_check_pending{};
        /// Stopped from a script; cross_fade / play_state start it again.
        bool stopped{};
    };

    void build_runtime();
    /// A float or int parameter's value as a number; 0 for an unknown index.
    auto read_number(int parameter) const -> float;
    /// Index of the named parameter when it has one of the given types, else -1.
    auto find_typed_parameter(hpp::string_view name,
                              animation_parameter_type type,
                              std::optional<animation_parameter_type> other_type = std::nullopt) const -> int;
    /// Rebuilds after an edit of the bound definition, keeping by name what still exists: the
    /// playing states, their time and the running crossfade stay on the player.
    void rebuild_preserving_state();
    /// Sets parameter values to the definition's defaults.
    void reset_values();
    /// After a rebuild: keeps the player's motion when it still is the shown state's, else enters
    /// the state again at its position without a crossfade.
    void adopt_playing_motion(animation_player& player, size_t layer_index);
    /// Whether the bound definition still has the lists the runtime was built for.
    auto matches_definition_shape() const -> bool;
    void start_layer(animation_player& player, size_t layer_index);
    void enter_state(animation_player& player,
                     size_t layer_index,
                     int state,
                     float duration,
                     float start_progress,
                     bool phase_sync);
    void resolve_mask(animation_player& player,
                      size_t layer_index,
                      uint64_t skeleton_version,
                      const skeleton_provider_t& skeleton);
    void apply_curves(const animation_player& player);
    void track_transition_completion(const animation_player& player, size_t layer_index);
    void evaluate_transitions(animation_player& player, size_t layer_index);
    auto is_transition_ready(const animation_player& player, size_t layer_index, size_t transition_index) const -> bool;
    auto are_conditions_met(const animation_transition_def& def, const transition_runtime& runtime) const -> bool;
    void consume_triggers(const animation_transition_def& def, const transition_runtime& runtime);
    void push_state_inputs(animation_player& player, size_t layer_index);
    auto get_state_speed(const animation_state_def& def, const state_runtime& runtime) const -> float;

    std::shared_ptr<const animation_controller> controller_;
    uint64_t bound_revision_{};
    std::vector<parameter_value> values_;
    std::vector<uint32_t> curve_ids_;
    std::vector<layer_runtime> layers_;
    animation_skeleton_desc skeleton_;
    /// Triggers transitions consumed this update; cleared once every layer has seen them.
    std::vector<int> consumed_triggers_;
    /// Player layers the definition drove before an edit removed them: cleared on the next update.
    size_t stale_layer_count_{};
    /// Names as of the last build, so an in-place edit can be mapped back by name.
    std::vector<std::string> parameter_names_;
    std::vector<std::string> layer_names_;
    std::vector<std::vector<std::string>> state_names_;
};

} // namespace unravel
