#pragma once
#include <engine/engine_export.h>

#include <engine/animation/animation_pose.h>
#include <engine/assets/asset_handle.h>

#include <hpp/string_view.hpp>
#include <math/math.h>

#include <memory>
#include <string>
#include <vector>

namespace unravel
{
class mesh;

/**
 * @brief Value type of a controller parameter.
 */
enum class animation_parameter_type : uint8_t
{
    float_value,
    int_value,
    bool_value,
    /// A bool that a transition consumes when it fires on it.
    trigger,
};

/**
 * @brief A named input of a controller, set from scripts (SetFloat, SetBool, SetTrigger...).
 */
struct animation_parameter_def
{
    friend auto operator==(const animation_parameter_def&, const animation_parameter_def&) -> bool = default;

    std::string name;
    animation_parameter_type type{animation_parameter_type::float_value};
    float default_float{};
    int default_int{};
    bool default_bool{};
};

/**
 * @brief How a transition condition compares its parameter.
 */
enum class animation_condition_mode : uint8_t
{
    /// Float or int parameter above the threshold.
    greater,
    /// Float or int parameter below the threshold.
    less,
    /// Int parameter equal to the threshold (floats compare with a small tolerance).
    equals,
    not_equals,
    /// Bool parameter (or trigger) set.
    is_true,
    /// Bool parameter cleared.
    is_false,
};

struct animation_condition_def
{
    friend auto operator==(const animation_condition_def&, const animation_condition_def&) -> bool = default;

    std::string parameter;
    animation_condition_mode mode{animation_condition_mode::greater};
    float threshold{};
};

/**
 * @brief What a state plays.
 */
enum class animation_motion_type : uint8_t
{
    clip,
    /// Clips placed on one parameter axis, blended linearly between neighbours.
    blend_1d,
    /// Clips placed on a plane of two parameters (gradient band interpolation).
    blend_2d_cartesian,
    /// Same, compared by direction and magnitude: velocity layouts with idle at the origin.
    blend_2d_directional,
};

struct animation_blend_point_def
{
    friend auto operator==(const animation_blend_point_def&, const animation_blend_point_def&) -> bool = default;

    asset_handle<animation_clip> clip;
    /// Position on the blend axes; 1D spaces use x only.
    math::vec2 position{};
};

struct animation_state_def
{
    friend auto operator==(const animation_state_def&, const animation_state_def&) -> bool = default;

    std::string name;
    animation_motion_type motion{animation_motion_type::clip};
    asset_handle<animation_clip> clip;
    /// Blend axes of blend states.
    std::string parameter_x;
    std::string parameter_y;
    std::vector<animation_blend_point_def> points;
    /// Playback rate of the state.
    float speed{1.0f};
    /// Optional float parameter the speed is multiplied by (walk cycle matched to move speed).
    std::string speed_parameter;
    bool loop{true};
    /// Normalized position the state starts at when it is entered without phase sync.
    float cycle_offset{};
    /// Free-form label scripts can test with IsInTag.
    std::string tag;
    /// Node position in the controller graph editor.
    math::vec2 editor_position{};
};

struct animation_transition_def
{
    friend auto operator==(const animation_transition_def&, const animation_transition_def&) -> bool = default;

    /// Source state name; empty means Any State (fires from whatever state the layer is in).
    std::string source;
    std::string destination;
    /// Every condition must hold.
    std::vector<animation_condition_def> conditions;
    /// Only fire once the source state has played to exit_time (and keep checking until its cycle
    /// ends - for a looping state, again from exit_time of every later cycle).
    bool has_exit_time{false};
    /// Normalized source time; values above 1 count whole loops (1.5 = half way into the second).
    float exit_time{0.9f};
    /// Crossfade length in seconds.
    float duration{0.2f};
    /// Normalized time the destination starts at.
    float offset{};
    /// Start the destination at the source's normalized phase instead of offset (walk <-> run).
    bool phase_sync{false};
    /// Transitions of the destination state (and Any State) may cut this crossfade short.
    bool interruptible{false};
    /// An Any State transition may re-enter the state the layer is already in.
    bool can_transition_to_self{false};
};

/**
 * @brief One entry of a layer's bone mask.
 *
 * With include_children the weight also covers every descendant that has no entry of its own -
 * so "spine_01 x 1" selects the upper body and a later "hand_r x 0" carves the right hand out.
 */
struct animation_mask_entry
{
    friend auto operator==(const animation_mask_entry&, const animation_mask_entry&) -> bool = default;

    std::string bone;
    float weight{1.0f};
    bool include_children{true};
};

struct animation_layer_def
{
    friend auto operator==(const animation_layer_def&, const animation_layer_def&) -> bool = default;

    std::string name;
    /// Weight the layer starts with; scripts change it with SetLayerWeight. The base layer is
    /// always fully weighted.
    float weight{1.0f};
    animation_layer_blend_mode blend_mode{animation_layer_blend_mode::override_pose};
    /// Empty: every bone.
    std::vector<animation_mask_entry> mask;
    /// State entered when the controller starts; empty means the first state.
    std::string default_state;
    std::vector<animation_state_def> states;
    std::vector<animation_transition_def> transitions;
    /// Node positions of the Entry and Any State nodes in the graph editor.
    math::vec2 entry_position{-260.0f, 0.0f};
    math::vec2 any_state_position{-260.0f, 120.0f};
};

/**
 * @brief State machine asset: parameters, layers of states and the transitions between them.
 *
 * An animation_component plays a controller instead of a single clip when one is assigned. Each
 * layer runs its own state machine; scripts drive it through parameters.
 */
struct animation_controller
{
    using sptr = std::shared_ptr<animation_controller>;

    std::vector<animation_parameter_def> parameters;
    std::vector<animation_layer_def> layers;
    /// Editor only: the model whose skeleton the graph editor offers for masks and previews.
    asset_handle<mesh> preview_model;

    /// Not saved. Bumped by editors after changing the definition in place, so running instances
    /// rebuild their runtime (keeping parameter values and current states by name).
    uint64_t revision{};

    auto find_parameter(hpp::string_view name) const -> int;
    auto find_layer(hpp::string_view name) const -> int;
};

/**
 * @brief Index of the state named `name` in `layer`, or -1.
 */
auto find_animation_state(const animation_layer_def& layer, hpp::string_view name) -> int;

/**
 * @brief Problems in a controller definition that make part of it unusable at runtime: duplicate
 * or empty names, transitions to unknown states, conditions on unknown or mistyped parameters,
 * blend states without points. Empty when the controller is consistent.
 */
auto validate_animation_controller(const animation_controller& controller) -> std::vector<std::string>;

/**
 * @brief Renames a state and every transition and default-state reference to it.
 */
void rename_animation_state(animation_layer_def& layer, size_t state_index, const std::string& new_name);

/**
 * @brief Removes a state and every transition from or to it.
 */
void remove_animation_state(animation_layer_def& layer, size_t state_index);

/**
 * @brief Renames a parameter and every condition, blend axis and speed parameter using it.
 */
void rename_animation_parameter(animation_controller& controller, size_t parameter_index, const std::string& new_name);

/**
 * @brief The weight of every bone of a skeleton under a layer mask: the nearest entry from the bone
 * up its hierarchy that reaches it (its own, or an ancestor's with include_children), clamped to
 * 0..1. Bones no entry reaches get 0; a bone listed twice takes its last entry. `parents` holds -1
 * for roots.
 */
auto compute_animation_mask_weights(const std::vector<animation_mask_entry>& mask,
                                    const std::vector<std::string>& names,
                                    const std::vector<int>& parents) -> std::vector<float>;

} // namespace unravel
