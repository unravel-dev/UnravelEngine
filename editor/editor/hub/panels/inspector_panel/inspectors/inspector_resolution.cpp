#include "inspector_resolution.h"
#include "imgui/imgui.h"
#include "inspectors.h"
#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"

namespace unravel
{

auto inspector_resolution_settings::inspect(rtti::context& ctx,
                                             rttr::variant& var,
                                             const var_info& info,
                                             const meta_getter& get_metadata) -> inspect_result
{
    auto& data = *var.get_value<settings::resolution_settings*>();
    auto& resolutions = data.resolutions;
    
    inspect_result result{};
    
    // Display current resolutions
    for(size_t i = 0; i < resolutions.size(); ++i)
    {
        auto& resolution = resolutions[i];
        
        ImGui::PushID(static_cast<int>(i));
        
        ImGui::Separator();
        
        // Resolution header with delete button
        ImGui::AlignTextToFramePadding();
        if(ImGui::TreeNode(("Resolution " + std::to_string(i)).c_str()))
        {
            // Name field
            {
                property_layout layout("Name");
                char name_buffer[256];
                std::strncpy(name_buffer, resolution.name.c_str(), sizeof(name_buffer) - 1);
                name_buffer[sizeof(name_buffer) - 1] = '\0';
                
                if(ImGui::InputText("##name", name_buffer, sizeof(name_buffer)))
                {
                    resolution.name = name_buffer;
                    result.changed = true;
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DrawItemActivityOutline();
            }
            
            // Width field
            {
                property_layout layout("Width");
                int width = resolution.width;
                if(ImGui::InputInt("##width", &width, 1, 100))
                {
                    if(width >= 0)
                    {
                        resolution.width = width;
                        result.changed = true;
                    }
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DrawItemActivityOutline();
            }
            
            // Height field
            {
                property_layout layout("Height");
                int height = resolution.height;
                if(ImGui::InputInt("##height", &height, 1, 100))
                {
                    if(height >= 0)
                    {
                        resolution.height = height;
                        result.changed = true;
                    }
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DrawItemActivityOutline();
            }
            
            // Aspect ratio field
            {
                property_layout layout("Aspect Ratio");
                float aspect = resolution.aspect;
                if(ImGui::InputFloat("##aspect", &aspect, 0.01f, 0.1f, "%.3f"))
                {
                    if(aspect >= 0.0f)
                    {
                        resolution.aspect = aspect;
                        result.changed = true;
                    }
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DrawItemActivityOutline();
                
                ImGui::SameLine();
                if(ImGui::Button("Auto Calculate"))
                {
                    if(resolution.width > 0 && resolution.height > 0)
                    {
                        resolution.aspect = static_cast<float>(resolution.width) / static_cast<float>(resolution.height);
                        result.changed = true;
                        result.edit_finished = true;
                    }
                }
                ImGui::SetItemTooltipEx("Calculate aspect ratio from width and height");
            }
            
            // Delete button (don't allow deleting the first resolution "Free Aspect")
            if(i > 0 && !info.read_only)
            {
                ImGui::Separator();
                if(ImGui::Button(ICON_MDI_DELETE " Delete Resolution"))
                {
                    resolutions.erase(resolutions.begin() + i);
                    result.changed = true;
                    result.edit_finished = true;
                    ImGui::TreePop();
                    ImGui::PopID();
                    break; // Exit the loop since we modified the vector
                }
                ImGui::SetItemTooltipEx("Delete this resolution");
            }
            
            ImGui::TreePop();
        }
        
        ImGui::PopID();
    }
    
    // Add new resolution button
    if(!info.read_only)
    {
        ImGui::Separator();
        if(ImGui::Button(ICON_MDI_PLUS " Add New Resolution"))
        {
            settings::resolution_settings::resolution new_resolution;
            new_resolution.name = "New Resolution";
            new_resolution.width = 1920;
            new_resolution.height = 1080;
            new_resolution.aspect = 16.0f / 9.0f;
            
            resolutions.push_back(new_resolution);
            result.changed = true;
            result.edit_finished = true;
        }
        ImGui::SetItemTooltipEx("Add a new resolution preset");
    }
    
    return result;
}

} // namespace unravel 