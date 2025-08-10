#include "inspector_prefab_component.h"
#include "imgui_widgets/tooltips.h"
#include "inspectors.h"
#include <engine/meta/ecs/entity.hpp>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <editor/editing/editing_manager.h>
#include <uuid/uuid.h>
#include <functional>
#include <editor/hub/panels/entity_panel.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>

namespace unravel
{

namespace
{


} // anonymous namespace

auto inspector_prefab_component::inspect(rtti::context& ctx,
                                        rttr::variant& var,
                                        const var_info& info,
                                        const meta_getter& get_metadata) -> inspect_result
{
    auto& data = *var.get_value<prefab_component*>();
    inspect_result result{};

    auto root_prefab_entity = data.get_owner();
    // Show override information
    const auto& overrides = data.get_all_overrides();
    
    if(!overrides.empty())
    {       
        std::string header_id = fmt::format("Property Overrides: {}###Override Details", overrides.size());
        if(ImGui::CollapsingHeader(header_id.c_str()))
        {
            ImGui::Indent();
            
            // Display overrides - show pretty paths to users
            std::string serialization_path_to_remove;
            
            // Display overrides from the new structure
            const auto& property_overrides = data.get_all_overrides();
            for(const auto& override_data : property_overrides)
            {
                // Build full display path with entity name
                std::string display_path;
                
                // Find the entity by UUID and get its name
                auto found_entity = scene::find_entity_by_prefab_uuid(root_prefab_entity, override_data.entity_uuid);
                if (found_entity)
                {
                    std::string entity_name = entity_panel::get_entity_name(found_entity);
                    display_path = entity_name + "/" + override_data.pretty_component_path;
                }
                else
                {
                    // Fallback if entity not found
                    display_path = override_data.pretty_component_path;
                }
                
                ImGui::BeginGroup();
                ImGui::BulletText("%s", display_path.c_str());
                ImGui::SameLine();
                
                // Add a revert button for each override
                std::string override_id = hpp::to_string(override_data.entity_uuid) + ":" + override_data.component_path;
                ImGui::PushID(override_id.c_str());
                if(ImGui::SmallButton("Revert"))
                {
                    // Store the UUID and component path for removal
                    serialization_path_to_remove = override_id;
                }
                ImGui::PopID();
                ImGui::EndGroup();


                if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && found_entity)
                {
                    auto& em = ctx.get_cached<editing_manager>();

                    em.focus(found_entity);
                }
                
                // Show technical details as tooltip
                std::string entity_name = found_entity ? entity_panel::get_entity_name(found_entity) : "Entity Not Found";
                ImGui::SetItemTooltipEx("Entity: %s\nUUID: %s\nComponent Path: %s\nPretty Path: %s", 
                                    entity_name.c_str(),
                                    hpp::to_string(override_data.entity_uuid).c_str(),
                                    override_data.component_path.c_str(),
                                    override_data.pretty_component_path.c_str());
                
            }

            // Process removal outside the loop to avoid iterator invalidation
            if(!serialization_path_to_remove.empty())
            {
                // Parse the stored override ID to extract UUID and component path
                auto colon_pos = serialization_path_to_remove.find(':');
                if (colon_pos != std::string::npos)
                {
                    std::string uuid_str = serialization_path_to_remove.substr(0, colon_pos);
                    std::string component_path = serialization_path_to_remove.substr(colon_pos + 1);
                    
                    auto uuid_opt = hpp::uuid::from_string(uuid_str);
                    if (uuid_opt.has_value())
                    {
                        data.remove_override(uuid_opt.value(), component_path);
                        data.changed = true;
                        result.changed = true;

                        auto& em = ctx.get_cached<editing_manager>();
                        em.sync_prefab_entity(ctx, root_prefab_entity, data.source);
                    }
                }
            }
            
            ImGui::Unindent();
        }
        ImGui::Separator();
    }

    if(!data.removed_entities.empty())
    {
        std::string header_id = fmt::format("Removed Entities: {}###Removed Entities", data.removed_entities.size());
        if(ImGui::CollapsingHeader(header_id.c_str()))
        {
            ImGui::Indent();

            hpp::uuid uiid_to_remove;
            for(auto& entity_uuid : data.removed_entities)
            {
                auto uuid_str = hpp::to_string(entity_uuid);
                ImGui::BulletText("%s", uuid_str.c_str());
                ImGui::SameLine();

                // Add a revert button for each override
                ImGui::PushID(uuid_str.c_str());
                if(ImGui::SmallButton("Revert"))
                {
                    // Store the UUID and component path for removal
                    uiid_to_remove = entity_uuid;
                }
                ImGui::PopID();
            }

            if(!uiid_to_remove.is_nil())
            {
                data.removed_entities.erase(uiid_to_remove);
                data.changed = true;
                result.changed = true;

                auto& em = ctx.get_cached<editing_manager>();
                em.sync_prefab_entity(ctx, root_prefab_entity, data.source);
                    
            }

            ImGui::Unindent();
        }
    }
  

    // Control buttons
    if(ImGui::Button("Apply All to Prefab", ImVec2(-1, ImGui::GetFrameHeight())))
    {
        data.changed = false;
        auto prefab_path = fs::resolve_protocol(data.source.id());
        asset_writer::atomic_save_to_file(prefab_path.string(), root_prefab_entity);
        data.clear_overrides(); // Clear overrides after applying to prefab
        result.changed = true;
    }
    
    if(ImGui::Button("Revert All Overrides", ImVec2(-1, ImGui::GetFrameHeight())))
    {
        data.clear_overrides();
        data.changed = true;
        result.changed = true;
        auto& em = ctx.get_cached<editing_manager>();
        em.sync_prefab_entity(ctx, root_prefab_entity, data.source);
    }
    
    
    ImGui::NewLine();

    result |= inspect_var_properties(ctx, var, info, get_metadata);

    if(result.changed)
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.sync_prefab_entity(ctx, root_prefab_entity, data.source);
    }
    
    return result;
}

} // namespace unravel
