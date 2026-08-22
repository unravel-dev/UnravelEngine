#include "inspector_entity.h"
#include "entt/meta/meta.hpp"
#include "inspector.h"
#include "inspectors.h"
#include "reflection/reflection.h"

#include <editor/imgui/integration/imgui_context_menu_style.h>
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
#include <hpp/finally.hpp>
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
        return ICON_MDI_LANGUAGE_CSHARP;
    }
    // Post-processing effects (using similar icons for consistency)
    else if constexpr(std::is_same<T, auto_exposure_component>::value)
    {
        return ICON_MDI_BRIGHTNESS_AUTO;
    }
    else if constexpr(std::is_same<T, bloom_component>::value)
    {
        return ICON_MDI_WHITE_BALANCE_SUNNY;
    }
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
    else if constexpr(std::is_same<T, volume_component>::value)
    {
        return ICON_MDI_VIEW_AGENDA;
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

        ImGui::PushFont(ImGui::Font::SemiBold);
        open = ImGui::CollapsingHeader(fmt::format("     {}", name).c_str(), nullptr, ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::PopFont();

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
    if(is_popup_open && ImGui::BeginPopup(popup_str))
    {
        {
            ImGui::ContextMenuStyleScope style_scope;

            bool removal_allowed = callbacks.can_remove();
            if(ImGui::MenuItemIcon(ICON_MDI_RESTORE, "Reset", nullptr, removal_allowed))
            {
                callbacks.on_remove();
                callbacks.on_add();

                result.changed = true;
                result.edit_finished = true;
            }

            ImGui::Separator();
            if(ImGui::MenuItemIcon(ICON_MDI_DELETE, "Remove Component", nullptr, removal_allowed))
            {
                callbacks.on_remove();
                result.changed = true;
                result.edit_finished = true;
            }
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

auto render_entity_header(rtti::context& ctx, entt::handle data, prefab_override_context& override_ctx) -> inspect_result
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
            auto type = entt::resolve<transform_component>();
            auto name = entt::get_name(type);
            auto pretty_name = entt::get_pretty_name(type);
            auto prop = type.data("active"_hs);
            auto prop_name = entt::get_name(prop);
            auto prop_pretty_name = entt::get_pretty_name(prop);

            override_ctx.set_component_type(name, pretty_name);
            override_ctx.push_segment(prop_name, prop_pretty_name);

            bool old_active = is_active;
            if(ImGui::Checkbox("##active", &is_active))
            {
                result.changed = true;
                result.edit_finished = true;

                auto& em = ctx.get_cached<editing_manager>();
                em.do_action<entity_set_active_action_t>({},
                    data,
                    old_active,
                    is_active);
            }
            
            override_ctx.pop_segment();
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
            auto type = entt::resolve<tag_component>();
            auto type_name = entt::get_name(type);
            auto pretty_name = entt::get_pretty_name(type);
            auto prop = type.data("name"_hs);
            auto prop_name = entt::get_name(prop);
            auto prop_pretty_name = entt::get_pretty_name(prop);
            
            override_ctx.set_component_type(type_name, pretty_name);
            override_ctx.push_segment(prop_name, prop_pretty_name);
                        
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::SetNextItemWidth(-1.0f);
            
            auto old_name = tag_comp->name;
            if(ImGui::InputTextWidget("##name", tag_comp->name, false))
            {
                result.changed = true;
                result.edit_finished = true;

                auto& em = ctx.get_cached<editing_manager>();      

                em.do_action<entity_set_name_action_t>({},
                    data,
                    old_name,
                    tag_comp->name);
            }
            
            ImGui::PopStyleVar();
            override_ctx.pop_segment();
        }
        ImGui::PopStyleColor();

        ImGui::EndTable();
    }
    
    // Tag field using traditional property_layout approach
    {
        auto type = entt::resolve<tag_component>();
        auto type_name = entt::get_name(type);
        auto pretty_name = entt::get_pretty_name(type);

        auto prop = type.data("tag"_hs);
        auto prop_name = entt::get_name(prop);
        auto prop_pretty_name = entt::get_pretty_name(prop);
        
        override_ctx.set_component_type(type_name, pretty_name);
        override_ctx.push_segment(prop_name, prop_pretty_name);

        property_layout layout(prop, tag_comp, true);

        var_info info;
        info.is_property = true;
        info.read_only = false;

        auto old_tag = tag_comp->tag;

        auto v_var = entt::forward_as_meta(tag_comp->tag);
        auto var_result = ::unravel::inspect_var(ctx, v_var, make_proxy(v_var), info);

        if(var_result.changed)
        {
            auto& em = ctx.get_cached<editing_manager>();      

            em.do_action<entity_set_tag_action_t>({},
                data,
                old_tag,
                tag_comp->tag);

        }

        result |= var_result;

        override_ctx.pop_segment();
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
                               entt::meta_any& var,
                               const meta_any_proxy& var_proxy,
                               const var_info& info,
                               const entt::meta_custom& custom) -> inspect_result
{
    inspect_result result{};
    auto data = var.cast<entt::handle>();

    if(data)
    {
        auto& inspector_ctx = ctx.get_cached<inspector_context>();
        inspector_ctx.inspected_registry = data.registry();
    }

    auto cleanup = hpp::finally([&]()
    {
        auto& inspector_ctx = ctx.get_cached<inspector_context>();
        inspector_ctx.inspected_registry = nullptr;
    });

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


        // Keep "Add Component" pinned below the scrollable component list.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float add_component_footer_height =
            style.ItemSpacing.y * 4.0f + style.WindowPadding.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.WindowRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.WindowPadding);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        ImGui::BeginChild("ENTITY_COMPONENTS",
                          ImVec2(0.0f, -add_component_footer_height),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

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

                auto type = entt::resolve<ctype>();
                auto name = entt::get_name(type);
                auto pretty_name = entt::get_pretty_name(type);
                
                // Track component type for prefab override context
                override_ctx.set_component_type(std::string(name), pretty_name);


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

                    meta_any_proxy comp_var_proxy;
                    comp_var_proxy.impl->parent = var_proxy.impl;
                    comp_var_proxy.impl->type_name = entt::get_pretty_name(type);
                    comp_var_proxy.impl->name = [&]()
                    {
                        const auto& name = var_proxy.impl->name;
                        if(name.empty())
                        {
                            return pretty_name;
                        }
                        return fmt::format("{}/{}", name, pretty_name);
                    }();
                    comp_var_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
                    {
                        entt::meta_any var;
                        if(parent_proxy.impl->getter(var) && var)
                        {
                            auto data = var.cast<entt::handle>();
                            if(data)
                            {
                                auto component = data.try_get<ctype>();
                                if(component)
                                {
                                    result = entt::forward_as_meta(*component);
                                    return true;
                                }
                            }
                        }
                        return false;
                    };
                    comp_var_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
                    {
                        return parent_proxy.impl->setter(parent_proxy, value, execution_count);
                    };
                    // entt::meta_any comp_var;
                    // call_var_getter(comp_var, comp_var_getter);
                    auto comp_var = entt::forward_as_meta(*component);

                    var_info comp_info;
                    comp_info.is_copyable = false;
                    return ::unravel::inspect_var(ctx, comp_var, comp_var_proxy, comp_info);
                    
                };

                callbacks.on_add = [&]()
                {
                    // data.emplace<ctype>();

                    auto& em = ctx.get_cached<editing_manager>();
                    em.do_action<entity_add_component_action_t>({}, data, type);
                };

                callbacks.on_remove = [&]()
                {
                    if(data.all_of<ctype>())
                    {
                        auto& em = ctx.get_cached<editing_manager>();
                        em.do_action<entity_remove_component_action_t>({}, data, type);
                    }
                    
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
                const auto& mono_obj = script.pinned->get_object();
                if(!mono_obj.valid())
                {
                    APPLOG_ERROR("Script object is invalid for domain version: {}", script.pinned->get_domain_version());
                    continue;
                }
                const auto& type = mono_obj.get_type();
                if(!type.valid())
                {
                    APPLOG_ERROR("Script object type is invalid for domain version: {}", script.pinned->get_domain_version());
                    continue;
                }
                fs::path source_loc = script_comp->get_script_source_location(script);

                auto name = type.get_fullname();
                const auto& pretty_name = name;

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

                        std::string var = ICON_MDI_LANGUAGE_CSHARP " " + source_loc.stem().string();
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

                    meta_any_proxy obj_proxy;
                    obj_proxy.impl->parent = var_proxy.impl;
                    obj_proxy.impl->name = [&]()
                    {
                        const auto& name = var_proxy.impl->name;
                        if(name.empty())
                        {
                            return pretty_name;
                        }
                        return fmt::format("{}/{}", name, pretty_name);
                    }();
                    obj_proxy.impl->getter = [parent_proxy = var_proxy, i](entt::meta_any& result)
                    {
                        entt::meta_any var;
                        if(parent_proxy.impl->getter(var) && var)
                        {
                            auto data = var.cast<entt::handle>();
                            if(data)
                            {
                                auto script_comp = data.try_get<script_component>();
                                if(script_comp)
                                {
                                    const auto& comps = script_comp->get_script_components();
                                    if(i < comps.size())
                                    {
                                        auto& script = comps[i];
                                        result = script.pinned;//entt::forward_as_meta(script.pinned);
                                        return true;
                                    }
                                }
                            }
                        }
                        return false;
                    };
                    obj_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
                    {
                        return parent_proxy.impl->setter(parent_proxy, value, execution_count);
                    };
                    // entt::meta_any obj_var;
                    // call_var_getter(obj_var, obj_getter);
                    entt::meta_any obj_var = script.pinned;//entt::forward_as_meta(*script.pinned);
                    obj_proxy.impl->type_name = entt::get_pretty_name(obj_var.type());

                    var_info obj_info;
                    obj_info.is_copyable = false;

                    inspect_res |= ::unravel::inspect_var(ctx, obj_var, obj_proxy, obj_info);
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

                callbacks.icon = ICON_MDI_LANGUAGE_CSHARP;


                auto script_type = entt::resolve<script_component>();
                auto script_type_name = entt::get_name(script_type);
                auto script_type_pretty_name = entt::get_pretty_name(script_type);
                // Track component type for prefab override context
                override_ctx.set_component_type(std::string(script_type_name), script_type_pretty_name);

                std::string segment = fmt::format("script_components[{}]/{}", i, name);
                std::string pretty_segment = fmt::format("Scripts[{}]/{}", i, pretty_name);
                override_ctx.push_segment(segment, pretty_segment);

                result |= inspect_component(pretty_name, callbacks);

                override_ctx.pop_segment();

                ImGui::PopID();
            }

            if(index_to_remove != -1)
            {
                auto comp_to_remove = comps[index_to_remove];

                script_component::script_object comp_to_add;

                auto type = comp_to_remove.pinned->get_object().get_type();

                auto& em = ctx.get_cached<editing_manager>();
                auto script_type_name = type.get_fullname();
                em.do_action<entity_remove_script_component_action_t>({}, data, script_type_name, index_to_remove);

                // script_comp->remove_script_component(comp_to_remove.pinned->object);
                // script_comp->process_pending_deletions();

                if(index_to_add != -1)
                {
                    // script_comp->add_script_component(type);

                    em.do_action<entity_add_script_component_action_t>({}, data, script_type_name);
                }

                result.changed |= true;
                result.edit_finished |= true;
            }
        }

        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Spacing();
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
                    //data.get_or_emplace<script_component>().add_script_component(type);
                    auto& em = ctx.get_cached<editing_manager>();
                    em.do_action<entity_add_script_component_action_t>({}, data, name);
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

                callbacks.icon = ICON_MDI_LANGUAGE_CSHARP;

                result |= list_component(filter_, name, callbacks);
            }

            hpp::for_each_tuple_type<all_addable_components>(
                [&](auto index)
                {
                    using ctype = std::tuple_element_t<decltype(index)::value, all_addable_components>;

                    auto name = entt::get_pretty_name(entt::resolve<ctype>());

                    auto type = entt::resolve<ctype>();

                    inspect_callbacks callbacks;

                    callbacks.on_add = [&]()
                    {
                        // data.emplace<ctype>();
                        auto& em = ctx.get_cached<editing_manager>();
                        em.do_action<entity_add_component_action_t>({}, data, type);


                        result.changed |= true;
                        result.edit_finished |= true;
                    };

                    callbacks.on_remove = [&]()
                    {
                        if(data.all_of<ctype>())
                        {
                            auto& em = ctx.get_cached<editing_manager>();
                            em.do_action<entity_remove_component_action_t>({}, data, type);
                            result.changed |= true;
                            result.edit_finished |= true;
                        }
                        
                        
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
        if(data)
        {
            if(auto prefab = data.try_get<prefab_component>())
            {
                prefab->changed = true;
            }

            // Component data was edited in place - wake the indexed transform-dirty
            // consumers so derived render data is re-consumed (e.g. the model pose
            // refresh picks up submesh_component entry edits on armature nodes).
            if(auto transform_comp = data.try_get<transform_component>())
            {
                transform_comp->mark_consumers_dirty();
            }
        }
        
        var = data;
    }

    return result;
}
} // namespace unravel
