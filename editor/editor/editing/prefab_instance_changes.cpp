#include "prefab_instance_changes.h"

#include <editor/editing/authoring_root.h>

#include <engine/assets/impl/asset_writer.h>
#include <engine/ecs/components/transform_component.h>

#include <set>

namespace unravel
{

namespace
{

auto asset_display_name(const asset_handle<prefab>& source) -> std::string
{
    if(!source.is_valid())
    {
        return "missing prefab";
    }
    return fs::path(source.id()).stem().string();
}

/// Fills one entry from its instance's bookkeeping.
void build_changes(prefab_instance_changes& changes)
{
    const auto* prefab_comp = changes.root.try_get<prefab_component>();
    if(prefab_comp == nullptr)
    {
        return;
    }
    changes.asset_name = asset_display_name(prefab_comp->source);
    changes.about = collect_statements_about(changes.root);
    changes.inherited_count = changes.about.stated.overrides.size();
    changes.local_count = changes.about.local.overrides.size();
    const auto add_removals = [&changes](const std::set<prefab_statement_target>& targets, bool is_instance, bool inherited)
    {
        for(const auto& removed : targets)
        {
            changes.removals.push_back({removed.id, is_instance, inherited});
            ++(inherited ? changes.inherited_count : changes.local_count);
        }
    };
    add_removals(changes.about.local.removed_entities, false, false);
    add_removals(changes.about.stated.removed_entities, false, true);
    add_removals(changes.about.local.removed_instances, true, false);
    add_removals(changes.about.stated.removed_instances, true, true);
}

void collect_nested_changes(entt::handle entity, size_t owner_index, std::vector<prefab_instance_changes>& out)
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
            // A nested instance is an entry of its own. One with no instance id was added
            // where it stands, so it is also an addition to the instance that contains it.
            if(child_prefab->instance_id.is_nil())
            {
                out[owner_index].additions.push_back({child, true, false});
                ++out[owner_index].added_count;
            }
            prefab_instance_changes nested;
            nested.root = child;
            build_changes(nested);
            out.push_back(std::move(nested));
            collect_nested_changes(child, out.size() - 1, out);
            continue;
        }
        if(child.all_of<prefab_id_component>())
        {
            collect_nested_changes(child, owner_index, out);
            continue;
        }
        // No prefab id and not an instance: added here, along with everything under it. Not
        // recursed into - the whole subtree is the addition.
        out[owner_index].additions.push_back({child, false, false});
        ++out[owner_index].added_count;
    }
}

/// Entities under an instance root whose id names a document other than the instance's own
/// asset: introduced by whatever contains it. Stops at nested instances, which are entries of
/// their own, and does not descend into an addition - the whole subtree is the addition.
void collect_outer_additions(entt::handle entity,
                             const hpp::uuid& own_document,
                             const std::set<hpp::uuid>& authoring_documents,
                             prefab_instance_changes& changes)
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
                // Added by the document being edited here - the authoring root above - it is
                // the author's own addition, not something inherited.
                const bool inherited = authoring_documents.count(id_comp->document) == 0u;
                changes.additions.push_back({child, false, inherited});
                continue;
            }
        }
        collect_outer_additions(child, own_document, authoring_documents, changes);
    }
}

/// The asset uids of the authoring roots at or above `root` - the documents being edited here.
auto authoring_documents_above(entt::handle root) -> std::set<hpp::uuid>
{
    std::set<hpp::uuid> documents;
    auto current = root;
    while(current)
    {
        if(is_authoring_root(current))
        {
            if(const auto* prefab_comp = current.try_get<prefab_component>())
            {
                documents.insert(prefab_comp->source.uid());
            }
        }
        const auto* trans_comp = current.try_get<transform_component>();
        current = trans_comp != nullptr ? trans_comp->get_parent() : entt::handle{};
    }
    return documents;
}

} // namespace

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

auto collect_prefab_changes(entt::handle root) -> std::vector<prefab_instance_changes>
{
    std::vector<prefab_instance_changes> out;
    prefab_instance_changes self;
    self.root = root;
    self.is_self = true;
    build_changes(self);
    out.push_back(std::move(self));
    collect_nested_changes(root, 0, out);
    // Entities a *containing* document introduced under an instance - their ids name that
    // document rather than the instance's own asset. Part of the containing prefab's
    // definition, so listed as inherited additions rather than local ones.
    const auto authoring_documents = authoring_documents_above(root);
    for(auto& changes : out)
    {
        if(const auto* prefab_comp = changes.root.try_get<prefab_component>())
        {
            collect_outer_additions(changes.root, prefab_comp->source.uid(), authoring_documents, changes);
        }
    }
    return out;
}

void revert_local_changes(prefab_component& prefab_comp)
{
    // The inherited halves are what the containing document states - and will state again on
    // the next replay - so they are what the lists collapse to. For a top-level instance, and
    // for every instance directly under an authoring root, they are empty and this is a full
    // clear.
    prefab_comp.local.clear();
    prefab_comp.changed = true;
}

void apply_instance_to_prefab(entt::handle root)
{
    auto* prefab_comp = root ? root.try_get<prefab_component>() : nullptr;
    if(prefab_comp == nullptr)
    {
        return;
    }
    prefab_comp->changed = false;
    auto prefab_path = fs::resolve_protocol(prefab_comp->source.id());
    asset_writer::atomic_save_to_file(prefab_path.string(), root);
    prefab_statements kept;
    if(is_nested_instance(root))
    {
        const auto* root_id = root.try_get<prefab_id_component>();
        for(const auto& override_data : prefab_comp->local.overrides)
        {
            const bool is_root = root_id != nullptr && override_data.instance_path.empty() &&
                                 override_data.entity_uuid == root_id->id;
            const bool is_placement =
                override_data.component_path.rfind("transform_component/local_transform/position", 0) == 0 ||
                override_data.component_path.rfind("transform_component/local_transform/rotation", 0) == 0;
            if(is_root && is_placement)
            {
                kept.overrides.insert(override_data);
            }
        }
    }
    prefab_comp->from_document = fold_document_statements(root);
    prefab_comp->local = std::move(kept);
    clear_local_statements_below(root);
}

void unlink_from_prefab(entt::handle entity)
{
    entity.remove<prefab_component, prefab_id_component>();
}

} // namespace unravel
