#pragma once
#include <editor/imgui/integration/imgui.h>

#include "../entity_panel.h"
#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace unravel
{
// Forward declarations
class editing_manager;
class scene;

/// The component a search found an entity by.
struct hierarchy_component_match
{
    std::string name;
    std::string icon;
};

/// What a search of the hierarchy shows: the entities whose name matches, those with a component
/// whose name matches, and every entity on the way down to one of them, so a match is always seen
/// where it lives.
struct hierarchy_search
{
    std::unordered_set<entt::entity> name_matches;
    /// The first matching component of each entity found by one.
    std::unordered_map<entt::entity, hierarchy_component_match> component_matches;
    std::unordered_set<entt::entity> shown;
    /// The first term that is not for components only, marked in the names that match.
    std::string highlight;
    bool is_active{};

    auto is_shown(entt::entity entity) const -> bool
    {
        return !is_active || shown.contains(entity);
    }

    auto is_match(entt::entity entity) const -> bool
    {
        return !is_active || name_matches.contains(entity) || component_matches.contains(entity);
    }

    auto is_name_match(entt::entity entity) const -> bool
    {
        return is_active && name_matches.contains(entity);
    }

    auto find_component_match(entt::entity entity) const -> const hierarchy_component_match*
    {
        const auto it = component_matches.find(entity);
        return it != component_matches.end() ? &it->second : nullptr;
    }

    auto is_empty() const -> bool
    {
        return name_matches.empty() && component_matches.empty();
    }
};

/// The entities of the active scene as a tree, one row each: the name, then a column per value
/// that sets an entity apart (static, layer, tag), editable in place. A toolbar above it creates
/// entities and searches them.
class hierarchy_panel : public entity_panel
{
public:
    hierarchy_panel(imgui_panels* parent, const char* name);

    void init(rtti::context& ctx);

    void draw_ui(rtti::context& ctx) override;
    void on_after_render(rtti::context& ctx) override;

    auto get_window_flags() const -> ImGuiWindowFlags override;

private:
    void draw_toolbar(rtti::context& ctx);
    void draw_create_dropdown(rtti::context& ctx);
    void draw_search_field();
    void update_search(scene& target_scene);
    void add_search_path(entt::registry& registry, entt::entity entity);
    auto get_scene_display_name(const editing_manager& em, scene* target_scene) const -> std::string;
    void draw_scene_table(rtti::context& ctx, scene& target_scene);
    void draw_scene_rows(rtti::context& ctx, scene& target_scene, bool is_focused);
    void handle_window_empty_click(rtti::context& ctx) const;

    ImGuiTextFilter filter_;
    hierarchy_search search_;
};
} // namespace unravel
