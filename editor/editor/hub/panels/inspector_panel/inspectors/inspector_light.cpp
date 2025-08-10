#include "inspector_light.h"
#include "inspectors.h"

namespace unravel
{

auto inspector_light_component::inspect(rtti::context& ctx,
                                        rttr::variant& var,
                                        const var_info& info,
                                        const meta_getter& get_metadata) -> inspect_result
{
    auto& data = *var.get_value<light_component*>();
    auto light_val = data.get_light();
    auto result = ::unravel::inspect(ctx, light_val);

    if(light_val.type == light_type::spot)
    {
        result |= ::unravel::inspect(ctx, light_val.spot_data);
    }
    else if(light_val.type == light_type::point)
    {
        result |= ::unravel::inspect(ctx, light_val.point_data);
    }
    else if(light_val.type == light_type::directional)
    {
        result |= ::unravel::inspect(ctx, light_val.directional_data);
    }

    if(data.get_light().casts_shadows)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
        if(ImGui::TreeNode("Shadow"))
        {
            ImGui::TreePush("Shadow");
            result |= ::unravel::inspect(ctx, light_val.shadow_params);

            ImGui::AlignTextToFramePadding();
            ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
            if(ImGui::TreeNode("Params"))
            {
                ImGui::TreePush("Specific");

                if(light_val.type == light_type::spot)
                {
                    result |= ::unravel::inspect(ctx, light_val.spot_data.shadow_params);
                }
                else if(light_val.type == light_type::point)
                {
                    result |= ::unravel::inspect(ctx, light_val.point_data.shadow_params);
                }
                else if(light_val.type == light_type::directional)
                {
                    result |= ::unravel::inspect(ctx, light_val.directional_data.shadow_params);
                }

                ImGui::TreePop();
                ImGui::TreePop();
            }

            ImGui::AlignTextToFramePadding();
            if(ImGui::TreeNode("Maps"))
            {
                ImGui::TreePush("Maps");

                auto& generator = data.get_shadowmap_generator();

                auto depth_type = generator.get_depth_type();

                ImGui::BeginGroup();
                auto program = generator.get_depth_render_program(depth_type);
                program->begin();
                ImGui::Image(
                    ImGui::ToTex(generator.get_rt_texture(0), 0, program->native_handle()).id,
                    ImVec2(256, 250));

                if(light_val.type == light_type::directional)
                {
                    for(uint8_t ii = 1; ii < light_val.directional_data.shadow_params.num_splits; ++ii)
                    {
                        ImGui::Image(ImGui::ToTex(generator.get_rt_texture(ii),
                                                  0,
                                                  program->native_handle())
                                         .id,
                                     ImVec2(256, 256));
                    }
                }
                program->end();
                ImGui::EndGroup();

                ImGui::TreePop();
                ImGui::TreePop();
            }

            ImGui::TreePop();
            ImGui::TreePop();
        }
    }

    if(result.changed)
    {
        data.set_light(light_val);
    }

    return result;
}

} // namespace unravel
