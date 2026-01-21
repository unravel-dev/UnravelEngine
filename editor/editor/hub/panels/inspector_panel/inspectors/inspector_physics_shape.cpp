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
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    if(auto current = property_layout::get_current())
    {
        current->pop_layout();
    }

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
        else if(type == entt::resolve<physics_mesh_shape>())
        {
            data.shape = physics_mesh_shape{};
        }
    }

    std::string prop_name = "shape";
    std::string prop_pretty_name = "Shape";
    // Create a proxy for the variant member that can get/set through the parent
    auto make_shape_proxy = [prop_name, prop_pretty_name, var_proxy](auto&& shape_ref) -> meta_any_proxy
    {
        using shape_type = std::decay_t<decltype(shape_ref)>;
        meta_any_proxy shape_proxy;
        shape_proxy.impl->type_name = entt::get_pretty_name(entt::resolve<shape_type>());
        shape_proxy.impl->get_name = [ prop_name, prop_pretty_name, parent_proxy = var_proxy]()
        {
            auto name = parent_proxy.impl->get_name();
            if(name.empty())
            {
                return prop_pretty_name;
            }
            return fmt::format("{}/{}", name, prop_pretty_name);
        };
        shape_proxy.impl->name = shape_proxy.impl->get_name();
        shape_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
        {
            entt::meta_any parent_var;
            if(parent_proxy.impl->getter(parent_var) && parent_var)
            {
                auto& compound_shape = parent_var.cast<physics_compound_shape&>();
                if(hpp::holds_alternative<shape_type>(compound_shape.shape))
                {
                    result = entt::meta_any{hpp::get<shape_type>(compound_shape.shape)};
                    return true;
                }
            }
            return false;
        };
        shape_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
        {
            entt::meta_any parent_var;
            if(parent_proxy.impl->getter(parent_var) && parent_var)
            {
                auto& compound_shape = parent_var.cast<physics_compound_shape&>();
                if(value.try_cast<shape_type>())
                {
                    compound_shape.shape = value.cast<shape_type>();
                    return parent_proxy.impl->setter(parent_proxy, parent_var, execution_count);
                }
            }
            return false;
        };
        return shape_proxy;
    };

    auto& override_ctx = ctx.get_cached<prefab_override_context>();
    override_ctx.push_segment("shape", "Shape");

    if(hpp::holds_alternative<physics_box_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_box_shape>(data.shape);
        auto shape_proxy = make_shape_proxy(shape);
        entt::meta_any shape_var;
        if(shape_proxy.impl->getter(shape_var))
        {
            result |= ::unravel::inspect_var(ctx, shape_var, shape_proxy, info, custom);
        }
    }
    else if(hpp::holds_alternative<physics_sphere_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_sphere_shape>(data.shape);
        auto shape_proxy = make_shape_proxy(shape);
        entt::meta_any shape_var;
        if(shape_proxy.impl->getter(shape_var))
        {
            result |= ::unravel::inspect_var(ctx, shape_var, shape_proxy, info, custom);
        }
    }
    else if(hpp::holds_alternative<physics_capsule_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_capsule_shape>(data.shape);
        auto shape_proxy = make_shape_proxy(shape);
        entt::meta_any shape_var;
        if(shape_proxy.impl->getter(shape_var))
        {
            result |= ::unravel::inspect_var(ctx, shape_var, shape_proxy, info, custom);
        }
    }
    else if(hpp::holds_alternative<physics_cylinder_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_cylinder_shape>(data.shape);
        auto shape_proxy = make_shape_proxy(shape);
        entt::meta_any shape_var;
        if(shape_proxy.impl->getter(shape_var))
        {
            result |= ::unravel::inspect_var(ctx, shape_var, shape_proxy, info, custom);
        }
    }
    else if(hpp::holds_alternative<physics_mesh_shape>(data.shape))
    {
        auto& shape = hpp::get<physics_mesh_shape>(data.shape);
        auto shape_proxy = make_shape_proxy(shape);
        entt::meta_any shape_var;
        if(shape_proxy.impl->getter(shape_var))
        {
            result |= ::unravel::inspect_var(ctx, shape_var, shape_proxy, info, custom);
        }
    }
    else
    {
        ImGui::LabelText("Unknown", "%s", "test");
    }

    return result;
}

} // namespace unravel
