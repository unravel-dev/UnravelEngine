#include "inspector.h"
#include <imgui/imgui_internal.h>
#include <string_utils/utils.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/meta/ecs/entity.hpp>
#include "imgui/imgui.h"
#include "inspectors.h"

namespace ImGui
{
    auto GetTintedStyleColor(ImGuiCol idx, float multiplier = 1.0f) -> ImVec4
    {
        auto color = ImGui::GetStyleColorVec4(idx);
        color.x *= multiplier;
        color.y *= multiplier;
        color.z *= multiplier;
        return color;
    }
}

namespace unravel
{

namespace
{
std::vector<property_layout*> stack;
void push_layout_to_stack(property_layout* l)
{
    stack.push_back(l);
}

void pop_layout_from_stack(property_layout* l)
{
    stack.pop_back();
}
} // namespace


property_layout_group::property_layout_group(const std::string& name)
{
    // ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetTintedStyleColor(ImGuiCol_ChildBg, 0.8f));
    // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    // ImGui::BeginChild(name.c_str(), {-1.0f, 0.0f}, ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
    
}

property_layout_group::~property_layout_group()
{
    // ImGui::EndChild();
    // ImGui::PopStyleVar();
    // ImGui::PopStyleColor();
    // ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImGuiCol_Separator), 2.0f);

}

auto property_layout::get_current() -> property_layout*
{
    if(stack.empty())
    {
        return nullptr;
    }
    return stack.back();
}

property_layout::property_layout()
{
    push_layout_to_stack(this);
}

property_layout::property_layout(const entt::meta_data& prop, const entt::meta_any& object, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    set_data(prop, object, columns);

    push_layout();
}

property_layout::property_layout(const std::string& name, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    set_data(name, "", "", columns);

    push_layout();
}

property_layout::property_layout(const std::string& name, const std::string& tooltip, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    set_data(name, tooltip,  "", columns);

    push_layout();
}

property_layout::property_layout(const std::string& name, const std::string& tooltip, const std::string& hint, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    set_data(name, tooltip, hint, columns);

    push_layout();
}

property_layout::property_layout(const std::string& name, const std::function<void()>& callback, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    callback_ = callback;
    set_data(name, "", "", columns);

    push_layout();
}


property_layout::~property_layout()
{  
    pop_layout();

    pop_layout_from_stack(this);
}

void property_layout::set_data(const entt::meta_data& prop, const entt::meta_any& object, bool columns)
{
    auto name = entt::get_pretty_name(prop);

    auto hint = entt::get_property_hint(object, prop);

    auto tooltip = entt::get_attribute_as<std::string>(prop, "tooltip");

    set_data(name, tooltip, hint, columns);

}

void property_layout::set_data(const std::string& name, const std::string& tooltip, const std::string& hint, bool columns)
{
    name_ = string_utils::capitalize(name);
    tooltip_ = tooltip;
    hint_ = hint;
    columns_ = columns;
}

void property_layout::push_layout(bool auto_proceed_to_next_column)
{
    pushed_ = true;

    if(columns_)
    {
        auto avail = ImGui::GetContentRegionAvail();

        columns_open_ = ImGui::BeginTable(("properties##" + name_).c_str(), 2);

        if(columns_open_)
        {

            auto first_column = 0.325f;
            ImGui::TableSetupColumn("##prop_column1", ImGuiTableColumnFlags_WidthFixed, avail.x * first_column);
            ImGui::TableSetupColumn("##prop_column2", ImGuiTableColumnFlags_WidthFixed, avail.x * (1.0f - first_column));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
        }
    }

    ImGui::AlignTextToFramePadding();
    if(callback_)
    {
        callback_();
    }
    else
    {
        ImGui::TextUnformatted(name_.c_str());
    }
 
             
    
    if(ImGui::BeginPopupContextItem(("Property Context Menu##" + name_).c_str()))
    {
        auto& ctx = engine::context();
        auto& override_ctx = ctx.get_cached<prefab_override_context>();

        if(override_ctx.is_path_overridden())
        {
            if(ImGui::MenuItem(fmt::format("Reset {} to default", name_).c_str()))
            {
                override_ctx.reset_override();
            }
        }

        ImGui::EndPopup();
    }

    if(!tooltip_.empty())
    {
        
        auto tooltip_callback = [&]()
        {
            ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushFont(ImGui::Font::Bold);
            ImGui::PushWindowFontScale(1.2f);
            ImGui::Text("%s", name_.c_str());
            ImGui::PopWindowFontScale();
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("%s", tooltip_.c_str());
            ImGui::PopStyleColor();
        };
        ImGui::ItemTooltipEx(tooltip_callback);

        ImGui::SameLine();
        ImGui::HelpMarker("(?)", true, tooltip_callback);
    }

    if(auto_proceed_to_next_column)
    {
        prepare_for_item();
    }
}

void property_layout::prepare_for_item()
{
    if(columns_open_)
    {
        ImGui::TableNextColumn();
    }

    ImGui::PushID(name_.c_str());
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
}

auto property_layout::push_tree_layout(ImGuiTreeNodeFlags flags) -> bool
{
    pushed_ = true;

    // group_ = property_layout_group(name_);

    if(columns_)
    {
        auto avail = ImGui::GetContentRegionAvail();

        columns_open_ = ImGui::BeginTable(("properties##" + name_).c_str(), 2);

        if(columns_open_)
        {
            auto first_column = 0.325f;
            ImGui::TableSetupColumn("##prop_column1", ImGuiTableColumnFlags_WidthFixed, avail.x * first_column);
            ImGui::TableSetupColumn("##prop_column2", ImGuiTableColumnFlags_WidthFixed, avail.x * (1.0f - first_column));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
        }
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    ImGui::AlignTextToFramePadding();
    // ImGui::PushFont(ImGui::Font::Bold);
    open_ = ImGui::TreeNodeEx(name_.c_str(), flags | ImGuiTreeNodeFlags_AllowOverlap);
    // open_ = ImGui::CollapsingSection(name_.c_str(), flags | ImGuiTreeNodeFlags_AllowOverlap);
    // ImGui::PopFont();           
    
    if(ImGui::BeginPopupContextItem(("Property Context Menu##" + name_).c_str()))
    {
        auto& ctx = engine::context();
        auto& override_ctx = ctx.get_cached<prefab_override_context>();

        if(override_ctx.is_path_overridden())
        {
            if(ImGui::MenuItem(fmt::format("Reset {} to default", name_).c_str()))
            {
                override_ctx.reset_override();
            }
        }

        ImGui::EndPopup();
    }

    if(!tooltip_.empty())
    {
        
        auto tooltip_callback = [&]()
        {
            ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushFont(ImGui::Font::Bold);
            ImGui::PushWindowFontScale(1.2f);
            ImGui::Text("%s", name_.c_str());
            ImGui::PopWindowFontScale();
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("%s", tooltip_.c_str());
            ImGui::PopStyleColor();
        };
        ImGui::ItemTooltipEx(tooltip_callback);

        ImGui::SameLine();
        ImGui::HelpMarker("(?)", true, tooltip_callback);
    }
    
    prepare_for_item();

    return open_;
}

void property_layout::pop_layout()
{
    if(!pushed_)
    {
        return;
    }

    ImGui::PopID();
    ImGui::PopItemWidth();

    if(open_)
    {
        open_ = false;
        ImGui::TreePop();
    }

    if(columns_)
    {
        columns_ = false;
        if(columns_open_ && ImGui::TableGetColumnCount() > 1)
        {
            ImGui::EndTable();
        }
    }

    

    
    if(!hint_.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.0f, 1.0f), "%s",hint_.c_str());
        ImGui::PopTextWrapPos();
    }
    // group_.reset();

    pushed_ = false;
}

void inspector::before_inspect(const entt::meta_data& prop, const entt::meta_any& object)
{
    layout_ = std::make_unique<property_layout>(prop, object);
}

void inspector::after_inspect(const entt::meta_data& prop, const entt::meta_any& object)
{
    layout_.reset();
}


auto make_proxy(entt::meta_any& var, const std::string& name) -> meta_any_proxy
{
    meta_any_proxy proxy;
    proxy.impl->parent = nullptr;
    proxy.impl->type_name = entt::get_pretty_name(var.type());
    proxy.impl->name = name;
    proxy.impl->resolver = [var](entt::meta_any& result) mutable
    {
        result = var;
        return true;
    };
    proxy.impl->getter = [var = var.as_ref()](entt::meta_any& result) mutable
    {
        result = var.as_ref();
        return true;
    };
    proxy.impl->setter = [](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(proxy.impl->getter(var) && var)
        {
            return var.assign(value);
        }
        return false;
    };
    return proxy;
}

auto make_entity_proxy(entt::meta_any& var, const std::string& name) -> meta_any_proxy
{
    if(var.type() != entt::resolve<entt::uhandle>())
    {
        return make_proxy(var, name);
    }
    auto handle = var.cast<entt::uhandle>();
    
    meta_any_proxy proxy;
    proxy.impl->parent = nullptr;
    proxy.impl->type_name = entt::get_pretty_name(var.type());
    proxy.impl->name = name;
    proxy.impl->resolver = [handle](entt::meta_any& result) mutable -> bool
    {
        result = handle;
        return !!result;
    };
    proxy.impl->getter = [handle](entt::meta_any& result) mutable -> bool
    {
        result = handle;
        return !!result;
    };
    proxy.impl->setter = [](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var_slot;
        if(proxy.impl->getter(var_slot) && var_slot)
        {
            return var_slot.assign(value);
        }
        return false;
    };
    return proxy;
}

auto make_property_proxy(const meta_any_proxy& var_proxy, const entt::meta_data& prop) -> meta_any_proxy
{
    meta_any_proxy prop_proxy;
    prop_proxy.impl->parent = var_proxy.impl;
    prop_proxy.impl->type_name = entt::get_pretty_name(prop.type());
    prop_proxy.impl->name = [&]()
    {
        const auto& name = var_proxy.impl->name;
        if(name.empty())
        {
            return entt::get_pretty_name(prop);
        }
        return fmt::format("{}/{}", name, entt::get_pretty_name(prop));
    }();
    prop_proxy.impl->getter = [parent_proxy = var_proxy, prop](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            result = prop.get(var);
            return true;
        }
        return false;
    };
    prop_proxy.impl->setter = [parent_proxy = var_proxy, prop](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            prop.set(var, value);
            return parent_proxy.impl->setter(parent_proxy, var, execution_count);
        }
        return false;
    };
    return prop_proxy;
}
} // namespace unravel
