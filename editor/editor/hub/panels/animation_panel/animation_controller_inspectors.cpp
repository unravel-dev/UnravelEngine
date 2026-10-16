#include "animation_controller_inspectors.h"
#include "animation_controller_ui.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <engine/meta/animation/animation_controller.hpp>

#include <imgui/imgui.h>
#include <imgui_widgets/tooltips.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace unravel
{
namespace
{
/// Share of a condition row the parameter takes once the comparison fits; the threshold gets the rest.
constexpr float CONDITION_PARAMETER_SHARE = 0.55f;

const ImVec4 COL_NAME_REFUSED(1.0f, 0.72f, 0.3f, 1.0f);
const ImVec4 COL_NAME_REFUSED_FRAME(0.9f, 0.3f, 0.25f, 1.0f);

const animation_controller_inspect_scope::context* current_context = nullptr;
bool is_name_focus_requested = false;

/// The name being typed into the name field `field_id` and why it is refused (empty when it is not).
struct name_field_state
{
    std::string buffer;
    std::string error;
    ImGuiID field_id{};
};
name_field_state name_field;

struct name_edit
{
    /// The edit ended on a new, accepted name: rename to name_field.buffer.
    bool renamed{};
};

auto is_numeric(animation_parameter_type type) -> bool
{
    return type == animation_parameter_type::float_value || type == animation_parameter_type::int_value;
}

/// Width of a comparison combo that shows every mode of `type` in full.
auto get_mode_combo_width(animation_parameter_type type) -> float
{
    float widest = 0.0f;
    for(auto mode : get_condition_modes(type))
    {
        widest = std::max(widest, ImGui::CalcTextSize(get_condition_mode_name(mode)).x);
    }
    return widest + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
}

/// Index of `item` in `items` when it is one of them.
template<typename T>
auto find_index(std::vector<T>& items, const T& item) -> std::optional<size_t>
{
    if(items.empty() || &item < items.data() || &item >= items.data() + items.size())
    {
        return std::nullopt;
    }
    return size_t(&item - items.data());
}

/**
 * @brief A name input that only accepts a new, non-empty name `reject` lets through; a refused one
 * keeps the field red with the reason under it until it is fixed or the edit ends.
 *
 * While the field is active only it shows the typed name; the rename happens once, when the edit
 * ends, so references move from the name the item had to the name it gets and never pick up an
 * intermediate name (one a dangling reference may have).
 */
auto draw_name_field(const std::string& current, const std::function<const char*(const std::string&)>& reject) -> name_edit
{
    name_edit edit;
    const ImGuiID id = ImGui::GetID("##name");
    const bool shows_error = name_field.field_id == id && !name_field.error.empty();
    if(is_name_focus_requested)
    {
        is_name_focus_requested = false;
        ImGui::SetKeyboardFocusHere();
    }
    if(shows_error)
    {
        ImGui::PushStyleColor(ImGuiCol_Border, COL_NAME_REFUSED_FRAME);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }
    // The current name goes in every frame; the field puts the typed text back into the buffer
    // while it is active, and the final text on the frame its edit ends.
    std::string text = current;
    input_text_string("##name", text);
    if(ImGui::IsItemActive() || ImGui::IsItemDeactivated())
    {
        name_field.buffer = text;
        name_field.field_id = id;
        const char* reason = nullptr;
        if(name_field.buffer.empty())
        {
            reason = "A name is required.";
        }
        else if(name_field.buffer != current)
        {
            reason = reject(name_field.buffer);
        }
        name_field.error = reason ? reason : "";
    }
    edit.renamed = ImGui::IsItemDeactivatedAfterEdit() && name_field.error.empty() && name_field.buffer != current;
    if(ImGui::IsItemDeactivated())
    {
        name_field.error.clear();
    }
    if(shows_error)
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::TextColored(COL_NAME_REFUSED, ICON_MDI_ALERT " %s", name_field.error.c_str());
    }
    return edit;
}

/// The name property as a name field; `apply` renames (keeping references) with the final name, as
/// one finished edit. A refused or unchanged name reports nothing.
auto draw_name_property(entt::meta_any& object,
                        const entt::meta_data& prop,
                        const std::string& current,
                        const std::function<const char*(const std::string&)>& reject,
                        const std::function<void(const std::string&)>& apply) -> inspect_result
{
    inspect_result result;
    property_layout layout(prop, object);
    if(draw_name_field(current, reject).renamed)
    {
        apply(name_field.buffer);
        result.changed = true;
        result.edit_finished = true;
    }
    return result;
}

/// A combo of the controller's parameters; a name no parameter has shows flagged.
auto draw_parameter_picker(const char* id,
                           const animation_controller& controller,
                           std::string& value,
                           bool numeric_only,
                           bool allow_none) -> bool
{
    bool changed = false;
    const bool is_missing = !value.empty() && controller.find_parameter(value) < 0;
    const std::string preview = value.empty() ? std::string("None") : (is_missing ? ICON_MDI_ALERT " " + value : value);
    if(ImGui::BeginCombo(id, preview.c_str()))
    {
        if(allow_none && ImGui::Selectable("None", value.empty()))
        {
            value.clear();
            changed = true;
        }
        for(const auto& parameter : controller.parameters)
        {
            if(numeric_only && !is_numeric(parameter.type))
            {
                continue;
            }
            const std::string item = std::string(get_parameter_icon(parameter.type)) + " " + parameter.name;
            if(ImGui::Selectable(item.c_str(), parameter.name == value))
            {
                value = parameter.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if(is_missing)
    {
        ImGui::SetItemTooltipEx("No parameter is named '%s'.", value.c_str());
    }
    return changed;
}

/// A combo of the layer's states; `empty_label` names the empty value.
auto draw_state_picker(const animation_layer_def& layer, std::string& value, const char* empty_label) -> bool
{
    bool changed = false;
    const bool is_missing = !value.empty() && find_animation_state(layer, value) < 0;
    const std::string preview = value.empty() ? std::string(empty_label) : (is_missing ? ICON_MDI_ALERT " " + value : value);
    if(ImGui::BeginCombo("##state", preview.c_str()))
    {
        for(const auto& state : layer.states)
        {
            if(ImGui::Selectable(state.name.c_str(), state.name == value))
            {
                value = state.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/// A picker in the property's row; a pick is a finished edit.
template<typename Picker>
auto draw_picker_property(entt::meta_any& object, const entt::meta_data& prop, const Picker& pick) -> inspect_result
{
    inspect_result result;
    property_layout layout(prop, object);
    if(pick())
    {
        result.changed = true;
        result.edit_finished = true;
    }
    return result;
}

/// Conditions on the parameter switch to a comparison its type has.
void fit_conditions_to_type(animation_controller& controller, const animation_parameter_def& parameter)
{
    const auto modes = get_condition_modes(parameter.type);
    for(auto& layer : controller.layers)
    {
        for(auto& transition : layer.transitions)
        {
            for(auto& condition : transition.conditions)
            {
                if(condition.parameter == parameter.name &&
                   std::find(modes.begin(), modes.end(), condition.mode) == modes.end())
                {
                    condition.mode = modes.front();
                }
            }
        }
    }
}

auto get_layer(const animation_controller_inspect_scope::context& scope) -> animation_layer_def*
{
    return scope.layer < scope.controller->layers.size() ? &scope.controller->layers[scope.layer] : nullptr;
}

/// The controller's own property `name` in a row, as the inspector draws it.
auto inspect_controller_property(rtti::context& ctx, animation_controller& controller, const entt::hashed_string& name)
    -> inspect_result
{
    entt::meta_any object = entt::forward_as_meta(controller);
    const auto prop = entt::resolve<animation_controller>().data(name);
    return prop ? inspect_property(ctx, object, make_proxy(object), prop) : inspect_result{};
}
} // namespace

animation_controller_inspect_scope::animation_controller_inspect_scope(context scope_context)
    : context_(std::move(scope_context))
    , previous_(current_context)
{
    current_context = &context_;
}

animation_controller_inspect_scope::~animation_controller_inspect_scope()
{
    current_context = previous_;
}

auto animation_controller_inspect_scope::get_current() -> const context*
{
    return current_context != nullptr && current_context->controller != nullptr ? current_context : nullptr;
}

void animation_controller_inspect_scope::request_name_focus()
{
    is_name_focus_requested = true;
}

void animation_controller_inspect_scope::cancel_name_focus()
{
    is_name_focus_requested = false;
}

auto inspector_animation_parameter_def::inspect(rtti::context& ctx,
                                                entt::meta_any& var,
                                                const meta_any_proxy& var_proxy,
                                                const var_info& info,
                                                const entt::meta_custom& custom) -> inspect_result
{
    const auto* scope = animation_controller_inspect_scope::get_current();
    auto& parameter = var.cast<animation_parameter_def&>();
    const auto index = scope ? find_index(scope->controller->parameters, parameter) : std::nullopt;
    if(!index)
    {
        return inspect_var_properties(ctx, var, var_proxy, info, custom);
    }
    auto& controller = *scope->controller;
    return inspect_var_properties(
        ctx,
        var,
        var_proxy,
        info,
        custom,
        [&](entt::meta_any& object, const entt::meta_data& prop, inspect_result& result)
        {
            const auto name = entt::get_name(prop);
            if(name == "name")
            {
                result |= draw_name_property(
                    object,
                    prop,
                    parameter.name,
                    [&](const std::string& typed) -> const char*
                    {
                        return controller.find_parameter(typed) >= 0 ? "Another parameter has this name." : nullptr;
                    },
                    [&](const std::string& typed)
                    {
                        rename_animation_parameter(controller, *index, typed);
                    });
                return true;
            }
            if(name == "type")
            {
                const auto before = parameter.type;
                result |= inspect_property(ctx, object, var_proxy, prop);
                if(parameter.type != before)
                {
                    fit_conditions_to_type(controller, parameter);
                }
                return true;
            }
            return false;
        });
}

auto inspector_animation_condition_def::inspect(rtti::context& ctx,
                                                entt::meta_any& var,
                                                const meta_any_proxy& var_proxy,
                                                const var_info& info,
                                                const entt::meta_custom& custom) -> inspect_result
{
    const auto* scope = animation_controller_inspect_scope::get_current();
    if(scope == nullptr)
    {
        return inspect_var_properties(ctx, var, var_proxy, info, custom);
    }
    auto& condition = var.cast<animation_condition_def&>();
    const auto& controller = *scope->controller;
    inspect_result result;
    const int parameter_index = controller.find_parameter(condition.parameter);
    const auto type = parameter_index >= 0 ? controller.parameters[size_t(parameter_index)].type
                                           : animation_parameter_type::float_value;
    const auto modes = get_condition_modes(type);
    const bool needs_threshold = is_numeric(type);
    // A trigger has one comparison only: the row shows just its parameter.
    const bool needs_mode = modes.size() > 1;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float mode_width = needs_mode ? get_mode_combo_width(type) : 0.0f;
    const float gaps = spacing * float(int(needs_mode) + int(needs_threshold));
    const float rest = ImGui::GetContentRegionAvail().x - mode_width - gaps;
    ImGui::SetNextItemWidth(needs_threshold ? rest * CONDITION_PARAMETER_SHARE : rest);
    if(draw_parameter_picker("##parameter", controller, condition.parameter, false, false))
    {
        const int picked = controller.find_parameter(condition.parameter);
        const auto picked_modes = get_condition_modes(controller.parameters[size_t(picked)].type);
        if(std::find(picked_modes.begin(), picked_modes.end(), condition.mode) == picked_modes.end())
        {
            condition.mode = picked_modes.front();
        }
        result.changed = true;
        result.edit_finished = true;
    }
    if(needs_mode)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(mode_width);
        if(ImGui::BeginCombo("##mode", get_condition_mode_name(condition.mode)))
        {
            for(auto mode : modes)
            {
                if(ImGui::Selectable(get_condition_mode_name(mode), condition.mode == mode))
                {
                    condition.mode = mode;
                    result.changed = true;
                    result.edit_finished = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    if(needs_threshold)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if(type == animation_parameter_type::int_value)
        {
            int value = int(std::lround(condition.threshold));
            if(ImGui::DragInt("##threshold", &value))
            {
                condition.threshold = float(value);
            }
        }
        else
        {
            ImGui::DragFloat("##threshold", &condition.threshold, 0.01f);
        }
        result.changed |= ImGui::IsItemEdited();
        result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
    }
    return result;
}

auto inspector_animation_state_def::inspect(rtti::context& ctx,
                                            entt::meta_any& var,
                                            const meta_any_proxy& var_proxy,
                                            const var_info& info,
                                            const entt::meta_custom& custom) -> inspect_result
{
    const auto* scope = animation_controller_inspect_scope::get_current();
    auto& state = var.cast<animation_state_def&>();
    auto* layer = scope ? get_layer(*scope) : nullptr;
    const auto index = layer ? find_index(layer->states, state) : std::nullopt;
    if(!index)
    {
        return inspect_var_properties(ctx, var, var_proxy, info, custom);
    }
    const auto& controller = *scope->controller;
    return inspect_var_properties(
        ctx,
        var,
        var_proxy,
        info,
        custom,
        [&](entt::meta_any& object, const entt::meta_data& prop, inspect_result& result)
        {
            const auto name = entt::get_name(prop);
            if(name == "name")
            {
                result |= draw_name_property(
                    object,
                    prop,
                    state.name,
                    [&](const std::string& typed) -> const char*
                    {
                        return find_animation_state(*layer, typed) >= 0 ? "Another state of this layer has this name."
                                                                        : nullptr;
                    },
                    [&](const std::string& typed)
                    {
                        rename_animation_state(*layer, *index, typed);
                    });
                return true;
            }
            if(name == "parameter_x" || name == "parameter_y" || name == "speed_parameter")
            {
                auto& value = name == "parameter_x" ? state.parameter_x
                              : name == "parameter_y" ? state.parameter_y
                                                      : state.speed_parameter;
                const bool allow_none = name == "speed_parameter";
                result |= draw_picker_property(object,
                                               prop,
                                               [&]()
                                               {
                                                   return draw_parameter_picker("##parameter", controller, value, true, allow_none);
                                               });
                return true;
            }
            if(name == "points")
            {
                if(scope->draw_blend_plot)
                {
                    result |= scope->draw_blend_plot(state);
                }
                result |= inspect_property(ctx, object, var_proxy, prop);
                return true;
            }
            // Moved by dragging the node in the graph.
            return name == "editor_position";
        });
}

auto inspector_animation_transition_def::inspect(rtti::context& ctx,
                                                 entt::meta_any& var,
                                                 const meta_any_proxy& var_proxy,
                                                 const var_info& info,
                                                 const entt::meta_custom& custom) -> inspect_result
{
    const auto* scope = animation_controller_inspect_scope::get_current();
    auto& transition = var.cast<animation_transition_def&>();
    auto* layer = scope ? get_layer(*scope) : nullptr;
    if(layer == nullptr || !find_index(layer->transitions, transition))
    {
        return inspect_var_properties(ctx, var, var_proxy, info, custom);
    }
    return inspect_var_properties(
        ctx,
        var,
        var_proxy,
        info,
        custom,
        [&](entt::meta_any& object, const entt::meta_data& prop, inspect_result& result)
        {
            const auto name = entt::get_name(prop);
            if(name == "source")
            {
                // The arrow's tail: made by Make Transition, not edited here.
                property_layout layout(prop, object);
                ImGui::TextUnformatted(transition.source.empty() ? ICON_MDI_ASTERISK " Any State" : transition.source.c_str());
                return true;
            }
            if(name == "destination")
            {
                result |= draw_picker_property(object,
                                               prop,
                                               [&]()
                                               {
                                                   return draw_state_picker(*layer, transition.destination, "None");
                                               });
                return true;
            }
            return false;
        });
}

auto inspector_animation_layer_def::inspect(rtti::context& ctx,
                                            entt::meta_any& var,
                                            const meta_any_proxy& var_proxy,
                                            const var_info& info,
                                            const entt::meta_custom& custom) -> inspect_result
{
    const auto* scope = animation_controller_inspect_scope::get_current();
    auto& layer = var.cast<animation_layer_def&>();
    const auto index = scope ? find_index(scope->controller->layers, layer) : std::nullopt;
    if(!index)
    {
        return inspect_var_properties(ctx, var, var_proxy, info, custom);
    }
    auto& controller = *scope->controller;
    const bool is_base = *index == 0;
    return inspect_var_properties(
        ctx,
        var,
        var_proxy,
        info,
        custom,
        [&](entt::meta_any& object, const entt::meta_data& prop, inspect_result& result)
        {
            const auto name = entt::get_name(prop);
            if(name == "name")
            {
                result |= draw_name_property(
                    object,
                    prop,
                    layer.name,
                    [&](const std::string& typed) -> const char*
                    {
                        return controller.find_layer(typed) >= 0 ? "Another layer has this name." : nullptr;
                    },
                    [&](const std::string& typed)
                    {
                        layer.name = typed;
                    });
                return true;
            }
            if(name == "weight" && is_base)
            {
                property_layout layout(prop, object);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Always 1: the base layer.");
                ImGui::PopStyleColor();
                return true;
            }
            if(name == "blend_mode")
            {
                // The base layer has nothing below it to override or add to.
                return is_base;
            }
            if(name == "mask")
            {
                if(is_base)
                {
                    return true;
                }
                result |= inspect_controller_property(ctx, controller, "preview_model"_hs);
                if(scope->draw_mask_bones)
                {
                    result |= scope->draw_mask_bones(layer);
                }
                result |= inspect_property(ctx, object, var_proxy, prop);
                return true;
            }
            if(name == "default_state")
            {
                result |= draw_picker_property(object,
                                               prop,
                                               [&]()
                                               {
                                                   return draw_state_picker(layer, layer.default_state, "(first state)");
                                               });
                return true;
            }
            // States, transitions and node positions are edited in the graph.
            return name == "states" || name == "transitions" || name == "entry_position" || name == "any_state_position";
        });
}

} // namespace unravel
