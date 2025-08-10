#include "inspector_layer.h"
#include "imgui/imgui.h"
#include "inspectors.h"
#include <engine/settings/settings.h>
#include <editor/hub/hub.h>

namespace unravel
{

auto inspector_layer::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    
    auto& data = var.get_value<layer_mask>();
    std::bitset<32> bits = data.mask;

    inspect_result result{};


    const auto& layer_names = ctx.get<settings>().layer.layers;


    std::string preview;

    if(bits.all())
    {
        preview = "Everything";
    }
    else if(bits.none())
    {
        preview = "Nothing";
    }
    else
    {

        for(size_t i = 0; i < bits.size(); ++i)
        {
            if(bits.test(i) && !layer_names[i].empty())
            {
                if(!preview.empty())
                {
                    preview += ",";
                }
                preview += layer_names[i];
            }
        }
    }

    if(ImGui::CalcTextSize(preview.c_str()).x > ImGui::GetContentRegionAvail().x)
    {
        preview = "Mixed...";
    }


    if(ImGui::BeginCombo("##Type", preview.c_str()))
    {
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

        if(ImGui::MenuItem("Edit Layers...", "", false))
        {
            ctx.get_cached<hub>().open_project_settings(ctx, "Layers");
        }

        if(ImGui::MenuItem("Nothing", "", bits.none()))
        {
            bits.reset();

            result.changed = true;
        }

        if(ImGui::MenuItem("Everything", "", bits.all()))
        {
            bits.set();

            result.changed = true;
        }

        for(size_t i = 0; i < bits.size(); ++i)
        {
            if(layer_names[i].empty())
            {
                continue;
            }
            if(ImGui::MenuItem(layer_names[i].c_str(), "", bits.test(i)))
            {
                bits.flip(i);
                result.changed = true;
            }

            ImGui::DrawItemActivityOutline();
        }


        ImGui::PopItemFlag();

        ImGui::EndCombo();
    }

    if(result.changed)
    {
        result.edit_finished |= true;//ImGui::IsItemDeactivatedAfterEdit();

        data.mask = int(bits.to_ulong());
        var = data;
    }

    return result;
}

} // namespace unravel
