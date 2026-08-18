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
    
    /**
     * @brief Where an entity in a prefab instance came from.
     *
     * The hierarchy showed everything with any prefab component in one colour, so a nested
     * instance the prefab supplies and one added here looked identical - and so did an
     * entity of the asset and an entity added inside it.
     */
    enum class prefab_role
    {
        /// Nothing to do with a prefab.
        none,

        /// A prefab instance placed directly in whatever it lives in, rather than nested
        /// inside another instance. The ordinary case, and nothing to call out.
        instance,

        /// A prefab instance the prefab containing it supplies. Its instance id names which.
        linked_instance,

        /// A prefab instance added or cloned inside another instance. No instance id, and no
        /// document above it will ever remove or replace it.
        local_instance,

        /// An entity the instance's own asset supplies.
        asset_content,

        /// An entity added inside an instance. Its asset knows nothing about it.
        local_content,
    };

    static auto get_entity_prefab_role(entt::handle entity) -> prefab_role;

    /// One-line explanation of a role, for a tooltip. Empty for `none`.
    static auto describe_prefab_role(prefab_role role) -> const char*;

    static auto get_entity_name(entt::handle entity) -> std::string;

    static auto get_entity_icon(entt::handle entity) -> std::string;
    static auto get_entity_display_color(entt::handle entity) -> ImVec4;
    
protected:

    imgui_panels* parent_{};
};
} // namespace unravel
