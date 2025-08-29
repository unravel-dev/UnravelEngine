#include "inspector_light.h"
#include "inspectors.h"

namespace unravel
{
auto inspector_light_component::inspect(rtti::context& ctx,
                                        entt::meta_any& var,
                                        const meta_any_proxy& var_proxy,
                                        const var_info& info,
                                        const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<light_component&>();
    const auto& light_val = data.get_light();
    

    if(light_val.casts_shadows)
    {
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
                for(uint8_t ii = 1; ii < light_val.directional_shadow_params.num_splits; ++ii)
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

    }


    return result;
}
} // namespace unravel
