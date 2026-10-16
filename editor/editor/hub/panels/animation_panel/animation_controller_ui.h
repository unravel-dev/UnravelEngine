#pragma once

#include <engine/animation/animation_controller.h>

#include <functional>
#include <string>
#include <vector>

struct ImFont;

namespace unravel
{

/**
 * @brief What an editing widget of the controller editor did this frame, so its owner can take an
 * undo snapshot before the first change of an interaction and save once it finishes.
 */
struct animation_edit_result
{
    /// The definition is about to change (first change of an interaction).
    bool begin_edit{};
    /// The definition changed in a way running controllers see.
    bool changed{};
    /// Editor layout changed (node positions): saved, but running controllers need no rebuild.
    bool layout_changed{};
    /// The interaction finished; save.
    bool finished{};

    /// Reports a one-step edit: begun, changed and finished at once.
    void set_committed()
    {
        begin_edit = true;
        changed = true;
        finished = true;
    }
};

/// `base`, or `base` with the first free number appended while `exists` reports the name taken.
auto make_unique_name(const std::function<bool(const std::string&)>& exists, const std::string& base) -> std::string;

/// `base`, or `base` numbered, as a name no state of `layer` has.
auto make_unique_state_name(const animation_layer_def& layer, const std::string& base) -> std::string;

/// Index of the state the layer starts in: its default state, else the first; -1 when there is none.
auto get_default_state(const animation_layer_def& layer) -> int;

/// `text` at `size`, shortened with "..." to fit `max_width` pixels: the longest prefix of whole
/// code points that fits.
auto fit_text(ImFont* font, float size, const std::string& text, float max_width) -> std::string;

/// A text input editing `value` in place, growing it as it is typed, so text of any length is kept
/// whole. True when the text changed this frame.
auto input_text_string(const char* label, std::string& value) -> bool;

/// The clip's file name without extension; "None" without a clip.
auto get_clip_label(const asset_handle<animation_clip>& clip) -> std::string;

auto get_motion_name(animation_motion_type motion) -> const char*;
auto get_parameter_type_name(animation_parameter_type type) -> const char*;
auto get_parameter_icon(animation_parameter_type type) -> const char*;
auto get_condition_mode_name(animation_condition_mode mode) -> const char*;

/// Condition modes that make sense for a parameter type, the default one first.
auto get_condition_modes(animation_parameter_type type) -> std::vector<animation_condition_mode>;

/// A condition as one line ("Speed > 0.50", "Grounded is true"); flags a parameter that does not exist.
auto describe_condition(const animation_controller& controller, const animation_condition_def& condition) -> std::string;

} // namespace unravel
