#include "inspector_physics_shape.h"
#include "imgui/imgui.h"
#include "inspectors.h"

namespace unravel
{

inspect_result inspector_physics_compound_shape::inspect(rtti::context& ctx,
                                               rttr::variant& var,
                                               const var_info& info,
                                               const meta_getter& get_metadata)
{
    auto& data = var.get_value<physics_compound_shape>();

    inspect_result result{};

    std::vector<const rttr::type*> variant_types;
    auto variant_types_var = var.get_type().get_metadata("variant_types");
    if(variant_types_var)
    {
        variant_types = variant_types_var.get_value<std::vector<const rttr::type*>>();
    }
    size_t item_current_idx = data.shape.index();

    const auto& combo_preview_value = variant_types[item_current_idx];

    auto name = rttr::get_pretty_name(*combo_preview_value);

    if(ImGui::BeginCombo("##Type", name.c_str()))
    {
        for(int n = 0; n < variant_types.size(); n++)
        {
            const bool is_selected = (item_current_idx == n);

            auto name = rttr::get_pretty_name(*variant_types[n]);

            if(ImGui::Selectable(name.c_str(), is_selected))
            {
                item_current_idx = n;
                result.changed = true;
            }

            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();


            ImGui::DrawItemActivityOutline();


            if(is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    property_layout::get_current()->pop_layout();

    if(result.changed)
    {
        const auto type = variant_types[item_current_idx];
        if(*type == rttr::type::get<physics_box_shape>())
        {
            data.shape = physics_box_shape{};
        }
        else if(*type == rttr::type::get<physics_sphere_shape>())
        {
            data.shape = physics_sphere_shape{};
        }
        else if(*type == rttr::type::get<physics_capsule_shape>())
        {
            data.shape = physics_capsule_shape{};
        }
        else if(*type == rttr::type::get<physics_cylinder_shape>())
        {
            data.shape = physics_cylinder_shape{};
        }
    }

    if(hpp::holds_alternative<physics_box_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_box_shape>(data.shape);
        result |= ::unravel::inspect(ctx, shape);
    }
    else if(hpp::holds_alternative<physics_sphere_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_sphere_shape>(data.shape);
        result |= ::unravel::inspect(ctx, shape);
    }
    else if(hpp::holds_alternative<physics_capsule_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_capsule_shape>(data.shape);
        result |= ::unravel::inspect(ctx, shape);
    }
    else if(hpp::holds_alternative<physics_cylinder_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_cylinder_shape>(data.shape);
        result |= ::unravel::inspect(ctx, shape);
    }
    else
    {
        ImGui::LabelText("Unknown", "%s", "test");
    }

    return result;
}

} // namespace unravel
