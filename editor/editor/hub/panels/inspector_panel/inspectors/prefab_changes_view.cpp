#include "prefab_changes_view.h"
#include "imgui_widgets/tooltips.h"
#include "imgui_widgets/utils.h"
#include "inspectors.h"

#include <editor/imgui/integration/imgui_messagebox.h>

#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/authoring_root.h>
#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/entity_panel.h>

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <engine/meta/ecs/entity.hpp>

namespace unravel
{

auto is_nested_instance(entt::handle entity) -> bool
{
    const auto* trans_comp = entity.try_get<transform_component>();
    auto current = trans_comp != nullptr ? trans_comp->get_parent() : entt::handle{};
    while(current)
    {
        if(current.all_of<prefab_component>())
        {
            return true;
        }
        const auto* parent_trans = current.try_get<transform_component>();
        current = parent_trans != nullptr ? parent_trans->get_parent() : entt::handle{};
    }
    return false;
}

namespace
{

// The palette the hierarchy panel established: amber marks what was made *here*, dimmed marks
// what a prefab states and will state again. Reused so the two views read as one system.
const ImVec4 k_local_colour{1.0f, 0.78f, 0.35f, 1.0f};
const ImVec4 k_removed_colour{0.92f, 0.50f, 0.45f, 1.0f};

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

/// A removal recorded on one instance, attributed to whoever made it.
struct removal_row
{
    hpp::uuid id;
    bool is_instance{};
    bool inherited{};
};

/// An entity that exists under an instance without being part of any prefab file.
struct addition_row
{
    entt::handle handle;
    bool is_instance{};

    /// Added by the prefab that contains this instance rather than here; shown, not editable.
    bool inherited{};
};

/**
 * @brief Everything recorded on one prefab instance, ready to draw.
 *
 * The clicked entity's own instance first (when it is one), then every instance nested under
 * it at any depth, in hierarchy order - which is what makes the view answer "what changed from
 * here down" without the user visiting each nested root by hand.
 */
struct instance_changes
{
    entt::handle root;
    std::string title;
    std::string asset_name;
    bool is_self{};

    /// What is stated about this instance's content, by whom. Owned here so the override tree
    /// can point into it.
    statements_about_instance about;

    std::map<hpp::uuid, entity_overrides> overrides;
    std::vector<removal_row> removals;
    std::vector<addition_row> additions;

    size_t local_count{};
    size_t inherited_count{};
    size_t added_count{};
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

/// Which author an override came from. Inherited ones are stated by the documents containing
/// this instance, and they re-state them every time they are replayed - so they are shown, and
/// are not the user's to revert here. Local ones were stated in this scene.
auto build_override_tree(const statements_about_instance& about, entt::handle instance_root)
    -> std::map<hpp::uuid, entity_overrides>
{
    std::map<hpp::uuid, entity_overrides> by_entity;

    const auto add = [&by_entity, instance_root](const prefab_property_override_data& override_data, bool inherited)
    {
        auto& entry = by_entity[override_data.entity_uuid];
        if(!entry.handle && entry.name.empty())
        {
            entry.handle = scene::find_entity_by_prefab_uuid(instance_root, override_data.entity_uuid);
            entry.name = entry.handle ? entity_panel::get_entity_name(entry.handle) : "Entity Not Found";
        }

        const auto& display_path = override_data.pretty_component_path.empty()
                                       ? override_data.component_path
                                       : override_data.pretty_component_path;

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
    };

    for(const auto& override_data : about.local.overrides)
    {
        add(override_data, false);
    }
    for(const auto& override_data : about.stated.overrides)
    {
        add(override_data, true);
    }

    return by_entity;
}

auto asset_display_name(const asset_handle<prefab>& source) -> std::string
{
    if(!source.is_valid())
    {
        return "missing prefab";
    }
    return fs::path(source.id()).stem().string();
}

/// Fills one group from its instance's bookkeeping.
void build_group(instance_changes& group)
{
    const auto* prefab_comp = group.root.try_get<prefab_component>();
    if(prefab_comp == nullptr)
    {
        return;
    }

    group.asset_name = asset_display_name(prefab_comp->source);
    group.about = collect_statements_about(group.root);
    group.overrides = build_override_tree(group.about, group.root);

    group.inherited_count = group.about.stated.overrides.size();
    group.local_count = group.about.local.overrides.size();

    for(const auto& removed : group.about.local.removed_entities)
    {
        group.removals.push_back({removed.id, false, false});
        ++group.local_count;
    }
    for(const auto& removed : group.about.stated.removed_entities)
    {
        group.removals.push_back({removed.id, false, true});
        ++group.inherited_count;
    }
    for(const auto& removed : group.about.local.removed_instances)
    {
        group.removals.push_back({removed.id, true, false});
        ++group.local_count;
    }
    for(const auto& removed : group.about.stated.removed_instances)
    {
        group.removals.push_back({removed.id, true, true});
        ++group.inherited_count;
    }
}

void collect_groups(entt::handle entity, size_t group_index, std::vector<instance_changes>& groups)
{
    const auto* trans_comp = entity.try_get<transform_component>();
    if(trans_comp == nullptr)
    {
        return;
    }

    for(auto child : trans_comp->get_children())
    {
        if(const auto* child_prefab = child.try_get<prefab_component>())
        {
            // A nested instance is a group of its own. One with no instance id was added
            // where it stands, so it is also an addition to the group that contains it.
            if(child_prefab->instance_id.is_nil())
            {
                groups[group_index].additions.push_back({child, true, false});
                ++groups[group_index].added_count;
            }

            instance_changes nested;
            nested.root = child;
            nested.title = entity_panel::get_entity_name(child);
            build_group(nested);

            groups.push_back(std::move(nested));
            const size_t nested_index = groups.size() - 1;
            collect_groups(child, nested_index, groups);
            continue;
        }

        if(child.all_of<prefab_id_component>())
        {
            collect_groups(child, group_index, groups);
            continue;
        }

        // No prefab id and not an instance: added here, along with everything under it. Not
        // recursed into - the whole subtree is the addition, and one row says so.
        groups[group_index].additions.push_back({child, false, false});
        ++groups[group_index].added_count;
    }
}

/// Entities under an instance root whose id names a document other than the instance's own
/// asset: introduced by whatever contains it. Stops at nested instances, which are groups of
/// their own, and does not descend into an addition - the whole subtree is the addition.
void collect_outer_additions(entt::handle entity, const hpp::uuid& own_document, instance_changes& group)
{
    const auto* trans_comp = entity.try_get<transform_component>();
    if(trans_comp == nullptr)
    {
        return;
    }

    for(auto child : trans_comp->get_children())
    {
        if(child.all_of<prefab_component>())
        {
            continue;
        }

        if(const auto* id_comp = child.try_get<prefab_id_component>())
        {
            if(!id_comp->document.is_nil() && id_comp->document != own_document)
            {
                group.additions.push_back({child, false, true});
                continue;
            }
        }

        collect_outer_additions(child, own_document, group);
    }
}

/**
 * @brief The root (when it is an instance) plus every instance nested below it, each with its
 *        changes.
 *
 * An authoring root - the content of the prefab being edited - is not an instance and gets no
 * group; its entry is a placeholder that only collects direct additions, and is not drawn.
 */
auto collect_changes(entt::handle root) -> std::vector<instance_changes>
{
    std::vector<instance_changes> groups;

    instance_changes self;
    self.root = root;
    self.title = entity_panel::get_entity_name(root);
    self.is_self = true;
    build_group(self);
    groups.push_back(std::move(self));

    collect_groups(root, 0, groups);

    // Entities a *containing* document introduced under an instance - their ids name that
    // document rather than the instance's own asset. Part of the containing prefab's
    // definition, so shown as inherited additions rather than local ones.
    for(auto& group : groups)
    {
        const auto* prefab_comp = group.root.try_get<prefab_component>();
        if(prefab_comp == nullptr)
        {
            continue;
        }
        collect_outer_additions(group.root, prefab_comp->source.uid(), group);
    }

    return groups;
}

/// The root whose deep Revert All was confirmed in the message box. The box answers on a later
/// frame, from outside this function, so the answer is parked here and picked up by the next
/// draw of that same root - which is what keeps the revert inside the normal flow, where the
/// caller's sync and save see its result.
auto pending_revert_all_root() -> entt::uhandle&
{
    static entt::uhandle root{};
    return root;
}

/// What the user asked for while drawing, applied after the walk so nothing is mutated under
/// the iteration.
struct pending_ops
{
    entt::handle revert_override_owner{};
    prefab_property_override_data revert_override{};

    entt::handle restore_owner{};
    hpp::uuid restore_id{};
    bool restore_is_instance{};

    entt::handle revert_group{};
    bool revert_all{};

    std::vector<entt::handle> delete_added;
};

/**
 * @brief Puts one instance back to exactly what the prefabs say.
 *
 * The inherited halves are what the containing document states - and will state again on the
 * next replay - so they are what the sets collapse to. For a top-level instance, and for every
 * instance directly under an authoring root, they are empty and this is a full clear.
 */
void revert_local_changes(prefab_component& prefab_comp)
{
    prefab_comp.local.clear();
    prefab_comp.changed = true;
}

auto short_uuid(const hpp::uuid& id) -> std::string
{
    return hpp::to_string(id).substr(0, 8);
}

/// Right-aligns a small control at the end of the current row. The row item before it must
/// carry ImGuiTreeNodeFlags_AllowOverlap (or not be interactive), or it swallows the click.
void draw_row_control(float width, const std::function<void()>& draw)
{
    const auto& style = ImGui::GetStyle();
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::AlignedItem(1.0f, ImGui::GetContentRegionAvail().x - style.FramePadding.x, width, draw);
}

auto small_button_width(const char* label) -> float
{
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

void draw_override_rows(const instance_changes& group, pending_ops& ops, const ImVec4& disabled_colour)
{
    const auto set_leaf_tooltip = [&](const override_node& node, const entity_overrides& entity)
    {
        const auto& override_data = *node.leaf;
        ImGui::SetItemTooltipEx("Entity: %s\nUUID: %s\nComponent Path: %s",
                                entity.name.c_str(),
                                hpp::to_string(override_data.entity_uuid).c_str(),
                                override_data.component_path.c_str());
    };

    const auto draw_leaf_control = [&](const override_node& node)
    {
        const auto& override_data = *node.leaf;

        if(node.leaf_is_inherited)
        {
            const char* label = "from prefab";
            draw_row_control(ImGui::CalcTextSize(label).x,
                             [&]()
                             {
                                 // No revert: the prefab containing this instance states this
                                 // override again on every resync, so dropping it here would
                                 // not last past the next one.
                                 ImGui::TextColored(disabled_colour, "%s", label);
                                 ImGui::SetItemTooltipEx(
                                     "Stated by the prefab that contains this instance.\n"
                                     "Change it there, or override it here to take it over.");
                             });
            return;
        }

        draw_row_control(small_button_width("Revert"),
                         [&]()
                         {
                             ImGui::PushID(override_data.component_path.c_str());
                             if(ImGui::SmallButton("Revert"))
                             {
                                 ops.revert_override_owner = group.root;
                                 ops.revert_override = override_data;
                             }
                             ImGui::SetItemTooltipEx("Set this property back to what the prefab has.");
                             ImGui::PopID();
                         });
    };

    const std::function<void(const std::string&, const override_node&, const entity_overrides&)> draw_node =
        [&](const std::string& label, const override_node& node, const entity_overrides& entity)
    {
        const bool is_leaf_only = node.children.empty();
        const bool all_inherited = node.local_count == 0u;

        std::string text = label;
        if(node.inherited_count > 0u && node.local_count > 0u)
        {
            text += fmt::format(" ({} here, {} from prefab)", node.local_count, node.inherited_count);
        }

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

        const bool open =
            ImGui::TreeNodeEx(text.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
        if(all_inherited)
        {
            ImGui::PopStyleColor();
        }

        // A node can be overridden *and* have overridden children - "Position" as well as
        // "Position/X" - so its own row still carries the controls.
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

    for(const auto& [entity_uuid, entity] : group.overrides)
    {
        ImGui::PushID(hpp::to_string(entity_uuid).c_str());

        const bool all_inherited = entity.root.local_count == 0u;
        if(all_inherited)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, disabled_colour);
        }

        const std::string entity_label = ICON_MDI_PENCIL " " + entity.name;
        const bool open = ImGui::TreeNodeEx(entity_label.c_str(),
                                            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                ImGuiTreeNodeFlags_AllowOverlap);
        if(all_inherited)
        {
            ImGui::PopStyleColor();
        }

        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && entity.handle)
        {
            auto& ctx = engine::context();
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
}

void draw_removal_rows(const instance_changes& group, pending_ops& ops, const ImVec4& disabled_colour)
{
    for(const auto& removal : group.removals)
    {
        const auto id_str = hpp::to_string(removal.id);
        ImGui::PushID(id_str.c_str());

        const std::string label = fmt::format("{} Removed {}: {}",
                                              ICON_MDI_MINUS_CIRCLE_OUTLINE,
                                              removal.is_instance ? "nested instance" : "entity",
                                              short_uuid(removal.id));

        ImGui::PushStyleColor(ImGuiCol_Text, removal.inherited ? disabled_colour : k_removed_colour);
        ImGui::TreeNodeEx(label.c_str(),
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::PopStyleColor();

        if(removal.is_instance)
        {
            ImGui::SetItemTooltipEx("A prefab instance nested in this one, deleted %s.\n"
                                    "Identified by its slot in the containing prefab (%s).",
                                    removal.inherited ? "by the prefab that contains this instance" : "here",
                                    id_str.c_str());
        }
        else
        {
            ImGui::SetItemTooltipEx("An entity of this instance's prefab, deleted %s.\nPrefab id: %s",
                                    removal.inherited ? "by the prefab that contains this instance" : "here",
                                    id_str.c_str());
        }

        if(removal.inherited)
        {
            const char* tag = "from prefab";
            draw_row_control(ImGui::CalcTextSize(tag).x,
                             [&]() { ImGui::TextColored(disabled_colour, "%s", tag); });
        }
        else
        {
            draw_row_control(small_button_width("Restore"),
                             [&]()
                             {
                                 if(ImGui::SmallButton("Restore"))
                                 {
                                     ops.restore_owner = group.root;
                                     ops.restore_id = removal.id;
                                     ops.restore_is_instance = removal.is_instance;
                                 }
                                 ImGui::SetItemTooltipEx("Bring it back from the prefab.");
                             });
        }

        ImGui::PopID();
    }
}

void draw_addition_rows(const instance_changes& group, pending_ops& ops, const ImVec4& disabled_colour)
{
    for(const auto& addition : group.additions)
    {
        if(!addition.handle)
        {
            continue;
        }

        ImGui::PushID(static_cast<int>(addition.handle.entity()));

        const std::string label = fmt::format("{} Added: {}{}",
                                              ICON_MDI_PLUS_CIRCLE_OUTLINE,
                                              entity_panel::get_entity_name(addition.handle),
                                              addition.is_instance ? " (prefab instance)" : "");

        ImGui::PushStyleColor(ImGuiCol_Text, addition.inherited ? disabled_colour : k_local_colour);
        ImGui::TreeNodeEx(label.c_str(),
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::PopStyleColor();

        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            auto& ctx = engine::context();
            auto& em = ctx.get_cached<editing_manager>();
            em.focus(addition.handle);
        }

        if(addition.inherited)
        {
            ImGui::SetItemTooltipEx("Added by the prefab that contains this instance.\nRemove it there, not here.");
            const char* tag = "from prefab";
            draw_row_control(ImGui::CalcTextSize(tag).x,
                             [&]() { ImGui::TextColored(disabled_colour, "%s", tag); });
        }
        else
        {
            ImGui::SetItemTooltipEx("Added here; no prefab file contains it.\n"
                                    "It is never touched by a sync, and Revert All keeps it.");
            draw_row_control(small_button_width("Delete"),
                             [&]()
                             {
                                 if(ImGui::SmallButton("Delete"))
                                 {
                                     ops.delete_added.push_back(addition.handle);
                                 }
                                 ImGui::SetItemTooltipEx("Delete the added entity (undoable).");
                             });
        }

        ImGui::PopID();
    }
}

void draw_group(const instance_changes& group, pending_ops& ops, const ImVec4& disabled_colour)
{
    ImGui::PushID(static_cast<int>(group.root.entity()));

    std::string label;
    if(group.is_self)
    {
        label = fmt::format(ICON_MDI_CUBE " This instance ({})", group.asset_name);
    }
    else
    {
        label = fmt::format(ICON_MDI_CUBE " {} ({})", group.title, group.asset_name);
    }

    std::vector<std::string> parts;
    if(group.local_count > 0u)
    {
        parts.push_back(fmt::format("{} here", group.local_count));
    }
    if(group.inherited_count > 0u)
    {
        parts.push_back(fmt::format("{} from prefab", group.inherited_count));
    }
    if(group.added_count > 0u)
    {
        parts.push_back(fmt::format("{} added", group.added_count));
    }
    if(!parts.empty())
    {
        label += "  - ";
        for(size_t i = 0; i < parts.size(); ++i)
        {
            label += (i > 0 ? ", " : "") + parts[i];
        }
    }

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Framed;
    if(group.is_self || group.local_count > 0u || group.added_count > 0u)
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        if(!group.is_self && group.root)
        {
            auto& ctx = engine::context();
            auto& em = ctx.get_cached<editing_manager>();
            em.focus(group.root);
        }
        ImGui::SetItemTooltipEx("%s changes recorded on this instance.\n"
                                "\"here\" means made at this level; \"from prefab\" means stated by the\n"
                                "prefab that contains the instance and re-stated on every sync.",
                                group.is_self ? "The clicked instance's" : "A nested instance's");
    }

    if(group.local_count > 0u)
    {
        draw_row_control(small_button_width("Revert"),
                         [&]()
                         {
                             if(ImGui::SmallButton("Revert"))
                             {
                                 ops.revert_group = group.root;
                             }
                             ImGui::SetItemTooltipEx("Revert the %zu change(s) made here on this instance.\n"
                                                     "Changes stated by a containing prefab are kept.\n"
                                                     "Added entities are kept.",
                                                     group.local_count);
                         });
    }

    if(open)
    {
        draw_override_rows(group, ops, disabled_colour);
        draw_removal_rows(group, ops, disabled_colour);
        draw_addition_rows(group, ops, disabled_colour);
        ImGui::TreePop();
    }

    ImGui::PopID();
}

} // anonymous namespace

auto draw_prefab_changes(rtti::context& ctx, entt::handle root) -> inspect_result
{
    inspect_result result{};
    if(!root)
    {
        return result;
    }

    // An instance root has a group of its own. The prefab being edited is an instance too, but
    // its own content is never recorded as overrides (see authoring_root_tag), so it has no
    // group - its placeholder entry is never drawn - and the view reads as "this prefab".
    const bool root_is_instance = root.all_of<prefab_component>() && !is_authoring_root(root);
    const char* subject = root_is_instance ? "this instance" : "this prefab";

    auto groups = collect_changes(root);

    size_t total_local = 0;
    size_t total_inherited = 0;
    size_t total_added = 0;
    for(const auto& group : groups)
    {
        total_local += group.local_count;
        total_inherited += group.inherited_count;
        total_added += group.added_count;
    }

    const auto disabled_colour = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    pending_ops ops;

    if(auto confirmed = pending_revert_all_root().resolve(); confirmed)
    {
        if(confirmed == root)
        {
            ops.revert_all = true;
        }
        // Consumed, or stale: a different root is being drawn, so the user has moved on, and a
        // parked confirmation must not fire on some later visit to the root it was given for.
        pending_revert_all_root() = {};
    }

    if(total_local + total_inherited + total_added == 0u)
    {
        ImGui::TextColored(disabled_colour,
                           root_is_instance ? "No changes on this instance or anything nested in it."
                                            : "No changes to the instances nested in this prefab.");
        ImGui::Separator();
    }
    else
    {
        std::string header = "Changes: ";
        std::vector<std::string> parts;
        if(total_local > 0u)
        {
            parts.push_back(fmt::format("{} here", total_local));
        }
        if(total_inherited > 0u)
        {
            parts.push_back(fmt::format("{} from prefab", total_inherited));
        }
        if(total_added > 0u)
        {
            parts.push_back(fmt::format("{} added", total_added));
        }
        for(size_t i = 0; i < parts.size(); ++i)
        {
            header += (i > 0 ? ", " : "") + parts[i];
        }
        header += "###Changes";

        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if(ImGui::CollapsingHeader(header.c_str()))
        {
            ImGui::Indent();

            // Everything from the root down, one group per instance. Groups with nothing to
            // say are skipped rather than rendered empty, and an authoring root has no group.
            for(const auto& group : groups)
            {
                const bool empty = group.local_count + group.inherited_count + group.added_count == 0u;
                if(group.is_self && !root_is_instance)
                {
                    continue;
                }
                if(!group.is_self && empty)
                {
                    continue;
                }
                draw_group(group, ops, disabled_colour);
            }

            ImGui::Unindent();
        }
        ImGui::Separator();
    }

    // ------------------------------------------------------------------ revert all

    ImGui::BeginDisabled(total_local == 0u);
    if(ImGui::Button(ICON_MDI_UNDO_VARIANT " Revert All Changes", ImVec2(-1, ImGui::GetFrameHeight())))
    {
        const auto message = fmt::format("Revert {} change(s) on {} and everything nested in it?\n\n"
                                         "Entities added here are kept. This cannot be undone.",
                                         total_local,
                                         subject);
        ImBox::ShowConfirmation("Revert All Changes?",
                                message,
                                [root_uh = entt::make_uhandle(root)](ImBox::ModalResult answer)
                                {
                                    if(answer == ImBox::ModalResult::Ok)
                                    {
                                        pending_revert_all_root() = root_uh;
                                    }
                                },
                                ImBox::MessageType::Warning);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Set %s - and everything nested in it - back to what the prefabs say.\n"
                            "Entities added here are kept; delete those individually.",
                            subject);

    // ------------------------------------------------------------------ apply what was asked

    bool mutated = false;

    if(ops.revert_override_owner)
    {
        if(auto* owner_prefab = ops.revert_override_owner.try_get<prefab_component>())
        {
            owner_prefab->remove_override(ops.revert_override.entity_uuid, ops.revert_override.component_path);
            owner_prefab->changed = true;
            mutated = true;
        }
    }

    if(ops.restore_owner)
    {
        if(auto* owner_prefab = ops.restore_owner.try_get<prefab_component>())
        {
            if(ops.restore_is_instance)
            {
                owner_prefab->local.restore_instance({}, ops.restore_id);
            }
            else
            {
                owner_prefab->local.restore_entity({}, ops.restore_id);
            }
            owner_prefab->changed = true;
            mutated = true;
        }
    }

    if(ops.revert_group)
    {
        if(auto* owner_prefab = ops.revert_group.try_get<prefab_component>())
        {
            revert_local_changes(*owner_prefab);
            mutated = true;
        }
    }

    if(ops.revert_all)
    {
        for(const auto& group : groups)
        {
            if(auto* owner_prefab = group.root.try_get<prefab_component>())
            {
                revert_local_changes(*owner_prefab);
            }
        }
        mutated = true;
    }

    if(!ops.delete_added.empty())
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.push_undo_stack_enabled(true);
        em.queue_action("Delete Entities", std::make_shared<delete_entities_action_t>(ops.delete_added));
        em.pop_undo_stack_enabled();
    }

    if(mutated)
    {
        result.changed = true;
        result.edit_finished = true;
    }

    return result;
}

} // namespace unravel
