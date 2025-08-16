#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/ecs/ecs.h>

#include <editor/imgui/integration/imgui.h>
#include <editor/shortcuts.h>
#include <rttr/type.h>

namespace unravel
{
class imgui_panels;

class entity_panel
{
public:
    entity_panel(imgui_panels* parent);

    ~entity_panel() = default;


    void on_frame_ui_render();

    void duplicate_entities(const std::vector<entt::handle>& entities);

    void focus_entities(entt::handle camera, const std::vector<entt::handle>& entities);

    void delete_entities(const std::vector<entt::handle>& entities);
    
    static auto get_entity_name(entt::handle entity) -> std::string;
    static void set_entity_name(entt::handle entity, const std::string& name);

    static auto get_entity_icon(entt::handle entity) -> std::string;
    static auto get_entity_display_color(entt::handle entity) -> ImVec4;
    
protected:

    imgui_panels* parent_{};
};
} // namespace unravel
