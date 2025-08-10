#include "inspector_math.h"
#include "imgui/imgui.h"
#include <imgui/imgui_internal.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>

namespace unravel
{
namespace
{
float DRAG_SPEED = 0.01f;

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
    bool result = ImGui::DragVecN("##",
                                  ImGuiDataType_Float,
                                  math::value_ptr(data),
                                  data.length(),
                                  DRAG_SPEED,
                                  nullptr,
                                  nullptr,
                                  reset ? math::value_ptr(*reset) : nullptr,
                                  format);
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragVec3(math::vec3& data, const var_info& info, const math::vec3* reset = nullptr, const char* format = "%.3f")
{
    bool result = ImGui::DragVecN("##",
                                  ImGuiDataType_Float,
                                  math::value_ptr(data),
                                  data.length(),
                                  DRAG_SPEED,
                                  nullptr,
                                  nullptr,
                                  reset ? math::value_ptr(*reset) : nullptr,
                                  format);

    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragVec4(math::vec4& data, const var_info& info, const math::vec4* reset = nullptr, const char* format = "%.3f")
{
    bool result = ImGui::DragVecN("##",
                                  ImGuiDataType_Float,
                                  math::value_ptr(data),
                                  data.length(),
                                  DRAG_SPEED,
                                  nullptr,
                                  nullptr,
                                  reset ? math::value_ptr(*reset) : nullptr,
                                  format);

    ImGui::ActiveItemWrapMousePos();

    return result;
}

} // namespace

auto inspector_bvec2::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<math::bvec2>();
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
        var = data;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_bvec3::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<math::bvec3>();
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
        var = data;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_bvec4::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<math::bvec4>();
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

        var = data;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec2::inspect(rtti::context& ctx,
                             rttr::variant& var,
                             const var_info& info,
                             const meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<math::vec2>();
    inspect_result result{};

    if(DragVec2(data, info))
    {
        var = data;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec3::inspect(rtti::context& ctx,
                             rttr::variant& var,
                             const var_info& info,
                             const meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<math::vec3>();
    inspect_result result{};

    if(DragVec3(data, info))
    {
        var = data;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec4::inspect(rtti::context& ctx,
                             rttr::variant& var,
                             const var_info& info,
                             const meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<math::vec4>();
    inspect_result result{};

    if(DragVec4(data, info))
    {
        var = data;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_color::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<math::color>();
    inspect_result result{};

    if(ImGui::ColorEdit4("##",
                         math::value_ptr(data.value),
                         ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
    {
        result.changed = true;
    }

    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_quaternion::inspect(rtti::context& ctx,
                                   rttr::variant& var,
                                   const var_info& info,
                                   const meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<math::quat>();
    inspect_result result{};

    auto val = quat_to_vec4(data);
    if(DragVec4(val, info))
    {
        data = vec4_to_quat(val);
        result.changed = true;
    }
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}

void inspector_transform::before_inspect(const rttr::property& prop)
{
    layout_ = std::make_unique<property_layout>();
    layout_->set_data(prop, false);
    open_ = layout_->push_tree_layout(ImGuiTreeNodeFlags_SpanFullWidth);
}

auto inspector_transform::inspect(rtti::context& ctx,
                                  rttr::variant& var,
                                  const var_info& info,
                                  const meta_getter& get_metadata) -> inspect_result
{
    if(!open_)
    {
        return {};
    }
    inspect_result result{};

    auto data = var.get_value<math::transform>();
    auto position = data.get_translation();
    auto rotation = data.get_rotation();
    auto scale = data.get_scale();
    auto skew = data.get_skew();
    //    auto perspective = data.get_perspective();

    auto type = rttr::type::get<math::transform>();
 
    static math::vec3 euler_angles(0.0f, 0.0f, 0.0f);

    math::quat old_quat(math::radians(euler_angles));

    float dot_product = math::dot(old_quat, rotation);
    bool equal = (dot_product > (1.0f - math::epsilon<float>()));
    if(!equal && (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGuizmo::IsUsing()))
    {
        euler_angles = data.get_rotation_euler_degrees();
    }



    ImGui::PushID("Position");
    {
        auto prop = type.get_property("position");
        auto prop_name = prop.get_name().to_string();
        auto prop_pretty_name = rttr::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(prop_name, prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name, "");
        layout.push_layout(false);

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
                    override_ctx.record_override();
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
                result.changed |= true;

                override_ctx.record_override();
            }
            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Rotation");
    {
        auto prop = type.get_property("rotation");
        auto prop_name = prop.get_name().to_string();
        auto prop_pretty_name = rttr::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(prop_name, prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name, "");
        layout.push_layout(false);

        ImGui::SameLine();

        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::GetFrameHeight(),
            [&]()
            {
                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    data.reset_rotation();
                    result.changed = true;
                    result.edit_finished = true;
                    override_ctx.record_override();
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });
        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            auto old_euler = euler_angles;
            static const auto reset = math::zero<math::vec3>();
            if(DragVec3(euler_angles, info, &reset, "%.2f°"))
            {
                data.rotate_local(math::radians(euler_angles - old_euler));
                result.changed |= true;

                override_ctx.record_override();
            }
            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        ImGui::PopItemWidth();

        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Scale");
    {
        auto prop = type.get_property("scale");
        auto prop_name = prop.get_name().to_string();
        auto prop_pretty_name = rttr::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(prop_name, prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name, "");
        layout.push_layout(false);

        ImGui::SameLine();
        static bool locked_scale = false;
        auto label = locked_scale ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT;
        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::CalcItemSize(label).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x,
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
                    override_ctx.record_override();
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
                result.changed |= true;

                override_ctx.record_override();
            }

            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Skew");
    {
        auto prop = type.get_property("skew");
        auto prop_name = prop.get_name().to_string();
        auto prop_pretty_name = rttr::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(prop_name, prop_pretty_name);
        
        property_layout layout;
        layout.set_data(prop_pretty_name, "");
        layout.push_layout(false);

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
                    override_ctx.record_override();
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
                result.changed |= true;
                override_ctx.record_override();
            }

            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        override_ctx.pop_segment();
    }
    ImGui::PopID();

    if(result.changed)
    {
        var = data;
    }

    return result;
}
} // namespace unravel
