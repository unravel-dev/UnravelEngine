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

/**
 * @brief One node of the override tree: a path segment, and whatever sits under it.
 *
 * Overrides are stored flat, as one entry per property path, which reads as an undifferentiated
 * list once there are more than a handful. The paths are already hierarchical - "Transform/
 * Position/X" - so they are regrouped here into the tree they describe.
 *
 * A node can be both: "Transform/Position" may be overridden and so may "Transform/Position/X".
 */
struct override_node
{
    std::map<std::string, override_node> children;

    /// Set when an override ends exactly here.
    const prefab_property_override_data* leaf{};
    bool leaf_is_inherited{};

    /// Everything at or below this node, so a collapsed branch can still say what it holds.
    size_t local_count{};
    size_t inherited_count{};
};

struct entity_overrides
{
    entt::handle handle;
    std::string name;
    override_node root;
};

auto split_path(const std::string& path) -> std::vector<std::string>
{
    std::vector<std::string> segments;
    size_t start = 0;
    while(start <= path.size())
    {
        const auto separator = path.find('/', start);
        const auto end = separator == std::string::npos ? path.size() : separator;
        if(end > start)
        {
            segments.emplace_back(path.substr(start, end - start));
        }
        if(separator == std::string::npos)
        {
            break;
        }
        start = separator + 1;
    }
    return segments;
}

/// Which of the two authors an override came from. Inherited ones are stated by the prefab
/// that contains this instance, and it re-states them every time it is replayed - so they are
/// shown, and are not the user's to revert here.
auto build_override_tree(const prefab_component& data, entt::handle root_prefab_entity)
    -> std::map<hpp::uuid, entity_overrides>
{
    std::map<hpp::uuid, entity_overrides> by_entity;

    for(const auto& override_data : data.get_all_overrides())
    {
        const bool inherited = data.inherited_overrides.count(override_data) != 0u;

        auto& entry = by_entity[override_data.entity_uuid];
        if(!entry.handle && entry.name.empty())
        {
            entry.handle = scene::find_entity_by_prefab_uuid(root_prefab_entity, override_data.entity_uuid);
            entry.name = entry.handle ? entity_panel::get_entity_name(entry.handle) : "Entity Not Found";
        }

        const auto& display_path =
            override_data.pretty_component_path.empty() ? override_data.component_path : override_data.pretty_component_path;

        auto* node = &entry.root;
        node->local_count += inherited ? 0u : 1u;
        node->inherited_count += inherited ? 1u : 0u;

        for(const auto& segment : split_path(display_path))
        {
            node = &node->children[segment];
            node->local_count += inherited ? 0u : 1u;
            node->inherited_count += inherited ? 1u : 0u;
        }

        node->leaf = &override_data;
        node->leaf_is_inherited = inherited;
    }

    return by_entity;
}

/// Appended to a collapsed branch so it still says what it is holding.
auto describe_counts(const override_node& node) -> std::string
{
    if(node.inherited_count == 0u)
    {
        return {};
    }
    if(node.local_count == 0u)
    {
        return fmt::format(" ({} from prefab)", node.inherited_count);
    }
    return fmt::format(" ({} here, {} from prefab)", node.local_count, node.inherited_count);
}

} // anonymous namespace


auto inspector_prefab_component::inspect(rtti::context& ctx,
                                        entt::meta_any& var,
                                        const meta_any_proxy& var_proxy,
                                        const var_info& info,
                                        const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<prefab_component&>();
    inspect_result result{};

    auto root_prefab_entity = data.get_owner();
    // Show override information
    const auto& overrides = data.get_all_overrides();
    
    if(!overrides.empty())
    {
        const size_t inherited_total = data.inherited_overrides.size();
        const size_t local_total = overrides.size() - std::min(inherited_total, overrides.size());

        const std::string header_id =
            inherited_total > 0u
                ? fmt::format("Property Overrides: {} here, {} from prefab###Override Details",
                              local_total,
                              inherited_total)
                : fmt::format("Property Overrides: {}###Override Details", overrides.size());

        if(ImGui::CollapsingHeader(header_id.c_str()))
        {
            ImGui::Indent();

            const auto tree = build_override_tree(data, root_prefab_entity);

            // Collected while drawing and acted on afterwards, so the set being iterated is
            // not modified underneath.
            const prefab_property_override_data* to_revert{};

            const auto disabled_colour = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

            // The control at the end of a leaf row. Right-aligned, and drawn after a node that
            // spans the full row width - so the node has to allow it to overlap, or the row
            // takes the click and the button never sees it.
            const auto draw_leaf_control = [&](const override_node& node)
            {
                const auto& override_data = *node.leaf;
                const auto& style = ImGui::GetStyle();

                if(node.leaf_is_inherited)
                {
                    const char* label = "from prefab";
                    const float width = ImGui::CalcTextSize(label).x;

                    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                    ImGui::AlignedItem(1.0f,
                                       ImGui::GetContentRegionAvail().x - style.FramePadding.x,
                                       width,
                                       [&]()
                                       {
                                           // No revert offered: the prefab containing this
                                           // instance states this override and states it again
                                           // on every resync, so dropping it here would not
                                           // last past the next one.
                                           ImGui::TextColored(disabled_colour, "%s", label);
                                           ImGui::SetItemTooltipEx(
                                               "Stated by the prefab that contains this instance.\n"
                                               "Change it there, or override it here to take it over.");
                                       });
                    return;
                }

                const char* label = "Revert";
                const float width = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;

                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                ImGui::AlignedItem(1.0f,
                                   ImGui::GetContentRegionAvail().x - style.FramePadding.x,
                                   width,
                                   [&]()
                                   {
                                       ImGui::PushID(override_data.component_path.c_str());
                                       if(ImGui::SmallButton(label))
                                       {
                                           to_revert = &override_data;
                                       }
                                       ImGui::PopID();
                                   });
            };

            // Where the override actually points, on the row itself rather than on the control,
            // so it reads the same whether the row can be reverted or not.
            const auto set_leaf_tooltip = [&](const override_node& node, const entity_overrides& entity)
            {
                const auto& override_data = *node.leaf;
                ImGui::SetItemTooltipEx("Entity: %s\nUUID: %s\nComponent Path: %s",
                                        entity.name.c_str(),
                                        hpp::to_string(override_data.entity_uuid).c_str(),
                                        override_data.component_path.c_str());
            };

            const std::function<void(const std::string&, const override_node&, const entity_overrides&)> draw_node =
                [&](const std::string& label, const override_node& node, const entity_overrides& entity)
            {
                const bool is_leaf_only = node.children.empty();
                const std::string text = label + describe_counts(node);

                const bool all_inherited = node.local_count == 0u;
                if(all_inherited)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, disabled_colour);
                }

                if(is_leaf_only)
                {
                    ImGui::TreeNodeEx(text.c_str(),
                                      ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                          ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
                    if(all_inherited)
                    {
                        ImGui::PopStyleColor();
                    }

                    if(node.leaf != nullptr)
                    {
                        set_leaf_tooltip(node, entity);
                        draw_leaf_control(node);
                    }
                    return;
                }

                const bool open = ImGui::TreeNodeEx(text.c_str(),
                                                    ImGuiTreeNodeFlags_SpanAvailWidth |
                                                        ImGuiTreeNodeFlags_AllowOverlap);
                if(all_inherited)
                {
                    ImGui::PopStyleColor();
                }

                // A node can be overridden *and* have overridden children - "Position" as well
                // as "Position/X" - so its own row still carries the controls.
                if(node.leaf != nullptr)
                {
                    set_leaf_tooltip(node, entity);
                    draw_leaf_control(node);
                }

                if(open)
                {
                    for(const auto& [child_label, child] : node.children)
                    {
                        draw_node(child_label, child, entity);
                    }
                    ImGui::TreePop();
                }
            };

            for(const auto& [entity_uuid, entity] : tree)
            {
                ImGui::PushID(hpp::to_string(entity_uuid).c_str());

                const bool all_inherited = entity.root.local_count == 0u;
                if(all_inherited)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, disabled_colour);
                }

                const std::string entity_label = entity.name + describe_counts(entity.root);
                const bool open = ImGui::TreeNodeEx(entity_label.c_str(),
                                                    ImGuiTreeNodeFlags_DefaultOpen |
                                                        ImGuiTreeNodeFlags_SpanAvailWidth |
                                                        ImGuiTreeNodeFlags_AllowOverlap);
                if(all_inherited)
                {
                    ImGui::PopStyleColor();
                }

                if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && entity.handle)
                {
                    auto& em = ctx.get_cached<editing_manager>();
                    em.focus(entity.handle);
                }

                if(open)
                {
                    for(const auto& [child_label, child] : entity.root.children)
                    {
                        draw_node(child_label, child, entity);
                    }
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            if(to_revert != nullptr)
            {
                data.remove_override(to_revert->entity_uuid, to_revert->component_path);
                data.changed = true;
                result.changed = true;

                auto& em = ctx.get_cached<editing_manager>();
                em.sync_prefab_entity(ctx, root_prefab_entity, data.source);
            }

            ImGui::Unindent();
        }
        ImGui::Separator();
    }

    if(!data.removed_instances.empty())
    {
        std::string header_id =
            fmt::format("Removed Nested Instances: {}###Removed Instances", data.removed_instances.size());
        if(ImGui::CollapsingHeader(header_id.c_str()))
        {
            ImGui::Indent();

            hpp::uuid instance_to_restore;
            for(const auto& instance_id : data.removed_instances)
            {
                const auto id_str = hpp::to_string(instance_id);
                ImGui::BulletText("%s", id_str.c_str());
                ImGui::SetItemTooltipEx("A prefab instance nested in this one that was deleted here.\n"
                                        "Identified by its slot in the containing prefab rather than by\n"
                                        "its own prefab id, which every instance of that prefab shares.");
                ImGui::SameLine();

                ImGui::PushID(id_str.c_str());
                if(ImGui::SmallButton("Restore"))
                {
                    instance_to_restore = instance_id;
                }
                ImGui::PopID();
            }

            if(!instance_to_restore.is_nil())
            {
                data.removed_instances.erase(instance_to_restore);
                data.changed = true;
                result.changed = true;

                auto& em = ctx.get_cached<editing_manager>();
                em.sync_prefab_entity(ctx, root_prefab_entity, data.source);
            }

            ImGui::Unindent();
        }
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

    result |= inspect_var_properties(ctx, var, var_proxy, info, custom);

    if(result.changed)
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.sync_prefab_entity(ctx, root_prefab_entity, data.source);
    }
    
    return result;
}

} // namespace unravel
