#include "inspector_physics_shape.h"
#include "imgui/imgui.h"
#include "inspectors.h"

namespace unravel
{

auto inspector_physics_compound_shape::inspect(rtti::context& ctx,
                                               entt::meta_any& var,
                                               const meta_any_proxy& var_proxy,
                                               const var_info& info,
                                               const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<physics_compound_shape&>();

    inspect_result result{};

    auto variant_types = entt::get_attribute_as<std::vector<entt::meta_type>>(var.type().custom(), "variant_types");

    size_t item_current_idx = data.shape.index();

    const auto& combo_preview_value = variant_types[item_current_idx];

    auto name = entt::get_pretty_name(combo_preview_value);

    if(ImGui::BeginCombo("##Type", name.c_str()))
    {
        for(int n = 0; n < variant_types.size(); n++)
        {
            const bool is_selected = (item_current_idx == n);

            auto name = entt::get_pretty_name(variant_types[n]);

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
        if(type == entt::resolve<physics_box_shape>())
        {
            data.shape = physics_box_shape{};
        }
        else if(type == entt::resolve<physics_sphere_shape>())
        {
            data.shape = physics_sphere_shape{};
        }
        else if(type == entt::resolve<physics_capsule_shape>())
        {
            data.shape = physics_capsule_shape{};
        }
        else if(type == entt::resolve<physics_cylinder_shape>())
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
