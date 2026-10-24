#include "inspector_math.h"
#include "entt/meta/meta.hpp"
#include "imgui_widgets/tooltips.h"
#include "inspector_coretypes.h"
#include "inspector_basetypes.h"
#include "inspectors.h"
#include "entt/meta/resolve.hpp"
#include "imgui/imgui.h"
#include <imgui/imgui_internal.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>

#include <cstring>
#include <string>

namespace unravel
{
namespace
{    


float DRAG_SPEED = 0.01f;

/// A vector's fields name their axis with a colored edge rather than a button, so the numbers keep the room.
constexpr float AXIS_EDGE_WIDTH = 2.0f;
constexpr std::array<const char*, 4> AXIS_NAMES{{"X", "Y", "Z", "W"}};
constexpr std::array<ImU32, 4> AXIS_EDGE_COLORS{
    {IM_COL32(205, 72, 64, 255), IM_COL32(92, 172, 74, 255), IM_COL32(74, 123, 217, 255), IM_COL32(64, 172, 172, 255)}};
/// Below this width, in font heights, a color drops its channel fields: one swatch the width of the value.
constexpr float COMPACT_COLOR_EM = 14.0f;

/// A vector's fields, each with a colored edge naming its axis. A field's context menu resets its component when
/// the vector has a reset value.
bool drag_vector_fields(ImGuiDataType data_type,
                        void* data,
                        int components,
                        const void* reset,
                        const char* format)
{
    const float width = ImGui::CalcItemWidth();
    const ImGuiStyle& style = ImGui::GetStyle();
    const size_t type_size = ImGui::DataTypeGetInfo(data_type)->Size;
    auto* component = static_cast<char*>(data);
    const auto* reset_component = static_cast<const char*>(reset);
    bool changed = false;
    ImGui::BeginGroup();
    ImGui::PushID("##compact_vector");
    ImGui::PushMultiItemsWidths(components, width);
    for(int i = 0; i < components; ++i)
    {
        ImGui::PushID(i);
        if(i > 0)
        {
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        }
        changed |= ImGui::DragScalar("", data_type, component, DRAG_SPEED, nullptr, nullptr, format);
        ImGui::DrawItemActivityOutline();
        const ImVec2 min = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddRectFilled(min,
                                                  ImVec2(min.x + AXIS_EDGE_WIDTH, ImGui::GetItemRectMax().y),
                                                  AXIS_EDGE_COLORS[size_t(i) % AXIS_EDGE_COLORS.size()]);
        if(reset_component != nullptr && ImGui::BeginPopupContextItem("##axis_menu"))
        {
            {
                ImGui::ContextMenuStyleScope style_scope;
                const std::string item = std::string("Reset ") + AXIS_NAMES[size_t(i) % AXIS_NAMES.size()];
                if(ImGui::MenuItem(item.c_str()))
                {
                    std::memcpy(component, reset_component, type_size);
                    changed = true;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopItemWidth();
        component += type_size;
        if(reset_component != nullptr)
        {
            reset_component += type_size;
        }
    }
    ImGui::PopID();
    ImGui::EndGroup();
    return changed;
}

/// What a color edit did: changed the value, and finished an edit.
struct color_edit_result
{
    bool changed{};
    bool edit_finished{};
};

/// A color as one swatch the width of the value, opening the picker: for a value too narrow for the channels.
auto edit_color_swatch(math::vec4& value, bool is_opaque) -> color_edit_result
{
    color_edit_result result{};
    const ImVec2 size(ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
    const ImVec4 shown(value.x, value.y, value.z, is_opaque ? 1.0f : value.w);
    const ImGuiColorEditFlags swatch_flags =
        is_opaque ? ImGuiColorEditFlags_NoAlpha : ImGuiColorEditFlags_AlphaPreviewHalf;
    if(ImGui::ColorButton("##swatch", shown, swatch_flags, size))
    {
        ImGui::OpenPopup("##swatch_picker");
    }
    if(ImGui::BeginPopup("##swatch_picker"))
    {
        result.changed = is_opaque ? ImGui::ColorPicker3("##picker", math::value_ptr(value))
                                   : ImGui::ColorPicker4("##picker",
                                                         math::value_ptr(value),
                                                         ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
        result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndPopup();
    }
    return result;
}

auto quat_to_vec4(math::quat q) -> math::vec4
{
    math::vec4 v;
    v.x = q.x;
    v.y = q.y;
    v.z = q.z;
    v.w = q.w;
    return v;
}
auto vec4_to_quat(math::vec4 v) -> math::quat
{
    math::quat q;
    q.x = v.x;
    q.y = v.y;
    q.z = v.z;
    q.w = v.w;
    return q;
}

bool DragFloat2(math::vec2& data, const var_info& info, std::array<const char*, 2> formats = {{"X:%.2f", "Y:%.2f"}})
{
    bool result = ImGui::DragMultiFormatScalarN("##",
                                                ImGuiDataType_Float,
                                                math::value_ptr(data),
                                                2,
                                                DRAG_SPEED,
                                                nullptr,
                                                nullptr,
                                                formats.data());
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragFloat3(math::vec3& data,
                const var_info& info,
                std::array<const char*, 3> formats = {{"X:%.3f", "Y:%.3f", "Z:%.3f"}})
{
    bool result = ImGui::DragMultiFormatScalarN("##",
                                                ImGuiDataType_Float,
                                                math::value_ptr(data),
                                                3,
                                                DRAG_SPEED,
                                                nullptr,
                                                nullptr,
                                                formats.data());
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragFloat4(math::vec4& data,
                const var_info& info,
                std::array<const char*, 4> formats = {{"X:%.3f", "Y:%.3f", "Z:%.3f", "W:%.3f"}})
{
    bool result = ImGui::DragMultiFormatScalarN("##",
                                                ImGuiDataType_Float,
                                                math::value_ptr(data),
                                                4,
                                                DRAG_SPEED,
                                                nullptr,
                                                nullptr,
                                                formats.data());
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragVec2(math::vec2& data, const var_info& info, const math::vec2* reset = nullptr, const char* format = "%.3f")
{
    bool result = drag_vector_fields(ImGuiDataType_Float,
                                     math::value_ptr(data),
                                     data.length(),
                                     reset ? math::value_ptr(*reset) : nullptr,
                                     format);
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragVec3(math::vec3& data, const var_info& info, const math::vec3* reset = nullptr, const char* format = "%.3f")
{
    bool result = drag_vector_fields(ImGuiDataType_Float,
                                     math::value_ptr(data),
                                     data.length(),
                                     reset ? math::value_ptr(*reset) : nullptr,
                                     format);

    ImGui::ActiveItemWrapMousePos();

    return result;
}

/**
 * Proxy for transform rotation as authored Euler degrees.
 * Using the reflected quat "rotation" property would call set_rotation and drop the Euler hint.
 * Setter always writes a by-value transform so the hint survives the meta_any assign chain.
 */
auto make_euler_rotation_proxy(const meta_any_proxy& transform_proxy) -> meta_any_proxy
{
    meta_any_proxy proxy;
    proxy.impl->parent = transform_proxy.impl;
    proxy.impl->type_name = "Rotation";
    proxy.impl->name = [&]()
    {
        const auto& name = transform_proxy.impl->name;
        if(name.empty())
        {
            return std::string("Rotation");
        }
        return fmt::format("{}/Rotation", name);
    }();
    proxy.impl->getter = [parent_proxy = transform_proxy](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            const auto& transform = var.cast<const math::transform&>();
            result = transform.get_rotation_euler_degrees();
            return true;
        }
        return false;
    };
    proxy.impl->setter = [parent_proxy = transform_proxy](meta_any_proxy& /*proxy*/,
                                                          const entt::meta_any& value,
                                                          uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(!parent_proxy.impl->getter(var) || !var)
        {
            return false;
        }
        // Force a real copy so we never mutate through a transient meta reference and then
        // lose the hint when the parent setter re-gets + assign()s.
        math::transform transform = var.cast<math::transform>();
        transform.set_rotation_euler_degrees(value.cast<math::vec3>());
        entt::meta_any written = transform;
        return parent_proxy.impl->setter(parent_proxy, written, execution_count);
    };
    return proxy;
}

bool DragVec4(math::vec4& data, const var_info& info, const math::vec4* reset = nullptr, const char* format = "%.3f")
{
    bool result = drag_vector_fields(ImGuiDataType_Float,
                                     math::value_ptr(data),
                                     data.length(),
                                     reset ? math::value_ptr(*reset) : nullptr,
                                     format);

    ImGui::ActiveItemWrapMousePos();

    return result;
}

} // namespace

auto inspector_bvec2::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::bvec2&>();
    inspect_result result{};

    enum bflags
    {
        none = 0,
        x = 1 << 0,
        y = 1 << 1,
    };

    int flags = 0;
    flags |= data.x ? bflags::x : 0;
    flags |= data.y ? bflags::y : 0;

    bool mod = false;
    ImGui::BeginGroup();
    mod |= ImGui::CheckboxFlags("X", &flags, bflags::x);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Y", &flags, bflags::y);
    ImGui::EndGroup();
    if(mod)
    {
        data.x = flags & bflags::x;
        data.y = flags & bflags::y;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_bvec3::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::bvec3&>();
    inspect_result result{};

    enum bflags
    {
        none = 0,
        x = 1 << 0,
        y = 1 << 1,
        z = 1 << 2,
    };

    int flags = 0;
    flags |= data.x ? bflags::x : 0;
    flags |= data.y ? bflags::y : 0;
    flags |= data.z ? bflags::z : 0;

    bool mod = false;
    ImGui::BeginGroup();
    mod |= ImGui::CheckboxFlags("X", &flags, bflags::x);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Y", &flags, bflags::y);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Z", &flags, bflags::z);
    ImGui::EndGroup();

    if(mod)
    {
        data.x = flags & bflags::x;
        data.y = flags & bflags::y;
        data.z = flags & bflags::z;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_bvec4::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::bvec4&>();
    inspect_result result{};

    enum bflags
    {
        none = 0,
        x = 1 << 0,
        y = 1 << 1,
        z = 1 << 2,
        w = 1 << 3,
    };

    int flags = 0;
    flags |= data.x ? bflags::x : 0;
    flags |= data.y ? bflags::y : 0;
    flags |= data.z ? bflags::z : 0;
    flags |= data.w ? bflags::w : 0;

    bool mod = false;
    ImGui::BeginGroup();
    mod |= ImGui::CheckboxFlags("X", &flags, bflags::x);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Y", &flags, bflags::y);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Z", &flags, bflags::z);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("W", &flags, bflags::w);
    ImGui::EndGroup();

    if(mod)
    {
        data.x = flags & bflags::x;
        data.y = flags & bflags::y;
        data.z = flags & bflags::z;
        data.w = flags & bflags::w;

        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec2::inspect(rtti::context& ctx,
                             entt::meta_any& var,
                             const meta_any_proxy& var_proxy,
                             const var_info& info,
                             const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::vec2&>();
    inspect_result result{};

    static const auto reset = math::zero<math::vec2>();

    if(DragVec2(data, info, &reset))
    {
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec3::inspect(rtti::context& ctx,
                             entt::meta_any& var,
                             const meta_any_proxy& var_proxy,
                             const var_info& info,
                             const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::vec3&>();
    inspect_result result{};

    static const auto reset = math::zero<math::vec3>();

    if(DragVec3(data, info, &reset))
    {
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec4::inspect(rtti::context& ctx,
                             entt::meta_any& var,
                             const meta_any_proxy& var_proxy,
                             const var_info& info,
                             const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::vec4&>();
    inspect_result result{};

    static const auto reset = math::zero<math::vec4>();

    if(DragVec4(data, info, &reset))
    {
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_color::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::color&>();
    inspect_result result{};

    // "opaque": the property uses no alpha, so none is shown.
    const bool is_opaque = entt::get_attribute_as<bool>(custom, "opaque");
    if(ImGui::CalcItemWidth() < COMPACT_COLOR_EM * ImGui::GetFontSize())
    {
        const color_edit_result swatch = edit_color_swatch(data.value, is_opaque);
        result.changed = swatch.changed;
        result.edit_finished = swatch.edit_finished;
        return result;
    }
    const bool is_edited = is_opaque ? ImGui::ColorEdit3("##", math::value_ptr(data.value))
                                     : ImGui::ColorEdit4("##",
                                                         math::value_ptr(data.value),
                                                         ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    if(is_edited)
    {
        result.changed = true;
    }

    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_quaternion::inspect(rtti::context& ctx,
                                   entt::meta_any& var,
                                   const meta_any_proxy& var_proxy,
                                   const var_info& info,
                                   const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::quat&>();
    inspect_result result{};

    auto val = math::degrees(math::eulerAngles(data));

    static const auto reset = math::zero<math::vec3>();

    // auto val = quat_to_vec4(data);
    if(DragVec3(val, info, &reset, "%.2f°"))
    {
        // data = vec4_to_quat(val);
        data = math::quat(math::radians(val));
        result.changed = true;
    }
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_bbox::inspect(rtti::context& ctx,
                                   entt::meta_any& var,
                                   const meta_any_proxy& var_proxy,
                                   const var_info& info,
                                   const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::bbox&>();
    inspect_result result{};


    static const auto reset = math::zero<math::vec3>();

    ImGui::BeginGroup();
    ImGui::PushID("Min");
    if(DragVec3(data.min, info, &reset, "Min %.2f"))
    {  
        result.changed = true;
    }
    ImGui::PopID();
    ImGui::PushID("Max");
    if(DragVec3(data.max, info, &reset, "Max %.2f"))
    {  
        result.changed = true;
    }
    ImGui::PopID();
    ImGui::EndGroup();
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}


void inspector_transform::before_inspect(const entt::meta_data& prop, const entt::meta_any& object)
{
    layout_ = std::make_unique<property_layout>();
    layout_->set_data(prop, object, false);

    bool default_open = !entt::get_attribute_as<bool>(prop, "collapsed");
    open_ = layout_->push_tree_layout(ImGuiTreeNodeFlags_SpanFullWidth, default_open);
}

auto inspector_transform::inspect(rtti::context& ctx,
                                  entt::meta_any& var,
                                  const meta_any_proxy& var_proxy,
                                  const var_info& info,
                                  const entt::meta_custom& custom) -> inspect_result
{
    if(!open_)
    {
        return {};
    }
    inspect_result result{};

    auto& data = var.cast<math::transform&>();
    const auto position_old = data.get_translation();
    auto position = position_old;
    const auto scale_old = data.get_scale();
    auto scale = scale_old;
    const auto skew_old = data.get_skew();
    auto skew = skew_old;

    auto type = entt::resolve<math::transform>();

    // Euler hint + per-field undo: rotation actions must apply via set_rotation_euler_degrees
    // (see make_euler_rotation_proxy). Quat set_rotation drops the authored Euler hint.
    math::vec3 euler_angles = data.get_rotation_euler_degrees();

    ImGui::PushID("Position");
    {
        auto prop = type.data("position"_hs);
        auto prop_pretty_name = entt::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(entt::get_name(prop), prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name);
        // The reset button shares the label cell.
        layout.push_layout(false, ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);

        ImGui::SameLine();

        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::GetFrameHeight(),
            [&]()
            {
                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    data.reset_position();
                    result.changed = true;
                    result.edit_finished = true;
                    auto prop_proxy = make_property_proxy(var_proxy, prop);
                    add_property_action(ctx, override_ctx, result, prop_proxy, position_old, data.get_position(), prop.custom());
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });
        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            static const auto reset = math::zero<math::vec3>();
            if(DragVec3(position, info, &reset))
            {
                data.set_position(position);
                result.changed = true;
                auto prop_proxy = make_property_proxy(var_proxy, prop);
                add_property_action(ctx, override_ctx, result, prop_proxy, position_old, position, prop.custom());
            }
            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        // The row ends while the path still names the property: its end draws the override mark.
        layout.pop_layout();
        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Rotation");
    {
        auto prop = type.data("rotation"_hs);
        auto prop_pretty_name = entt::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(entt::get_name(prop), prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name);
        // The reset button shares the label cell.
        layout.push_layout(false, ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);

        ImGui::SameLine();

        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::GetFrameHeight(),
            [&]()
            {
                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    const math::vec3 euler_old = euler_angles;
                    data.reset_rotation();
                    euler_angles = math::zero<math::vec3>();
                    result.changed = true;
                    result.edit_finished = true;
                    ImGui::GetStateStorage()->SetBool(ImGui::GetID("euler_edit_session"), false);
                    auto euler_proxy = make_euler_rotation_proxy(var_proxy);
                    add_property_action(ctx, override_ctx, result, euler_proxy, euler_old, euler_angles, prop.custom());
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });
        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            // While any rotation axis is mid-edit (especially TempInput text entry), keep a
            // stable authored Euler triple. Re-reading from the quaternion each frame near
            // gimbal lock (pitch ~ +/-90) feeds equivalent-but-different XYZ back into the
            // widgets and re-applies them - the cube "constantly rotates" and X/Z jump.
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID euler_edit_id = ImGui::GetID("euler_edit_session");
            const ImGuiID euler_x_id = ImGui::GetID("euler_edit_x");
            const ImGuiID euler_y_id = ImGui::GetID("euler_edit_y");
            const ImGuiID euler_z_id = ImGui::GetID("euler_edit_z");
            const bool was_editing_euler = storage->GetBool(euler_edit_id, false);
            if(was_editing_euler)
            {
                euler_angles.x = storage->GetFloat(euler_x_id, euler_angles.x);
                euler_angles.y = storage->GetFloat(euler_y_id, euler_angles.y);
                euler_angles.z = storage->GetFloat(euler_z_id, euler_angles.z);
            }
            const math::vec3 euler_old = euler_angles;
            static const auto reset = math::zero<math::vec3>();
            bool rotation_changed = false;
            if(DragVec3(euler_angles, info, &reset, "%.2f°"))
            {
                data.set_rotation_euler_degrees(euler_angles);
                rotation_changed = true;
                result.changed = true;
                auto euler_proxy = make_euler_rotation_proxy(var_proxy);
                add_property_action(ctx, override_ctx, result, euler_proxy, euler_old, euler_angles, prop.custom());
            }
            const bool edit_finished_euler = ImGui::IsItemDeactivatedAfterEdit();
            result.edit_finished |= edit_finished_euler;
            // Commit the session Euler once more on deactivate so the live transform keeps
            // the authored triple after the ImGui TempInput ends (not only during edit).
            if(edit_finished_euler && was_editing_euler)
            {
                data.set_rotation_euler_degrees(euler_angles);
                result.changed = true;
                auto euler_proxy = make_euler_rotation_proxy(var_proxy);
                add_property_action(ctx, override_ctx, result, euler_proxy, euler_old, euler_angles, prop.custom());
            }
            // DragVecN wraps BeginGroup/EndGroup; IsItemActive covers any axis still engaged.
            const bool is_editing_euler = ImGui::IsItemActive();
            if(is_editing_euler || rotation_changed)
            {
                storage->SetBool(euler_edit_id, true);
                storage->SetFloat(euler_x_id, euler_angles.x);
                storage->SetFloat(euler_y_id, euler_angles.y);
                storage->SetFloat(euler_z_id, euler_angles.z);
            }
            else if(was_editing_euler)
            {
                storage->SetBool(euler_edit_id, false);
            }
        }

        ImGui::PopItemWidth();

        // The row ends while the path still names the property: its end draws the override mark.
        layout.pop_layout();
        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Scale");
    {
        auto prop = type.data("scale"_hs);
        auto prop_pretty_name = entt::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(entt::get_name(prop), prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name);
        static bool locked_scale = false;
        auto label = locked_scale ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT;
        const float buttons_width =
            ImGui::CalcItemSize(label).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        // The lock and reset buttons share the label cell.
        layout.push_layout(false, buttons_width + ImGui::GetStyle().ItemSpacing.x);

        ImGui::SameLine();
        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            buttons_width,
            [&]()
            {
                if(ImGui::Button(label))
                {
                    locked_scale = !locked_scale;
                }

                ImGui::SetItemTooltipEx("Enable/Disable Constrained Proportions");

                ImGui::SameLine();

                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    data.reset_scale();
                    result.changed = true;
                    result.edit_finished = true;
                    auto prop_proxy = make_property_proxy(var_proxy, prop);
                    add_property_action(ctx, override_ctx, result, prop_proxy, scale_old, data.get_scale(), prop.custom());
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });

        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            static const auto reset = math::one<math::vec3>();
            auto before_scale = scale;
            if(DragVec3(scale, info, &reset))
            {
                auto delta = scale - before_scale;

                if(locked_scale)
                {
                    before_scale += math::vec3(delta.x);
                    before_scale += math::vec3(delta.y);
                    before_scale += math::vec3(delta.z);
                    scale = before_scale;
                }

                data.set_scale(scale);
                result.changed = true;
                auto prop_proxy = make_property_proxy(var_proxy, prop);
                add_property_action(ctx, override_ctx, result, prop_proxy, scale_old, scale, prop.custom());
            }

            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        // The row ends while the path still names the property: its end draws the override mark.
        layout.pop_layout();
        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Skew");
    {
        auto prop = type.data("skew"_hs);
        auto prop_pretty_name = entt::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(entt::get_name(prop), prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name);
        // The reset button shares the label cell.
        layout.push_layout(false, ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);

        ImGui::SameLine();

        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::GetFrameHeight(),
            [&]()
            {
                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    data.reset_skew();
                    result.changed = true;
                    result.edit_finished = true;
                    auto prop_proxy = make_property_proxy(var_proxy, prop);
                    add_property_action(ctx, override_ctx, result, prop_proxy, skew_old, data.get_skew(), prop.custom());
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });
        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            static const auto reset = math::zero<math::vec3>();
            if(DragVec3(skew, info, &reset))
            {
                data.set_skew(skew);
                result.changed = true;
                auto prop_proxy = make_property_proxy(var_proxy, prop);
                add_property_action(ctx, override_ctx, result, prop_proxy, skew_old, skew, prop.custom());
            }

            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        // The row ends while the path still names the property: its end draws the override mark.
        layout.pop_layout();
        override_ctx.pop_segment();
    }
    ImGui::PopID();

    return result;
}
} // namespace unravel
