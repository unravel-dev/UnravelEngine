#include "inspector_entity.h"
#include "inspectors.h"

#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/entity_panel.h>
#include <editor/imgui/imgui_interface.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/rendering/font.h>
#include <engine/scripting/ecs/systems/script_system.h>

#include <hpp/type_name.hpp>
#include <hpp/utility.hpp>
#include <string_utils/utils.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
namespace unravel
{

namespace
{
template<typename T>
auto get_component_icon() -> std::string
{
    // Core components
    if constexpr(std::is_same<T, id_component>::value)
    {
        return ICON_MDI_IDENTIFIER;
    }
    else if constexpr(std::is_same<T, tag_component>::value)
    {
        return ICON_MDI_TAG;
    }
    else if constexpr(std::is_same<T, layer_component>::value)
    {
        return ICON_MDI_LAYERS;
    }
    else if constexpr(std::is_same<T, prefab_component>::value)
    {
        return ICON_MDI_CUBE;
    }
    else if constexpr(std::is_same<T, prefab_id_component>::value)
    {
        return ICON_MDI_CUBE_OUTLINE;
    }
    // Transform
    else if constexpr(std::is_same<T, transform_component>::value)
    {
        return ICON_MDI_AXIS_ARROW;
    }
    // Test/Debug
    else if constexpr(std::is_same<T, test_component>::value)
    {
        return ICON_MDI_BUG;
    }
    // Rendering components
    else if constexpr(std::is_same<T, model_component>::value)
    {
        return ICON_MDI_SHAPE;
    }
    else if constexpr(std::is_same<T, submesh_component>::value)
    {
        return ICON_MDI_SHAPE_OUTLINE;
    }
    else if constexpr(std::is_same<T, camera_component>::value)
    {
        return ICON_MDI_CAMERA;
    }
    else if constexpr(std::is_same<T, text_component>::value)
    {
        return ICON_MDI_TEXT;
    }
    // Animation
    else if constexpr(std::is_same<T, animation_component>::value)
    {
        return ICON_MDI_ANIMATION;
    }
    else if constexpr(std::is_same<T, bone_component>::value)
    {
        return ICON_MDI_BONE;
    }
    // Lighting
    else if constexpr(std::is_same<T, light_component>::value)
    {
        return ICON_MDI_LIGHTBULB;
    }
    else if constexpr(std::is_same<T, skylight_component>::value)
    {
        return ICON_MDI_WEATHER_SUNNY;
    }
    else if constexpr(std::is_same<T, reflection_probe_component>::value)
    {
        return ICON_MDI_REFLECT_HORIZONTAL;
    }
    // Physics
    else if constexpr(std::is_same<T, physics_component>::value)
    {
        return ICON_MDI_ATOM;
    }
    // Audio
    else if constexpr(std::is_same<T, audio_source_component>::value)
    {
        return ICON_MDI_VOLUME_HIGH;
    }
    else if constexpr(std::is_same<T, audio_listener_component>::value)
    {
        return ICON_MDI_EAR_HEARING;
    }
    // Scripting
    else if constexpr(std::is_same<T, script_component>::value)
    {
        return ICON_MDI_SCRIPT;
    }
    // Post-processing effects (using similar icons for consistency)
    else if constexpr(std::is_same<T, tonemapping_component>::value)
    {
        return ICON_MDI_BRIGHTNESS_5;
    }
    else if constexpr(std::is_same<T, fxaa_component>::value)
    {
        return ICON_MDI_FILTER;
    }
    else if constexpr(std::is_same<T, assao_component>::value)
    {
        return ICON_MDI_FILTER_OUTLINE;
    }
    else if constexpr(std::is_same<T, ssr_component>::value)
    {
        return ICON_MDI_MIRROR;
    }
    else
    {
        // Default fallback icon
        return ICON_MDI_CUBE_OUTLINE;
    }
}

struct inspect_callbacks
{
    std::function<inspect_result()> on_inspect;
    std::function<void()> on_add;
    std::function<void()> on_remove;
    std::function<bool()> can_remove;
    std::function<bool()> can_merge;

    std::string icon;
};

auto inspect_component(const std::string& name, const inspect_callbacks& callbacks) -> inspect_result
{
    inspect_result result{};

    bool opened = true;

    ImGui::PushID(name.c_str());

    auto popup_str = "COMPONENT_SETTING";

    bool open_popup = false;
    bool open = true;
    if(!callbacks.can_merge())
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);

        auto pos = ImGui::GetCursorPos();
        auto col_header = ImGui::GetColorU32(ImGuiCol_Header);
        auto col_header_hovered = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
        auto col_header_active = ImGui::GetColorU32(ImGuiCol_HeaderActive);

        auto col_framebg = ImGui::GetColorU32(ImGuiCol_FrameBg);
        auto col_framebg_hovered = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
        auto col_framebg_active = ImGui::GetColorU32(ImGuiCol_FrameBgActive);

        ImGui::PushStyleColor(ImGuiCol_Header, col_framebg);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col_framebg_hovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, col_framebg_active);

        open = ImGui::CollapsingHeader(fmt::format("     {}", name).c_str(), nullptr, ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::OpenPopupOnItemClick(popup_str);
        ImGui::PopStyleColor(3);

        ImGui::SetCursorPos(pos);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("       %s", callbacks.icon.c_str());

        ImGui::SameLine();
        auto settings_size = ImGui::CalcTextSize(ICON_MDI_COG).x + ImGui::GetStyle().FramePadding.x * 2.0f;

        auto avail = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().FramePadding.x;
        ImGui::AlignedItem(1.0f,
                           avail,
                           settings_size,
                           [&]()
                           {
                               if(ImGui::Button(ICON_MDI_COG))
                               {
                                   open_popup = true;
                               }
                           });
    }

    if(open)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
        ImGui::TreePush(name.c_str());

        result |= callbacks.on_inspect();

        ImGui::TreePop();
        ImGui::PopStyleVar();
    }
    if(open_popup)
    {
        ImGui::OpenPopup(popup_str);
    }

    bool is_popup_open = ImGui::IsPopupOpen(popup_str);
    if(is_popup_open && ImGui::BeginPopupContextWindowEx(popup_str))
    {
        bool removal_allowed = callbacks.can_remove();
        if(ImGui::MenuItem("Reset", nullptr, false, removal_allowed))
        {
            callbacks.on_remove();
            callbacks.on_add();

            result.changed = true;
            result.edit_finished = true;
        }

        ImGui::Separator();
        if(ImGui::MenuItem("Remove Component", nullptr, false, removal_allowed))
        {
            callbacks.on_remove();
            result.changed = true;
            result.edit_finished = true;
        }

        ImGui::EndPopup();
    }

    ImGui::PopID();
    if(!opened)
    {
        callbacks.on_remove();
        result.changed = true;
        result.edit_finished = true;
    }

    return result;
}

auto list_component(ImGuiTextFilter& filter, const std::string& name, const inspect_callbacks& callbacks)
    -> inspect_result
{
    inspect_result result{};
    if(!filter.PassFilter(name.c_str()))
    {
        return result;
    }

    if(ImGui::Selectable(fmt::format("{} {}", callbacks.icon, name).c_str()))
    {
        callbacks.on_remove();
        callbacks.on_add();

        result.changed = true;
        result.edit_finished = true;

        ImGui::CloseCurrentPopup();
    }
    return result;
}
auto get_entity_pretty_name(entt::handle entity) -> const std::string&
{
    if(!entity)
    {
        static const std::string empty = "None (Entity)";
        return empty;
    }
    auto& tag = entity.get_or_emplace<tag_component>();
    return tag.name;
}

auto process_drag_drop_target(rtti::context& ctx, entt::handle& obj) -> bool
{
    if(ImGui::IsDragDropPossibleTargetForType("entity"))
    {
        ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f)));
    }

    bool result = false;

    if(ImGui::BeginDragDropTarget())
    {
        if(ImGui::IsDragDropPayloadBeingAccepted())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        else
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        }

        {
            auto payload = ImGui::AcceptDragDropPayload("entity");
            if(payload != nullptr)
            {
                entt::handle dropped{};
                std::memcpy(&dropped, payload->Data, size_t(payload->DataSize));
                if(dropped)
                {
                    obj = dropped;
                    result = true;
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    return result;
}

auto render_entity_header(rtti::context& ctx, entt::handle data, const prefab_override_context& override_ctx) -> inspect_result
{
    inspect_result result{};
    
    if(!data)
    {
        return result;
    }

    auto tag_comp = data.try_get<tag_component>();
    auto trans_comp = data.try_get<transform_component>();
    
    if(!tag_comp)
    {
        return result;
    }

    // Create a table for proper alignment
    if(ImGui::BeginTable("EntityHeader", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoClip))
    {
        ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 22.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        
        ImGui::TableNextRow();
        
        // Active checkbox column
        ImGui::TableSetColumnIndex(0);
        if(trans_comp)
        {
            bool is_active = trans_comp->is_active();
            
            // Track component type for prefab override context
            auto type = rttr::type::get<transform_component>();
            auto name = type.get_name().to_string();
            auto pretty_name = rttr::get_pretty_name(type);
            
            // Use a copy of override context for proper path handling
            auto& override_ctx_ref = const_cast<prefab_override_context&>(override_ctx);
            override_ctx_ref.set_component_type(name, pretty_name);
            override_ctx_ref.push_segment("active", "Active");

            if(ImGui::Checkbox("##active", &is_active))
            {
                trans_comp->set_active(is_active);
                result.changed = true;
                result.edit_finished = true;
            }
            
            // ImGui::PopStyleColor(3);
            override_ctx_ref.pop_segment();
        }
        
        auto col = entity_panel::get_entity_display_color(data);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        // Icon column
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        {
            auto icon = entity_panel::get_entity_icon(data);

            ImGui::Text("%s", icon.c_str());
        }
        // Name field column
        ImGui::TableSetColumnIndex(2);
        {
            // Track component type for prefab override context
            auto type = rttr::type::get<tag_component>();
            auto type_name = type.get_name().to_string();
            auto pretty_name = rttr::get_pretty_name(type);
            
            auto& override_ctx_ref = const_cast<prefab_override_context&>(override_ctx);
            override_ctx_ref.set_component_type(type_name, pretty_name);
            override_ctx_ref.push_segment("name", "Name");
                        
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::SetNextItemWidth(-1.0f);
            
            if(ImGui::InputTextWidget("##name", tag_comp->name, false))
            {
                result.changed = true;
                result.edit_finished = true;
            }
            
            ImGui::PopStyleVar();
            override_ctx_ref.pop_segment();
        }
        ImGui::PopStyleColor();

        ImGui::EndTable();
    }
    
    // Tag field using traditional property_layout approach
    {
        // Track component type for prefab override context
        auto type = rttr::type::get<tag_component>();
        auto type_name = type.get_name().to_string();
        auto pretty_name = rttr::get_pretty_name(type);

        rttr::property prop = type.get_property("tag");
        auto prop_name = prop.get_name().to_string();
        auto prop_pretty_name = rttr::get_pretty_name(prop);
        
        auto& override_ctx_ref = const_cast<prefab_override_context&>(override_ctx);
        override_ctx_ref.set_component_type(type_name, pretty_name);
        override_ctx_ref.push_segment(prop_name, prop_pretty_name);

        property_layout layout(prop, true);
        rttr::variant v = tag_comp->tag;

        var_info info;
        info.is_property = true;
        info.read_only = false;

        result |= inspect_var(ctx, v, info);
        if(result.changed)
        {
            tag_comp->tag = v.get_value<std::string>();
        }

        override_ctx_ref.pop_segment();
    }
    
    return result;
}

} // namespace

auto inspector_entity::inspect_as_property(rtti::context& ctx, entt::handle& data) -> inspect_result
{
    auto name = get_entity_pretty_name(data);

    inspect_result result;

    if(ImGui::Button(ICON_MDI_DELETE, ImVec2(0.0f, ImGui::GetFrameHeight())))
    {
        if(data)
        {
            data = {};
            result.changed = true;
            result.edit_finished = true;
        }
    }

    ImGui::SameLine();
    auto id = fmt::format("{} {}", ICON_MDI_CUBE, name);
    if(ImGui::Button(id.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight())))
    {
        auto& em = ctx.get_cached<editing_manager>();

        em.focus(data);
    }

    ImGui::SetItemTooltipEx("%s", id.c_str());

    bool drag_dropped = process_drag_drop_target(ctx, data);
    result.changed |= drag_dropped;
    result.edit_finished |= drag_dropped;

    return result;
}

auto inspector_entity::inspect(rtti::context& ctx,
                               rttr::variant& var,
                               const var_info& info,
                               const meta_getter& get_metadata) -> inspect_result
{
    inspect_result result{};
    auto data = var.get_value<entt::handle>();

    if(info.is_property)
    {
        result = inspect_as_property(ctx, data);
    }
    else
    {
        if(!data)
        {
            return result;
        }

        auto& override_ctx = ctx.get_cached<prefab_override_context>();

        // Render Unity-style entity header (active checkbox, icon, name, tag)
        result |= render_entity_header(ctx, data, override_ctx);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if(is_debug_view())
        {
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
            ImGui::TreePush("Entity");
            {
                property_layout layout("Entity");
                const auto ent = data.entity();
                const auto idx = entt::to_entity(ent);
                const auto ver = entt::to_version(ent);
                const auto id = entt::to_integral(ent);
    
                //ImGui::SetItemTooltipEx("Id: %d\nIndex: %d\nVersion: %d", id, idx, ver);
                ImGui::Text("Id: %u, Index: %u, Version: %u", id, idx, ver);
            }
            
            ImGui::TreePop();
            ImGui::PopStyleVar();
        }

        hpp::for_each_tuple_type<all_inspectable_components>(
            [&](auto index)
            {
                using ctype = std::tuple_element_t<decltype(index)::value, all_inspectable_components>;
                auto component = data.try_get<ctype>();

                if(!component)
                {
                    return;
                }
                
                // Skip tag_component as it's handled in the Unity-style header
                if constexpr(std::is_same_v<ctype, tag_component>)
                {
                    return;
                }

                auto type = rttr::type::get<ctype>();
                auto name = type.get_name().to_string();
                auto pretty_name = rttr::get_pretty_name(type);
                
                // Track component type for prefab override context
                override_ctx.set_component_type(name, pretty_name);


                inspect_callbacks callbacks;

                callbacks.on_inspect = [&]() -> inspect_result
                {
                    
                    if constexpr(std::is_base_of<owned_component, ctype>::value)
                    {
                        if(is_debug_view())
                        {
                            property_layout layout("Owner");
                            ImGui::Text("%u", uint32_t(component->get_owner().entity()));
                        }
                    }
                    return ::unravel::inspect(ctx, component);
                };

                callbacks.on_add = [&]()
                {
                    data.emplace<ctype>();
                };

                callbacks.on_remove = [&]()
                {
                    data.remove<ctype>();
                };

                callbacks.can_remove = []()
                {
                    return !std::is_same<ctype, id_component>::value && !std::is_same<ctype, tag_component>::value &&
                           !std::is_same<ctype, transform_component>::value && !std::is_same<ctype, prefab_id_component>::value &&
                           !std::is_same<ctype, layer_component>::value && !std::is_same<ctype, bone_component>::value &&
                           !std::is_same<ctype, submesh_component>::value;
                };

                callbacks.can_merge = []()
                {
                    return std::is_same<ctype, id_component>::value || std::is_same<ctype, tag_component>::value;
                };

                
                callbacks.icon = get_component_icon<ctype>();
                
                result |= inspect_component(pretty_name, callbacks);
            });

        auto script_comp = data.try_get<script_component>();
        if(script_comp)
        {
            const auto& comps = script_comp->get_script_components();

            int index_to_remove = -1;
            int index_to_add = -1;
            for(size_t i = 0; i < comps.size(); ++i)
            {
                ImGui::PushID(i);
                const auto& script = comps[i];
                const auto& type = script.scoped->object.get_type();
                fs::path source_loc = script_comp->get_script_source_location(script);

                inspect_callbacks callbacks;
                callbacks.on_inspect = [&]() -> inspect_result
                {
                    inspect_result inspect_res{};

                    if(!source_loc.empty())
                    {
                        var_info field_info;
                        field_info.is_property = true;
                        field_info.read_only = true;
                        ImGui::PushReadonly(field_info.read_only);

                        std::string var = ICON_MDI_SCRIPT " " + source_loc.stem().string();
                        {
                            property_layout layout("Script");

                            if(ImGui::Button(var.c_str(), ImVec2(-1.0f, ImGui::GetFrameHeight())))
                            {
                                auto& em = ctx.get_cached<editing_manager>();

                                em.focus(source_loc);
                                em.focus_path(source_loc.parent_path());
                            }

                            if(ImGui::IsItemDoubleClicked(ImGuiMouseButton_Left))
                            {
                                editor_actions::open_workspace_on_file(source_loc);
                            }
                        }
                        ImGui::PopReadonly();
                    }
                    rttr::variant obj = static_cast<mono::mono_object&>(script.scoped->object);
                    inspect_res |= ::unravel::inspect_var(ctx, obj);
                    return inspect_res;
                };

                callbacks.on_add = [&]()
                {
                    index_to_add = i;
                };

                callbacks.on_remove = [&]()
                {
                    index_to_remove = i;
                };

                callbacks.can_remove = []()
                {
                    return true;
                };

                callbacks.can_merge = []()
                {
                    return false;
                };

                callbacks.icon = ICON_MDI_SCRIPT;

                auto name = type.get_fullname();
                const auto& pretty_name = name;

                auto script_type = rttr::type::get<script_component>();
                auto script_type_name = script_type.get_name().to_string();
                auto script_type_pretty_name = rttr::get_pretty_name(script_type);
                // Track component type for prefab override context
                override_ctx.set_component_type(script_type_name, script_type_pretty_name);

                override_ctx.push_segment("script_components/" + name, "Scripts/" + pretty_name);

                result |= inspect_component(pretty_name, callbacks);

                override_ctx.pop_segment();

                ImGui::PopID();
            }

            if(index_to_remove != -1)
            {
                auto comp_to_remove = comps[index_to_remove];

                script_component::script_object comp_to_add;

                auto type = comp_to_remove.scoped->object.get_type();
                script_comp->remove_script_component(comp_to_remove.scoped->object);
                script_comp->process_pending_deletions();

                if(index_to_add != -1)
                {
                    script_comp->add_script_component(type);
                }

                result.changed |= true;
                result.edit_finished |= true;
            }
        }

        ImGui::Separator();
        ImGui::NextLine();
        static const auto label = "Add Component";
        auto avail = ImGui::GetContentRegionAvail();
        ImVec2 size = ImGui::CalcItemSize(label);
        size.x *= 2.0f;
        ImGui::AlignedItem(0.5f,
                           avail.x,
                           size.x,
                           [&]()
                           {
                               auto pos = ImGui::GetCursorScreenPos();
                               if(ImGui::Button(label, size))
                               {
                                   ImGui::OpenPopup("COMPONENT_MENU");
                                   ImGui::SetNextWindowPos(pos);
                               }
                           });

        if(ImGui::BeginPopup("COMPONENT_MENU"))
        {
            if(ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
            }

            ImGui::DrawFilterWithHint(filter_, ICON_MDI_SELECT_SEARCH " Search...", size.x);
            ImGui::DrawItemActivityOutline();

            ImGui::Separator();
            ImGui::BeginChild("COMPONENT_MENU_CONTEXT", ImVec2(ImGui::GetContentRegionAvail().x, size.x));

            const auto& scr = ctx.get_cached<script_system>();
            for(const auto& type : scr.get_all_scriptable_components())
            {
                const auto& name = type.get_fullname();

                inspect_callbacks callbacks;

                callbacks.on_add = [&]()
                {
                    data.get_or_emplace<script_component>().add_script_component(type);
                    result.changed |= true;
                    result.edit_finished |= true;
                };

                callbacks.on_remove = [&]()
                {
                };

                callbacks.can_remove = []()
                {
                    return true;
                };

                callbacks.can_merge = []()
                {
                    return false;
                };

                callbacks.icon = ICON_MDI_SCRIPT;

                result |= list_component(filter_, name, callbacks);
            }

            hpp::for_each_tuple_type<all_addable_components>(
                [&](auto index)
                {
                    using ctype = std::tuple_element_t<decltype(index)::value, all_addable_components>;

                    auto name = rttr::get_pretty_name(rttr::type::get<ctype>());

                    inspect_callbacks callbacks;

                    callbacks.on_add = [&]()
                    {
                        data.emplace<ctype>();
                        result.changed |= true;
                        result.edit_finished |= true;
                    };

                    callbacks.on_remove = [&]()
                    {
                        data.remove<ctype>();
                        result.changed |= true;
                        result.edit_finished |= true;
                    };

                    callbacks.can_remove = []()
                    {
                        return true;
                    };

                    callbacks.can_merge = []()
                    {
                        return false;
                    };

                    // callbacks.icon = ICON_MDI_GRID;

                    result |= list_component(filter_, name, callbacks);
                });

            ImGui::EndChild();
            ImGui::EndPopup();
        }
    }

    if(result.changed)
    {
        if(auto prefab = data.try_get<prefab_component>())
        {
            prefab->changed = true;
        }
        var = data;
    }

    return result;
}
} // namespace unravel
