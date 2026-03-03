#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/ecs/ecs.h>

#include "panel_base.h"
#include <editor/imgui/integration/imgui.h>
#include <editor/shortcuts.h>

namespace unravel
{
class imgui_panels;

class entity_panel : public panel_base
{
public:
    entity_panel(imgui_panels* parent, const char* name);

    virtual ~entity_panel() = default;

    void duplicate_entities(const std::vector<entt::handle>& entities);

    void focus_entities(entt::handle camera, const std::vector<entt::handle>& entities);

    void delete_entities(const std::vector<entt::handle>& entities);
    
    static auto get_entity_name(entt::handle entity) -> std::string;

    static auto get_entity_icon(entt::handle entity) -> std::string;
    static auto get_entity_display_color(entt::handle entity) -> ImVec4;
    
protected:

    imgui_panels* parent_{};
};
} // namespace unravel
