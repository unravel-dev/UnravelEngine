#include "animation_controller_ui.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <filesystem/filesystem.h>
#include <logging/logging.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace unravel
{
namespace
{
constexpr const char* ELLIPSIS = "...";
} // namespace

auto make_unique_name(const std::function<bool(const std::string&)>& exists, const std::string& base) -> std::string
{
    if(!exists(base))
    {
        return base;
    }
    for(int i = 1;; ++i)
    {
        std::string candidate = base + " " + std::to_string(i);
        if(!exists(candidate))
        {
            return candidate;
        }
    }
}

auto make_unique_state_name(const animation_layer_def& layer, const std::string& base) -> std::string
{
    return make_unique_name(
        [&](const std::string& name)
        {
            return find_animation_state(layer, name) >= 0;
        },
        base);
}

auto get_default_state(const animation_layer_def& layer) -> int
{
    if(layer.default_state.empty())
    {
        return layer.states.empty() ? -1 : 0;
    }
    return find_animation_state(layer, layer.default_state);
}

namespace
{
/// Grows the string ImGui edits in place.
auto resize_edited_string(ImGuiInputTextCallbackData* data) -> int
{
    if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        auto* value = static_cast<std::string*>(data->UserData);
        value->resize(size_t(data->BufTextLen));
        data->Buf = value->data();
    }
    return 0;
}
} // namespace

auto input_text_string(const char* label, std::string& value) -> bool
{
    return ImGui::InputText(label,
                            value.data(),
                            value.capacity() + 1,
                            ImGuiInputTextFlags_CallbackResize,
                            resize_edited_string,
                            &value);
}

auto fit_text(ImFont* font, float size, const std::string& text, float max_width) -> std::string
{
    const auto get_width = [&](const std::string& candidate)
    {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x;
    };
    if(get_width(text) <= max_width)
    {
        return text;
    }
    // Byte offset of every code point boundary: a cut never splits a UTF-8 sequence.
    std::vector<size_t> boundaries{0};
    const char* const begin = text.c_str();
    const char* const end = begin + text.size();
    for(const char* it = begin; it < end;)
    {
        unsigned int code_point = 0;
        it += std::max(ImTextCharFromUtf8(&code_point, it, end), 1);
        boundaries.push_back(size_t(it - begin));
    }
    // Binary search on the number of code points kept: `fits` does, `too_long` does not.
    size_t fits = 0;
    size_t too_long = boundaries.size() - 1;
    while(too_long - fits > 1)
    {
        const size_t middle = (fits + too_long) / 2;
        if(get_width(text.substr(0, boundaries[middle]) + ELLIPSIS) <= max_width)
        {
            fits = middle;
        }
        else
        {
            too_long = middle;
        }
    }
    return text.substr(0, boundaries[fits]) + ELLIPSIS;
}

auto get_clip_label(const asset_handle<animation_clip>& clip) -> std::string
{
    if(!clip)
    {
        return "None";
    }
    return fs::path(clip.id()).stem().string();
}

auto get_motion_name(animation_motion_type motion) -> const char*
{
    switch(motion)
    {
        case animation_motion_type::clip:
            return "Clip";
        case animation_motion_type::blend_1d:
            return "Blend 1D";
        case animation_motion_type::blend_2d_cartesian:
            return "Blend 2D Cartesian";
        case animation_motion_type::blend_2d_directional:
            return "Blend 2D Directional";
    }
    return "";
}

auto get_parameter_type_name(animation_parameter_type type) -> const char*
{
    switch(type)
    {
        case animation_parameter_type::float_value:
            return "Float";
        case animation_parameter_type::int_value:
            return "Int";
        case animation_parameter_type::bool_value:
            return "Bool";
        case animation_parameter_type::trigger:
            return "Trigger";
    }
    return "";
}

auto get_parameter_icon(animation_parameter_type type) -> const char*
{
    switch(type)
    {
        case animation_parameter_type::float_value:
            return ICON_MDI_DECIMAL;
        case animation_parameter_type::int_value:
            return ICON_MDI_NUMERIC;
        case animation_parameter_type::bool_value:
            return ICON_MDI_TOGGLE_SWITCH_OUTLINE;
        case animation_parameter_type::trigger:
            return ICON_MDI_RADIOBOX_MARKED;
    }
    return "";
}

auto get_condition_mode_name(animation_condition_mode mode) -> const char*
{
    switch(mode)
    {
        case animation_condition_mode::greater:
            return "Greater";
        case animation_condition_mode::less:
            return "Less";
        case animation_condition_mode::equals:
            return "Equals";
        case animation_condition_mode::not_equals:
            return "Not Equal";
        case animation_condition_mode::is_true:
            return "True";
        case animation_condition_mode::is_false:
            return "False";
    }
    return "";
}

auto get_condition_modes(animation_parameter_type type) -> std::vector<animation_condition_mode>
{
    switch(type)
    {
        case animation_parameter_type::float_value:
            return {animation_condition_mode::greater, animation_condition_mode::less};
        case animation_parameter_type::int_value:
            return {animation_condition_mode::greater,
                    animation_condition_mode::less,
                    animation_condition_mode::equals,
                    animation_condition_mode::not_equals};
        case animation_parameter_type::bool_value:
            return {animation_condition_mode::is_true, animation_condition_mode::is_false};
        case animation_parameter_type::trigger:
            return {animation_condition_mode::is_true};
    }
    return {};
}

auto describe_condition(const animation_controller& controller, const animation_condition_def& condition) -> std::string
{
    const int index = controller.find_parameter(condition.parameter);
    if(index < 0)
    {
        return fmt::format("{} (no such parameter)", condition.parameter.empty() ? "?" : condition.parameter);
    }
    const auto& name = condition.parameter;
    switch(controller.parameters[size_t(index)].type)
    {
        case animation_parameter_type::float_value:
            return fmt::format("{} {} {:.2f}", name, condition.mode == animation_condition_mode::less ? "<" : ">", condition.threshold);
        case animation_parameter_type::int_value:
        {
            const char* op = condition.mode == animation_condition_mode::less     ? "<"
                             : condition.mode == animation_condition_mode::equals ? "=="
                             : condition.mode == animation_condition_mode::not_equals ? "!="
                                                                                      : ">";
            return fmt::format("{} {} {}", name, op, int(std::lround(condition.threshold)));
        }
        case animation_parameter_type::bool_value:
            return fmt::format("{} is {}", name, condition.mode == animation_condition_mode::is_false ? "false" : "true");
        case animation_parameter_type::trigger:
            return fmt::format("{} (trigger)", name);
    }
    return name;
}

} // namespace unravel
