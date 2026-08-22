#include "entity.hpp"

#include <chrono>
#include <serialization/archives/yaml.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <filesystem/file_istream.h>
#include <serialization/serialization.h>
#include "components/all_components.h"
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/ui/ecs/systems/ui_system.h>
#include <engine/meta/core/common/basetypes.hpp>

#include "entt/entity/fwd.hpp"
#include "logging/logging.h"
#include "reflection/reflection.h"
#include "uuid/uuid.h"

#include <algorithm>
#include <hpp/utility.hpp>
#include <sstream>
#include <utility>

namespace unravel
{

auto const_handle_cast(entt::const_handle chandle) -> entt::handle
{
    entt::handle handle(*const_cast<entt::handle::registry_type*>(chandle.registry()), chandle.entity());
    return handle;
}


auto as_span(const std::vector<entity_data<entt::handle>>& entities) -> hpp::span<const entt::handle>
{
    // pointer to the first entity_data
    auto* base = entities.data();

    // entity_data is just a tag type around entt::handle, so they must have the same size
    static_assert(sizeof(entity_data<entt::handle>) == sizeof(entt::handle), "entity_data<entt::handle> and entt::handle must have the same size");
    static_assert(alignof(entity_data<entt::handle>) == alignof(entt::handle), "entity_data<entt::handle> and entt::handle must have the same alignment");
    // compute the address of the first entt::handle
    auto* first_handle = reinterpret_cast<const entt::handle*>(base);

    // build a span over all of them
    hpp::span<const entt::handle> handles{first_handle, entities.size()};
    return handles;
}

thread_local load_context* load_ctx_ptr{};
thread_local save_context* save_ctx_ptr{};
thread_local post_load_callbacks* post_load_callbacks_ptr{};

auto push_load_context(entt::registry& registry) -> bool
{
    if(load_ctx_ptr)
    {
        return false;
    }
    load_ctx_ptr = new load_context();
    load_ctx_ptr->reg = &registry;
    return true;
}

void pop_load_context(bool push_result)
{
    if(push_result && load_ctx_ptr)
    {
        auto entities = std::move(load_ctx_ptr->entities);
        delete load_ctx_ptr;
        load_ctx_ptr = {};


        if(!entities.empty())
        {   
            auto callbacks = get_post_load_callbacks();
            if(callbacks && callbacks->callback)
            {
                callbacks->callback(as_span(entities));
            } 
        }

    }
}

auto get_load_context() -> load_context&
{
    assert(load_ctx_ptr);
    return *load_ctx_ptr;
}

void push_on_load_callbacks(const post_load_callbacks& callbacks)
{
    if(post_load_callbacks_ptr)
    {
        return;
    }
    post_load_callbacks_ptr = new post_load_callbacks(callbacks);
}
void pop_on_load_callbacks()
{
    if(post_load_callbacks_ptr)
    {
        delete post_load_callbacks_ptr;
        post_load_callbacks_ptr = {};
    }
}

auto get_post_load_callbacks() -> const post_load_callbacks*
{
    return post_load_callbacks_ptr;
}

auto push_save_context() -> bool
{
    if(save_ctx_ptr)
    {
        return false;
    }
    save_ctx_ptr = new save_context();

    return true;
}

void pop_save_context(bool push_result)
{
    if(push_result && save_ctx_ptr)
    {
        delete save_ctx_ptr;
        save_ctx_ptr = {};
    }
}


auto get_save_context() -> save_context&
{
    assert(save_ctx_ptr);
    return *save_ctx_ptr;
}

auto try_get_save_context() -> save_context*
{
    return save_ctx_ptr;
}

auto try_get_load_context() -> load_context*
{
    return load_ctx_ptr;
}

auto instance_path_between(entt::handle outer_root, entt::handle inner_root, std::vector<hpp::uuid>& out) -> bool
{
    out.clear();
    if(!outer_root || !inner_root)
    {
        return false;
    }
    if(outer_root == inner_root)
    {
        return true;
    }

    std::vector<hpp::uuid> reversed;
    auto current = inner_root;
    while(current && current != outer_root)
    {
        if(const auto* prefab_comp = current.try_get<prefab_component>())
        {
            if(prefab_comp->instance_id.is_nil())
            {
                return false;
            }
            reversed.push_back(prefab_comp->instance_id);
        }
        const auto* trans_comp = current.try_get<transform_component>();
        current = trans_comp != nullptr ? trans_comp->get_parent() : entt::handle{};
    }
    if(current != outer_root)
    {
        return false;
    }

    out.assign(reversed.rbegin(), reversed.rend());
    return true;
}

namespace
{
/// Calls fn(ancestor_root, chain) for every instance root above `root`, nearest first, with
/// the slot chain from that ancestor down to root. Stops at the first ancestor that cannot
/// address root - an unnamed instance in between - because nothing above it can either.
template<typename Fn>
void for_each_addressing_ancestor(entt::handle root, Fn&& fn)
{
    const auto* trans_comp = root.try_get<transform_component>();
    auto current = trans_comp != nullptr ? trans_comp->get_parent() : entt::handle{};
    std::vector<hpp::uuid> chain;
    while(current)
    {
        if(current.all_of<prefab_component>())
        {
            if(!instance_path_between(current, root, chain))
            {
                return;
            }
            fn(current, chain);
        }
        const auto* parent_trans = current.try_get<transform_component>();
        current = parent_trans != nullptr ? parent_trans->get_parent() : entt::handle{};
    }
}

/// Calls fn(nested_root, chain) for every named instance root nested under `node` at any
/// depth, with the slot chain from the walk's root to it. Does not descend into an unnamed
/// one: nothing below it can be addressed from above.
template<typename Fn>
void for_each_addressable_nested_root(entt::handle node, std::vector<hpp::uuid>& chain, Fn&& fn)
{
    const auto* trans_comp = node.try_get<transform_component>();
    if(trans_comp == nullptr)
    {
        return;
    }
    for(auto child : trans_comp->get_children())
    {
        if(const auto* nested_prefab = child.try_get<prefab_component>())
        {
            if(nested_prefab->instance_id.is_nil())
            {
                continue;
            }
            chain.push_back(nested_prefab->instance_id);
            fn(child, chain);
            for_each_addressable_nested_root(child, chain, fn);
            chain.pop_back();
            continue;
        }
        for_each_addressable_nested_root(child, chain, fn);
    }
}
} // namespace

auto collect_statements_about(entt::handle root) -> statements_about_instance
{
    statements_about_instance out;
    if(!root)
    {
        return out;
    }
    if(const auto* own = root.try_get<prefab_component>())
    {
        out.local = own->local.direct();
    }
    for_each_addressing_ancestor(root,
                                 [&out](entt::handle ancestor, const std::vector<hpp::uuid>& chain)
                                 {
                                     const auto& ancestor_prefab = ancestor.get<prefab_component>();
                                     out.stated.merge(ancestor_prefab.from_document.at(chain));
                                     out.local.merge(ancestor_prefab.local.at(chain));
                                 });
    return out;
}

auto collect_replay_statements(entt::handle root) -> replay_statements
{
    replay_statements out;
    if(!root)
    {
        return out;
    }
    if(const auto* own = root.try_get<prefab_component>())
    {
        out.local.merge(own->local);
    }
    for_each_addressing_ancestor(root,
                                 [&out](entt::handle ancestor, const std::vector<hpp::uuid>& chain)
                                 {
                                     const auto& ancestor_prefab = ancestor.get<prefab_component>();
                                     out.stated.merge(ancestor_prefab.from_document.rebased(chain));
                                     out.local.merge(ancestor_prefab.local.rebased(chain));
                                 });
    std::vector<hpp::uuid> chain;
    for_each_addressable_nested_root(root,
                                     chain,
                                     [&out](entt::handle nested, const std::vector<hpp::uuid>& path)
                                     {
                                         out.local.merge(nested.get<prefab_component>().local.prefixed(path));
                                     });
    return out;
}

auto fold_document_statements(entt::handle root) -> prefab_statements
{
    prefab_statements out;
    if(!root)
    {
        return out;
    }
    if(const auto* own = root.try_get<prefab_component>())
    {
        out.merge(own->from_document);
        // What the root's own list states about nested content - an authoring root's adopted
        // list. Its entries about the root's direct content are content, not statements.
        out.merge(own->local.nested_only());
    }
    std::vector<hpp::uuid> chain;
    for_each_addressable_nested_root(root,
                                     chain,
                                     [&out](entt::handle nested, const std::vector<hpp::uuid>& path)
                                     {
                                         out.merge(nested.get<prefab_component>().local.prefixed(path));
                                     });
    return out;
}

void clear_local_statements_below(entt::handle root)
{
    if(!root)
    {
        return;
    }
    std::vector<hpp::uuid> chain;
    for_each_addressable_nested_root(root,
                                     chain,
                                     [](entt::handle nested, const std::vector<hpp::uuid>&)
                                     {
                                         nested.get<prefab_component>().local.clear();
                                     });
}

void re_home_document_statements(entt::handle root)
{
    if(!root)
    {
        return;
    }
    auto* own = root.try_get<prefab_component>();
    if(own == nullptr)
    {
        return;
    }
    prefab_statements pool = own->from_document;
    pool.merge(own->local.nested_only());
    std::vector<hpp::uuid> chain;
    for_each_addressable_nested_root(root,
                                     chain,
                                     [&pool](entt::handle nested, const std::vector<hpp::uuid>& path)
                                     {
                                         nested.get<prefab_component>().local.merge(pool.at(path));
                                     });
    own->from_document.clear();
    own->local = own->local.direct();
}

/**
 * @brief Every removal stated about content under `root`, relative to root: here, by the
 *        documents above, and by root's own document as last replayed.
 */
auto collect_replay_removals(entt::handle root) -> prefab_statements
{
    prefab_statements out;
    if(!root)
    {
        return out;
    }
    const auto replay = collect_replay_statements(root);
    out.merge(replay.stated);
    out.merge(replay.local);
    if(const auto* own = root.try_get<prefab_component>())
    {
        out.merge(own->from_document);
    }
    return out;
}

/**
 * @brief Converts override and removal state from records written before statements lived
 *        with their author.
 *
 * Such a record merged every author's overrides on the nested root it sat on, with memos saying
 * which half was whose. For a prefab document (`document_root` set) the document's own half of
 * every nested root's record becomes the document's statements, re-rooted to the document; the
 * other halves belong to the documents inside and are refreshed by their own replays. For a
 * scene the local half stays on the root that carried it and each document's half goes to
 * that document's instance above it, re-rooted there.
 */
void convert_legacy_override_state(entt::registry& registry, entt::handle document_root, const hpp::uuid& document_uid)
{
    auto& load_ctx = get_load_context();
    const bool is_prefab_document = static_cast<bool>(document_root);
    for(auto& [entity_id, legacy] : load_ctx.legacy_overrides)
    {
        entt::handle entity{registry, entity_id};
        if(!entity || !entity.all_of<prefab_component>())
        {
            continue;
        }
        auto& prefab_comp = entity.get<prefab_component>();

        if(is_prefab_document)
        {
            if(entity == document_root)
            {
                continue;
            }
            std::vector<hpp::uuid> chain;
            if(!instance_path_between(document_root, entity, chain))
            {
                continue;
            }
            const auto own_it = legacy.stated_overrides.find(document_uid);
            const auto& own = own_it != legacy.stated_overrides.end() ? own_it->second : legacy.property_overrides;
            for(auto entry : own)
            {
                entry.instance_path = chain;
                load_ctx.document_statements.overrides.insert(std::move(entry));
            }
            // Directly nested, the removal memo equalled the set and all of it was the
            // document's; deeper, the memo was the middle document's own and is refreshed by
            // its replay.
            const bool depth_one = chain.size() == 1;
            for(const auto& removed : legacy.removed_entities)
            {
                if(depth_one || legacy.inherited_removed_entities.count(removed) == 0u)
                {
                    load_ctx.document_statements.removed_entities.insert({chain, removed});
                }
            }
            for(const auto& removed : legacy.removed_instances)
            {
                if(depth_one || legacy.inherited_removed_instances.count(removed) == 0u)
                {
                    load_ctx.document_statements.removed_instances.insert({chain, removed});
                }
            }
            continue;
        }

        std::set<prefab_property_override_data> stated_union;
        for(const auto& [document, stated] : legacy.stated_overrides)
        {
            stated_union.insert(stated.begin(), stated.end());
        }
        for(const auto& entry : legacy.property_overrides)
        {
            if(stated_union.count(entry) == 0u)
            {
                prefab_comp.local.overrides.insert(entry);
            }
        }
        for(const auto& removed : legacy.removed_entities)
        {
            if(legacy.inherited_removed_entities.count(removed) == 0u)
            {
                prefab_comp.local.removed_entities.insert({{}, removed});
            }
        }
        for(const auto& removed : legacy.removed_instances)
        {
            if(legacy.inherited_removed_instances.count(removed) == 0u)
            {
                prefab_comp.local.removed_instances.insert({{}, removed});
            }
        }
        for_each_addressing_ancestor(
            entity,
            [&legacy](entt::handle ancestor, const std::vector<hpp::uuid>& chain)
            {
                auto& ancestor_prefab = ancestor.get<prefab_component>();
                const auto stated_it = legacy.stated_overrides.find(ancestor_prefab.source.uid());
                if(stated_it != legacy.stated_overrides.end())
                {
                    for(auto entry : stated_it->second)
                    {
                        entry.instance_path = chain;
                        ancestor_prefab.from_document.overrides.insert(std::move(entry));
                    }
                }
                // The removal memo was the nearest container's statement.
                if(chain.size() == 1)
                {
                    for(const auto& removed : legacy.inherited_removed_entities)
                    {
                        ancestor_prefab.from_document.removed_entities.insert({chain, removed});
                    }
                    for(const auto& removed : legacy.inherited_removed_instances)
                    {
                        ancestor_prefab.from_document.removed_instances.insert({chain, removed});
                    }
                }
            });
    }
    load_ctx.legacy_overrides.clear();
}
scoped_instance_frame::scoped_instance_frame()
{
    get_load_context().instance_stack.emplace_back();
}

scoped_instance_frame::~scoped_instance_frame()
{
    auto& stack = get_load_context().instance_stack;
    if(!stack.empty())
    {
        stack.pop_back();
    }
}

/**
 * @brief Maps a nested instance's entities to a null handle, so records addressed at them
 *        are skipped rather than applied.
 *
 * Same mechanism `removed_entities` uses: load_entity_from_prefab_uid reports a hit with an
 * empty handle, and LOAD(entity_data) then reads no components for that record.
 */
void shadow_nested_instance(entt::handle obj,
                            load_context::instance_frame& frame,
                            bool is_nested_root,
                            const std::vector<hpp::uuid>& parent_path = {})
{
    if(!obj)
    {
        return;
    }

    if(auto* id_comp = obj.try_get<prefab_id_component>())
    {
        // The handle is kept, unlike a removed_entities entry: records still resolve to an
        // empty handle and skip, but cleanup needs something to destroy if the asset has
        // dropped this nested instance.
        auto& mapping = frame.mapping_by_prefab_uid[id_comp->id];
        mapping.handle = obj;
        mapping.shadowed = true;

        // Counted, not flagged: a duplicated nested instance carries the original's prefab
        // uid, so one entry can stand for several live entities.
        ++mapping.shadow_count;

        if(is_nested_root)
        {
            frame.shadowed_roots.emplace_back(id_comp->id, obj);

            const auto* prefab_comp = obj.try_get<prefab_component>();
            if(prefab_comp != nullptr && prefab_comp->instance_id.is_nil())
            {
                frame.adoption_candidates.push_back({parent_path, id_comp->id, obj});
            }
        }
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            shadow_nested_instance(child, frame, false, parent_path);
        }
    }
}

void record_nested_instance(entt::handle obj,
                            load_context::instance_frame& frame,
                            std::vector<hpp::uuid> path);

/**
 * @brief Records one nested instance's entities so the containing document can address them.
 *
 * Stops at a deeper instance and gives that one its own scope, because its entities are keyed
 * in yet another asset's uid space.
 */
void collect_scope_contents(entt::handle obj,
                            load_context::instance_frame& frame,
                            load_context::instance_frame::nested_scope& scope,
                            const std::vector<hpp::uuid>& path,
                            bool is_scope_root)
{
    if(!obj)
    {
        return;
    }

    if(!is_scope_root && obj.all_of<prefab_component>())
    {
        record_nested_instance(obj, frame, path);
        return;
    }

    if(auto* id_comp = obj.try_get<prefab_id_component>())
    {
        id_comp->generate_if_nil();
        scope.by_prefab_uid[id_comp->id] = obj;
    }

    if(is_scope_root)
    {
        // Entities removed from this instance - here, or by a document above the one being
        // loaded. A null entry: the document's record for it is skipped, not resurrected.
        for(const auto& removed : frame.removals.removed_entities)
        {
            if(removed.instance_path == path)
            {
                scope.by_prefab_uid[removed.id] = {};
            }
        }
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            collect_scope_contents(child, frame, scope, path, false);
        }
    }
}

/**
 * @brief Gives a live nested instance a scope the containing document can address.
 *
 * Falls back to shadowing when the instance has no id - a document written before instance
 * ids existed cannot name it, so the old behaviour of skipping its records wholesale is the
 * only safe reading.
 */
void record_nested_instance(entt::handle obj,
                            load_context::instance_frame& frame,
                            std::vector<hpp::uuid> path)
{
    if(!obj)
    {
        return;
    }

    const auto* prefab_comp = obj.try_get<prefab_component>();
    if(prefab_comp == nullptr || prefab_comp->instance_id.is_nil())
    {
        // Added where it stands, or produced by a document written before instance ids. Both
        // mean no document can name it, so its records are skipped wholesale as before.
        shadow_nested_instance(obj, frame, true, path);
        return;
    }

    path.push_back(prefab_comp->instance_id);

    auto& scope = frame.nested_scopes[path];
    scope.root = obj;

    if(auto* id_comp = obj.try_get<prefab_id_component>())
    {
        // Kept so cleanup knows this was live before the load, alongside the unnamed ones.
        frame.shadowed_roots.emplace_back(id_comp->id, obj);
    }

    for(const auto& removed : frame.removals.removed_instances)
    {
        if(removed.instance_path != path)
        {
            continue;
        }
        auto removed_path = path;
        removed_path.push_back(removed.id);
        frame.removed_instance_paths.insert(std::move(removed_path));
    }

    collect_scope_contents(obj, frame, scope, path, true);

    // Also shadowed by prefab uid. A document written before instance ids says nothing about
    // which instance it means, and without this its records would miss every named instance
    // and create a second copy of each.
    shadow_nested_instance(obj, frame, true, path);
}

void add_to_uid_mapping_impl(entt::handle obj, load_context::instance_frame& frame, bool is_instance_root)
{
    if(!obj)
    {
        return;
    }

    // A descendant that is itself an instance root belongs to a different asset, keyed in
    // that asset's uid space. It gets a scope of its own so the document's records for it
    // land on the right instance rather than on whichever entity happens to share a uid.
    if(!is_instance_root && obj.all_of<prefab_component>())
    {
        record_nested_instance(obj, frame, {});
        return;
    }

    if(auto* id_comp = obj.try_get<prefab_id_component>())
    {
        id_comp->generate_if_nil();
        frame.mapping_by_prefab_uid[id_comp->id].handle = obj;
    }

    if(is_instance_root)
    {
        // Removals about this instance's own direct content. A null mapping entry: the
        // document's record for it is skipped, not resurrected.
        for(const auto& removed : frame.removals.removed_entities)
        {
            if(removed.instance_path.empty())
            {
                frame.mapping_by_prefab_uid[removed.id].handle = {};
            }
        }
        for(const auto& removed : frame.removals.removed_instances)
        {
            if(removed.instance_path.empty())
            {
                frame.removed_instance_paths.insert({removed.id});
            }
        }
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            add_to_uid_mapping_impl(child, frame, false);
        }
    }
}

/**
 * @brief Records the existing instance subtree so its records can be matched to it.
 *
 * Fills the innermost instance frame, so an entity resolves within the instance it belongs
 * to rather than against every instance in the scene.
 */
void add_to_uid_mapping(entt::handle& obj)
{
    if(!obj)
    {
        return;
    }

    auto* frame = get_load_context().current_instance();
    if(frame == nullptr)
    {
        // Nothing to match against: entities will be created rather than resolved.
        return;
    }

    frame->root = obj;
    frame->removals = collect_replay_removals(obj);
    add_to_uid_mapping_impl(obj, *frame, true);
}

/// Disposes of instance entities the records never claimed - i.e. entities deleted from
/// the prefab since this instance was made. Operates on the innermost frame only.
void cleanup_uid_mapping()
{
    // Unconsumed load stubs. They were never visible to gameplay, so they must not
    // produce pre-destroy notifications on the way out.
    scene::scoped_destroy_suppression no_pre_destroy;

    auto& load_ctx = get_load_context();
    auto* frame = load_ctx.current_instance();
    if(frame == nullptr)
    {
        return;
    }

    for(auto& [uid, mapping] : frame->mapping_by_prefab_uid)
    {
        // Shadowed entries are handled below: an unconsumed one is not necessarily gone
        // from the asset, it may be something the user added here.
        if(mapping.consumed() || mapping.shadowed || !mapping.handle)
        {
            continue;
        }

        // Unmentioned is a deletion only for this document's own content. An entity another
        // document introduced here - an outer one adding under this instance - carries that
        // document's name, and one the user made carries none; neither is this document's to
        // remove.
        const auto* id_comp = mapping.handle.try_get<prefab_id_component>();
        if(id_comp != nullptr && id_comp->document != load_ctx.document_uid)
        {
            continue;
        }

        scene::destroy_entity(mapping.handle);
    }

    // Named by the document: decided exactly, one instance at a time. No record addressed it
    // means the author removed that instance, and nothing else can mean it.
    //
    // Ordered by path, so an outer instance goes before anything nested inside it and the
    // inner handles are already invalid by the time they come up.
    //
    // Unconsumed is not enough on its own. A document written before instance ids says
    // nothing about which instance it means, so its records go through the prefab uid instead
    // and leave every scope unconsumed - and destroying those would take every nested
    // instance in the scene with it.
    //
    // What separates the two is whether the document addressed the instance *at all*. A
    // document that still contains it addresses it one way or the other; one that dropped it
    // carries no record for it under either name.
    for(auto& [path, scope] : frame->nested_scopes)
    {
        if(scope.consumed || !scope.root)
        {
            continue;
        }

        const auto* id_comp = scope.root.try_get<prefab_id_component>();
        if(id_comp != nullptr)
        {
            const auto it = frame->mapping_by_prefab_uid.find(id_comp->id);
            if(it != frame->mapping_by_prefab_uid.end() && it->second.consumed())
            {
                continue;
            }
        }

        // Unmentioned is only a deletion for a slot this document placed. An instance an outer
        // document added here is named by that document, one the user added is not named at
        // all - and neither is this document's to remove. The slot says whose it is.
        const auto* nested_prefab = scope.root.try_get<prefab_component>();
        if(nested_prefab == nullptr || nested_prefab->instance_document != load_ctx.document_uid)
        {
            continue;
        }

        scene::destroy_entity(scope.root);
    }

}

/**
 * @brief Attributes ids from a file written before they named their document.
 *
 * Such a file says "entity x", "slot y", and nothing about whose. The only reading its writer
 * could have meant: what sits directly under a root is that root's document's; what sits
 * under a nested instance is that instance's asset's - unless the instance listed it as added
 * by the document containing it, the one case those files did record. A named slot was named
 * by the document containing the instance. Touches nil documents only, so an entity already
 * attributed - by an earlier pass, or a newer scene - is left as it is.
 *
 * Run once per load that saw such an id, while the load context still holds the lists.
 */
void qualify_legacy_prefab_ids(entt::handle obj,
                               hpp::uuid owner_document,
                               hpp::uuid added_document,
                               const std::set<hpp::uuid>* added_list,
                               bool is_walk_root)
{
    if(!obj)
    {
        return;
    }

    auto& load_ctx = get_load_context();

    auto* prefab_comp = obj.try_get<prefab_component>();
    if(prefab_comp != nullptr)
    {
        // The root being loaded over is placed by the scene, not by anything in this document.
        if(!is_walk_root && !prefab_comp->instance_id.is_nil() && prefab_comp->instance_document.is_nil())
        {
            prefab_comp->instance_document = owner_document;
        }

        added_document = owner_document;
        owner_document = prefab_comp->source.uid();

        const auto listed = load_ctx.legacy_foreign_entities.find(obj.entity());
        added_list = listed != load_ctx.legacy_foreign_entities.end() ? &listed->second : nullptr;
    }

    if(auto* id_comp = obj.try_get<prefab_id_component>(); id_comp != nullptr && id_comp->document.is_nil())
    {
        const bool added_here = prefab_comp == nullptr && added_list != nullptr && added_list->count(id_comp->id) != 0u;
        id_comp->document = added_here ? added_document : owner_document;
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            qualify_legacy_prefab_ids(child, owner_document, added_document, added_list, false);
        }
    }
}

namespace
{
/// Defined further down, next to the other nesting walkers.
void collect_nested_instance_roots(entt::handle obj, std::vector<entt::handle>& out, bool is_subtree_root);
void reconcile_cloned_slots(entt::handle obj, std::vector<hpp::uuid> instance_chain);
} // namespace

/**
 * @brief Settles the identities a prefab document needs, before any of it is written.
 *
 * Two of them, and both have to happen up front rather than as each entity is reached: an
 * entity's record is written after its parent's, so anything recorded on the parent while
 * walking the children lands in the file a version late.
 *
 * - Every nested instance gets a slot if it has none, named after this document. That is the
 *   moment a slot in this file comes into existence, including for instances added or cloned
 *   in since the last write. Deliberately not done on load: an id the file does not carry
 *   would differ between instances of it, and every instance has to agree with the document
 *   about which slot is which.
 *
 * - Every other entity gets a prefab id if it has none, and its document settled: whatever a
 *   nested instance between here and the save root supplied keeps that asset's name;
 *   everything else - new, the user's, or carried in from a document outside this one - is
 *   this document's content now, which is what writing it into this file means. The name is
 *   what lets a nested asset's sync leave this document's additions inside it alone.
 */
void prepare_prefab_identity(entt::handle obj,
                             entt::handle enclosing_instance,
                             bool is_subtree_root,
                             std::vector<hpp::uuid> document_chain)
{
    if(!obj)
    {
        return;
    }

    auto* save_ctx = try_get_save_context();
    const hpp::uuid document_uid = save_ctx != nullptr ? save_ctx->document_uid : hpp::uuid{};

    const auto settle_entity_identity = [&](entt::handle entity)
    {
        auto& id_comp = entity.get_or_emplace<prefab_id_component>();
        id_comp.generate_if_nil();
        const bool supplied_by_nested =
            std::find(document_chain.begin(), document_chain.end(), id_comp.document) != document_chain.end();
        if(!supplied_by_nested && !document_uid.is_nil())
        {
            id_comp.document = document_uid;
        }
    };

    if(is_subtree_root)
    {
        // The document's own root, whether a plain entity or the instance being re-saved as
        // its own document.
        settle_entity_identity(obj);
    }
    else if(auto* prefab_comp = obj.try_get<prefab_component>())
    {
        // A slot is named by the document that placed it. Directly under the save root that
        // is always this document. Inside another instance it is this document only for an
        // instance it added there; an unnamed one that instance's own asset supplied - from a
        // file written before slots existed - is the asset's to name, and naming it from here
        // would have two documents disagreeing about its id. placed_by is what still tells
        // those two apart.
        const bool placed_here = !enclosing_instance || prefab_comp->placed_by == instance_placement::other;
        if(prefab_comp->instance_id.is_nil() && placed_here)
        {
            prefab_comp->instance_id = generate_uuid();
            prefab_comp->instance_document = document_uid;
        }

        enclosing_instance = obj;
        document_chain.push_back(prefab_comp->source.uid());
    }
    else
    {
        settle_entity_identity(obj);
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            prepare_prefab_identity(child, enclosing_instance, false, document_chain);
        }
    }
}

auto is_parent(entt::const_handle potential_parent, entt::const_handle child) -> bool
{
    if(!potential_parent)
    {
        return false;
    }
    // Traverse up the hierarchy from `child`
    while(true)
    {
        // Access the transform component once per entity
        const auto* transform = child.try_get<transform_component>();
        if(!transform)
        {
            return false; // Reached the root without finding `potential_parent`
        }
        auto parent = transform->get_parent();
        if(!parent)
        {
            return false;
        }

        if(parent == potential_parent)
        {
            return true; // Found the parent relationship
        }

        child = parent; // Move up the hierarchy
    }
}
auto find_root(entt::const_handle e) -> entt::const_handle
{
    // Loop to find the root entity
    while(true)
    {
        // Access the `transform_component` once
        const auto* transform = e.try_get<transform_component>();
        if(!transform || !transform->get_parent())
        {
            break; // If no parent, we are at the root
        }
        e = transform->get_parent(); // Move to the parent entity
    }
    return e; // Root entity
}

auto are_related(entt::const_handle lhs, entt::const_handle rhs) -> bool
{
    return find_root(lhs) == find_root(rhs);
}

enum entity_flags
{
    none,
    resolve_with_existing,
    resolve_with_loaded,
};

auto push_entity_path(entt::const_handle obj) -> bool
{
    auto ctx = serialization::get_path_context();
    if(ctx)
    {
        if(auto id = obj.try_get<prefab_id_component>())
        {
            ctx->push_segment(id->id.to_string());
            return true;
        }
    }
    return false;
}

void pop_entity_path(bool pushed)
{
    auto ctx = serialization::get_path_context();
    if(pushed && ctx)
    {
        ctx->pop_segment();
    }
}

} // namespace unravel

using namespace unravel;

namespace ser20
{

template<typename Archive>
void save_entity_id(Archive& ar, const entt::const_handle& obj)
{
    entt::handle::entity_type id = obj.valid() ? obj.entity() : entt::null;
    try_save(ar, ser20::make_nvp("id", id));
}

template<typename Archive>
void save_entity_prefab_uid(Archive& ar, const entt::const_handle& obj)
{
    if(obj)
    {
        auto& id_comp = const_handle_cast(obj).get_or_emplace<prefab_id_component>();
        if(id_comp.id.is_nil())
        {
            // Reached only for an entity the identity pass did not settle; the pass runs over
            // everything under the save root before anything is written.
            id_comp.id = generate_uuid();
            id_comp.document = get_save_context().document_uid;
        }

        try_save(ar, ser20::make_nvp("prefab_uid", id_comp.id));
    }
    else
    {
        try_save(ar, ser20::make_nvp("prefab_uid", hpp::uuid{}));
    }
}

/**
 * @brief Writes which nested instance of the document being saved this entity is.
 *
 * Only nested instance roots have one, so the key is omitted everywhere else rather than
 * writing a nil - a prefab's records are read a great many times and most of them are not
 * instances. An absent key reads back as nil, which is also what a document written before
 * instance ids existed says.
 */
template<typename Archive>
void save_entity_instance_uid(Archive& ar, const entt::const_handle& obj)
{
    if(!obj)
    {
        return;
    }

    const auto* prefab_comp = obj.try_get<prefab_component>();
    if(prefab_comp == nullptr || prefab_comp->instance_id.is_nil())
    {
        return;
    }

    try_save(ar, ser20::make_nvp("instance_uid", prefab_comp->instance_id));
}

/**
 * @brief Writes which nested instance, if any, this entity lives inside.
 *
 * A chain rather than a single id, because at depth the id alone is ambiguous: a prefab
 * containing two instances of B contains two instances of whatever B nests, and those carry
 * the same id. The chain runs outermost-first and stops at the entity being saved, which is
 * the document's own root and belongs to no instance.
 *
 * Absent - the common case, and every entity of a document that nests nothing.
 */
template<typename Archive>
void save_entity_instance_path(Archive& ar, const entt::const_handle& obj)
{
    if(!obj)
    {
        return;
    }

    const auto& save_source = get_save_context().save_source;

    std::vector<hpp::uuid> path;
    const auto* trans_comp = obj.try_get<transform_component>();
    auto current = trans_comp != nullptr ? trans_comp->get_parent() : entt::handle{};

    while(current && current != save_source)
    {
        if(const auto* current_prefab = current.try_get<prefab_component>())
        {
            if(current_prefab->instance_id.is_nil())
            {
                // An unnamed instance somewhere up the chain makes every id below it
                // ambiguous, so the whole path is unusable. Written as absent, which reads
                // back as the pre-instance-id behaviour.
                return;
            }
            path.push_back(current_prefab->instance_id);
        }

        const auto* parent_trans = current.try_get<transform_component>();
        current = parent_trans != nullptr ? parent_trans->get_parent() : entt::handle{};
    }

    if(path.empty())
    {
        return;
    }

    std::reverse(path.begin(), path.end());
    try_save(ar, ser20::make_nvp("instance_path", path));
}

template<typename Archive>
void save_entity_uuid(Archive& ar, const entt::const_handle& obj)
{
    if(obj)
    {
        auto& id_comp = const_handle_cast(obj).get_or_emplace<id_component>();
        id_comp.generate_if_nil();
        try_save(ar, ser20::make_nvp("uid", id_comp.id));
    }
    else
    {
        try_save(ar, ser20::make_nvp("uid", hpp::uuid{}));
    }
}

template<typename Archive>
void save_entity(Archive& ar, const entt::const_handle& obj, entity_flags flags)
{
    auto& save_ctx = get_save_context();
    if(save_ctx.is_saving_to_prefab())
    {
        save_entity_prefab_uid(ar, obj);
        save_entity_instance_uid(ar, obj);
        save_entity_instance_path(ar, obj);
    }
    if(!save_ctx.is_cloning())
    {
        save_entity_uuid(ar, obj);
    }
    else
    {
        save_entity_id(ar, obj);
    }
}

template<typename Archive>
auto load_entity_from_id(Archive& ar, entt::handle& obj, entity_flags flags) -> bool
{
    entt::handle::entity_type id{};
    bool valid = try_load(ar, ser20::make_nvp("id", id));

    valid &= id != entt::null && id != entt::handle::entity_type(0);
    if(valid)
    {
        auto& load_ctx = get_load_context();
        auto it = load_ctx.mapping_by_eid.find(id);
        if(it != load_ctx.mapping_by_eid.end())
        {
            obj = it->second;
            // APPLOG_TRACE("found in cache entity from id: {}", uint32_t(id));
        }
        else if(obj)
        {
            load_ctx.mapping_by_eid[id] = obj;
            // APPLOG_TRACE("added to cache entity from id: {}", uint32_t(id));
        }
        else
        {
            if(flags == entity_flags::resolve_with_existing)
            {
                entt::handle check_entity(*load_ctx.reg, id);
                if(check_entity)
                {
                    obj = check_entity;
                    load_ctx.mapping_by_eid[id] = obj;
                    // APPLOG_TRACE("added to cache entity from id: {}", uint32_t(id));
                }
                else
                {
                    obj = {};
                }
            }
            else
            {
                obj = entt::handle(*load_ctx.reg, load_ctx.reg->create());
                load_ctx.mapping_by_eid[id] = obj;
                // APPLOG_TRACE("created and added to cache entity from id: {}", uint32_t(id));
            }
        }
    }


    return valid;
}

/**
 * @brief Whether a record belongs to a nested instance that was deleted here.
 *
 * Every prefix counts: deleting an instance deletes what was nested inside it, and the
 * document still carries records for all of it.
 */
auto is_removed_instance_path(const load_context::instance_frame& frame,
                              const std::vector<hpp::uuid>& path) -> bool
{
    if(frame.removed_instance_paths.empty() || path.empty())
    {
        return false;
    }

    std::vector<hpp::uuid> prefix;
    prefix.reserve(path.size());
    for(const auto& segment : path)
    {
        prefix.push_back(segment);
        if(frame.removed_instance_paths.count(prefix) != 0u)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Claims an unnamed live instance for an id the document knows.
 *
 * The two sides disagree whenever a prefab is re-saved by a build that has instance ids while
 * instances of it created earlier do not - the common shape of the transition, and the reverse
 * of it. Both mean the same slot, so the live one is labelled and matched. Without this the
 * document's record finds nothing, creates a second instance, and the nesting doubles on every
 * load.
 */
auto adopt_unnamed_instance(load_context::instance_frame& frame,
                            const std::vector<hpp::uuid>& scope_path,
                            const hpp::uuid& prefab_uid)
    -> std::map<std::vector<hpp::uuid>, load_context::instance_frame::nested_scope>::iterator
{
    // Only for an instance that predates instance ids, which is a whole-instance state: if
    // anything here is named, this one was produced by a document that names them, and an
    // unnamed nested instance alongside those is one added here - a clone, most often. Adopting
    // that would hand the user's copy to the document and replace its contents.
    const std::vector<hpp::uuid> parent_path(scope_path.begin(), std::prev(scope_path.end()));

    for(auto& candidate : frame.adoption_candidates)
    {
        if(!candidate.handle || candidate.prefab_uid != prefab_uid || candidate.parent_path != parent_path)
        {
            continue;
        }

        auto* prefab_comp = candidate.handle.try_get<prefab_component>();
        if(prefab_comp == nullptr)
        {
            continue;
        }

        // Only what this document placed may be claimed for one of its slots. A clone, a
        // hand-placed instance or one an outer document added here is not its to take - the
        // two are indistinguishable by anything else when both are unnamed.
        if(prefab_comp->placed_by == instance_placement::other)
        {
            continue;
        }

        prefab_comp->instance_id = scope_path.back();

        auto& scope = frame.nested_scopes[scope_path];
        scope.root = candidate.handle;
        collect_scope_contents(candidate.handle, frame, scope, scope_path, true);

        // Claimed once. A second record naming another instance of the same prefab has to
        // find a different one, or both ids would land on the same entity.
        candidate.handle = {};

        return frame.nested_scopes.find(scope_path);
    }

    return frame.nested_scopes.end();
}

/**
 * @brief Resolves an entity addressed by prefab uid alone, without an instance to scope it.
 *
 * The path for a document's own entities, and the fallback for one written before nested
 * instances could name themselves.
 */
auto resolve_plain_prefab_uid(load_context& load_ctx,
                              load_context::prefab_uid_mapping_t& mapping,
                              entt::handle& obj) -> bool
{
    // Marked consumed either way. For a shadowed entry that is the whole point: it records
    // that the asset still mentions this nested instance, which is what distinguishes one the
    // author deleted from one the user added here.
    // Counted only for records. A parent's children list resolves every nested root too, and
    // counting those would make the asset look like it still contains an instance it dropped.
    if(load_ctx.resolving_record)
    {
        ++mapping.consumed_count;
    }

    if(!mapping.shadowed)
    {
        obj = mapping.handle;
        return true;
    }

    // More records addressed at this uid than there are live entities carrying it: the asset
    // gained a nested instance, almost always by the author duplicating one, which is exactly
    // the case the uid cannot distinguish. There is nothing left to protect, so refuse the
    // match and let the caller fall through to the uuid and entity-id paths, which create it
    // the same way a fresh instantiate would.
    //
    // The cursor advances on records only, so the links inside a record - its parent, its
    // children - see the same answer the record itself got. That is what keeps the new
    // subtree hanging together instead of being reparented to the original.
    if(mapping.shadow_cursor >= mapping.shadow_count)
    {
        if(load_ctx.resolving_record)
        {
            ++mapping.surplus_records;
            return false;
        }

        // A link, with every live instance of this uid already claimed by a record. Falling
        // through would reach the entity-id path and *create* an entity - one no record will
        // ever fill, because the records that would have are the ones that claimed the
        // slots. Its parent link then points at something with no transform.
        //
        // Unless a record really did fall through, in which case the entity it created is
        // the one this link means, and the entity-id path finds it.
        if(mapping.surplus_records == 0)
        {
            obj = mapping.handle;
            return true;
        }

        return false;
    }

    if(load_ctx.resolving_record)
    {
        ++mapping.shadow_cursor;
    }

    obj = {};
    return true;
}

template<typename Archive>
auto load_entity_from_prefab_uid(Archive& ar, entt::handle& obj, entity_flags flags) -> bool
{
    hpp::uuid uid;
    try_load(ar, ser20::make_nvp("prefab_uid", uid));


    // Resolved against the innermost instance only. A prefab uid identifies an entity
    // within one prefab asset, so it is meaningful relative to the instance being loaded
    // and nothing wider.
    auto& load_ctx = get_load_context();
    auto* frame = load_ctx.current_instance();
    if(frame == nullptr)
    {
        return false;
    }

    // Nothing nested here, so nothing can be scoped to an instance: the two lookups below
    // would miss on every record. Deleted nested instances count as nested state even though
    // nothing of them is live - their records still carry instance ids, and skipping the
    // instance-id reads here would skip the removed-path check with them, recreating an
    // instance whose only remaining trace is the removal entry.
    if(frame->nested_scopes.empty() && frame->shadowed_roots.empty() && frame->removed_instance_paths.empty())
    {
        auto plain_it = frame->mapping_by_prefab_uid.find(uid);
        if(plain_it == frame->mapping_by_prefab_uid.end())
        {
            return false;
        }
        return resolve_plain_prefab_uid(load_ctx, plain_it->second, obj);
    }

    // A nested instance names itself, so it can be matched exactly - which prefab uids
    // cannot do, since a duplicate or a copy carries the same one.
    hpp::uuid instance_uid;
    try_load(ar, ser20::make_nvp("instance_uid", instance_uid));

    // And an entity inside one says which, so the document can reach into a specific
    // instance instead of every instance of that prefab at once.
    std::vector<hpp::uuid> instance_path;
    try_load(ar, ser20::make_nvp("instance_path", instance_path));

    if(load_ctx.resolving_record)
    {
        load_ctx.record_instance_path_depth = instance_path.size();
    }

    if(!instance_uid.is_nil())
    {
        auto scope_path = instance_path;
        scope_path.push_back(instance_uid);

        if(is_removed_instance_path(*frame, scope_path))
        {
            // Deleted here. A hit with an empty handle, so the record is skipped instead of
            // creating the instance again.
            obj = {};
            return true;
        }

        auto scope_it = frame->nested_scopes.find(scope_path);
        if(scope_it == frame->nested_scopes.end())
        {
            scope_it = adopt_unnamed_instance(*frame, scope_path, uid);
        }

        if(scope_it == frame->nested_scopes.end())
        {
            // The document has a nested instance this one does not: the author added it,
            // most often by duplicating. Refuse the match so the caller falls through to the
            // paths that create it.
            return false;
        }

        if(load_ctx.resolving_record)
        {
            scope_it->second.consumed = true;
            load_ctx.record_matched_scope = true;
        }

        obj = scope_it->second.root;

        // Filtered against its own authoring, like its contents: what the document says
        // about this instance is its override set and its link, not the placement or name
        // the instance has been given here. Its prefab_component is let through regardless -
        // it is that override set, and it loads before anything it has to filter.
        load_ctx.pending_nested_owner = scope_it->second.root;
        return true;
    }

    if(!instance_path.empty())
    {
        if(is_removed_instance_path(*frame, instance_path))
        {
            obj = {};
            return true;
        }

        auto scope_it = frame->nested_scopes.find(instance_path);
        if(scope_it == frame->nested_scopes.end())
        {
            // Content of an instance that is not here yet - it arrives with the instance.
            return false;
        }

        auto entity_it = scope_it->second.by_prefab_uid.find(uid);
        if(entity_it == scope_it->second.by_prefab_uid.end())
        {
            // Not among the instance's entities - but it may be an *unnamed* instance nested in
            // it (one its own asset has not named yet), which is shadowed by prefab uid rather
            // than scoped. Resolving through the shadow skips the record, as it should: that
            // instance is refreshed by its own sync, not by this document's snapshot of it.
            auto shadow_it = frame->mapping_by_prefab_uid.find(uid);
            if(shadow_it != frame->mapping_by_prefab_uid.end() && shadow_it->second.shadowed)
            {
                return resolve_plain_prefab_uid(load_ctx, shadow_it->second, obj);
            }

            // The document knows an entity inside this instance that the instance does not:
            // one added under it. Created like any other addition.
            return false;
        }

        // May be null, for an entity the instance removed - a hit with an empty handle, so
        // the record is skipped rather than bringing it back.
        obj = entity_it->second;

        // Which instance's authoring this record belongs to, so the filter can let through
        // what that instance overrides and hold back the rest of the snapshot.
        load_ctx.pending_nested_owner = scope_it->second.root;
        return true;
    }

    auto it = frame->mapping_by_prefab_uid.find(uid);
    if(it != frame->mapping_by_prefab_uid.end())
    {
        return resolve_plain_prefab_uid(load_ctx, it->second, obj);
    }

    return false;
}

template<typename Archive>
auto load_entity_from_uuid(Archive& ar, entt::handle& obj, entity_flags flags) -> bool
{
    hpp::uuid uuid;
    if(!try_load(ar, ser20::make_nvp("uid", uuid)))
    {
        return false;
    }
    if(uuid.is_nil())
    {
        return false;
    }
    auto& load_ctx = get_load_context();
    auto it = load_ctx.mapping_by_uid.find(uuid);
    if(it != load_ctx.mapping_by_uid.end())
    {
        obj = it->second;
    }
    else if(obj)
    {
        load_ctx.mapping_by_uid[uuid] = obj;
    }
    else
    {
        if(flags == entity_flags::resolve_with_existing)
        {
            auto view = load_ctx.reg->view<id_component>();
            for(auto e : view)
            {
                if(view.get<id_component>(e).id == uuid)
                {
                    obj = entt::handle(*load_ctx.reg, e);
                    load_ctx.mapping_by_uid[uuid] = obj;
                    return true;
                }
            }
            obj = {};
            return false;
        }
        obj = entt::handle(*load_ctx.reg, load_ctx.reg->create());
        load_ctx.mapping_by_uid[uuid] = obj;
    }
    return true;
}

template<typename Archive>
void load_entity(Archive& ar, entt::handle& obj, entity_flags flags)
{
    bool valid = false;
    auto& load_ctx = get_load_context();
    if(load_ctx.is_updating_prefab())
    {
        valid = load_entity_from_prefab_uid(ar, obj, flags);
    }
    if(!valid && !load_ctx.is_cloning())
    {
        valid = load_entity_from_uuid(ar, obj, flags);
    }
    if(!valid)
    {
        valid = load_entity_from_id(ar, obj, flags);
    }
    if(!valid)
    {
        obj = {};
    }
}

template<typename Component>
auto should_save_component(const entt::const_handle& obj) -> bool
{
    if constexpr(std::is_same_v<Component, prefab_component>)
    {
        auto& save_ctx = get_save_context();

        // Dropped for the entity this file *defines*. Its prefab_component says it is an
        // instance of some other asset, and baking that into the asset being written would
        // declare this file a variant of that one - a different feature, and not what a
        // "save as prefab" means.
        //
        // Kept for every descendant. A prefab instance nested inside the one being saved is
        // a link to another asset, and dropping it is what flattened nested prefabs into
        // loose entities: the inner asset's edits stopped reaching them, permanently and
        // silently (audit B4).
        if(save_ctx.is_saving_to_prefab() && obj == save_ctx.save_source)
        {
            return false;
        }
    }
    else if constexpr(std::is_same_v<Component, prefab_id_component>)
    {
        // if we are cloning
        // we need to generate a new id for the entity
        auto& save_ctx = get_save_context();
        if(save_ctx.is_cloning())
        {
            if(save_ctx.get_clone_mode() != clone_mode_t::cloning_prefab_instance)
            {
                return false;
            }
        } 
    }
    return true;
}

/**
 * @brief The serialization keys for one component type, derived once.
 *
 * entt::get_name looks the name up in a std::map<std::string, meta_any> keyed by
 * const char*, so each call built a temporary std::string for the lookup and returned a
 * copy - and both the save and load paths asked for it once per component type per
 * entity, then concatenated "has_" onto the result. With 32 serializable types that was
 * ~100 allocations per entity before any data moved.
 *
 * Held by reference-returning statics rather than computed inline: NameValuePair stores
 * name.c_str() as a bare pointer, so a stable string is also safer than the temporary
 * these call sites used to build.
 *
 * Initialised on first use, which is the first save or load - long after the REFLECT
 * blocks have run at static-init time, so the reflected name is available rather than
 * entt::get_name's mangled fallback.
 */
template<typename Component>
struct component_keys
{
    static auto get() -> const component_keys&
    {
        static const component_keys instance{};
        return instance;
    }

    std::string name;
    std::string has_name;

private:
    component_keys() : name(entt::get_name(entt::resolve<Component>())), has_name("has_" + name)
    {
    }
};

template<typename Component>
auto should_load_component(const entt::handle& obj) -> bool
{
    if constexpr(std::is_same_v<Component, id_component>)
    {
        // Instance entity UIDs are per-instance (implicit override). When syncing /
        // loading into an existing prefab hierarchy, never overwrite them from the
        // prefab asset. New entities still get a UID via generate_if_nil.
        auto& load_ctx = get_load_context();
        if(load_ctx.is_updating_prefab())
        {
            auto& id_comp = obj.get_or_emplace<id_component>();
            id_comp.generate_if_nil();
            return false;
        }
    }
    return true;
}

SAVE(entt::const_handle)
{
    save_entity(ar, obj, entity_flags::none);
}
SAVE_INSTANTIATE(entt::const_handle, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(entt::const_handle, ser20::oarchive_binary_t);

LOAD(entt::handle)
{

    load_entity(ar, obj, entity_flags::none);
}

LOAD_INSTANTIATE(entt::handle, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(entt::handle, ser20::iarchive_binary_t);

SAVE(const_entity_handle_link)
{
    // Saving entity links is a little more complex than just entities
    // The rule is as follows.
    // If we are saving as single entity hierarch :
    // If the entity link is not part of it :
    // -> if we are saving to prefab, break the link
    // -> if we are duplicating resolve the link on load with exsisting scene.
    entity_flags flags = entity_flags::resolve_with_loaded;
    entt::const_handle to_save = obj.handle;

    auto& save_ctx = get_save_context();

    bool is_saving_single = save_ctx.save_source.valid();
    if(is_saving_single)
    {
        // is the entity a child of the hierarchy that we are saving?
        bool save_source_is_parent = is_parent(save_ctx.save_source, obj.handle);

        // if it is an external entity
        if(!save_source_is_parent)
        {
            if(save_ctx.is_saving_to_prefab())
            {
                // when saving prefabs, external entities
                // should not be saved
                to_save = {};
            }
            else
            {
                // when saving entities for duplication purpose, external entities
                // should not be resolved from the existing scene
                flags = entity_flags::resolve_with_existing;
            }
        }
        else
        {
            if(!save_ctx.is_saving_to_prefab())
            {
                flags = entity_flags::resolve_with_existing;
            }
        }
    }

    try_save(ar, ser20::make_nvp("flags", flags));
    save_entity(ar, to_save, flags);
}
SAVE_INSTANTIATE(const_entity_handle_link, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(const_entity_handle_link, ser20::oarchive_binary_t);

LOAD(entity_handle_link)
{
    entity_flags flags{};
    try_load(ar, ser20::make_nvp("flags", flags));

    load_entity(ar, obj.handle, flags);
}

LOAD_INSTANTIATE(entity_handle_link, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(entity_handle_link, ser20::iarchive_binary_t);



SAVE(entity_components<entt::const_handle>)
{
    hpp::for_each_tuple_type<unravel::all_serializeable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, unravel::all_serializeable_components>;
           
            if(!should_save_component<ctype>(obj.entity))
            {
                return;
            }
           
            auto component = obj.entity.try_get<ctype>();
            if(!component)
            {
                return;
            }

            const auto& keys = component_keys<ctype>::get();

            try_save(ar, ser20::make_nvp(keys.has_name, true));
            try_save(ar, ser20::make_nvp(keys.name, *component));
        });
}
SAVE_INSTANTIATE(entity_components<entt::const_handle>, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(entity_components<entt::const_handle>, ser20::oarchive_binary_t);

LOAD(entity_components<entt::handle>)
{
    hpp::for_each_tuple_type<unravel::all_serializeable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, unravel::all_serializeable_components>;

            if(!should_load_component<ctype>(obj.entity))
            {
                return;
            }


            const auto& keys = component_keys<ctype>::get();

            bool has_component = false;
            serialize_check(keys.has_name,
                            [&]() -> bool
                            {
                                return try_serialize_direct(ar, ser20::make_nvp(keys.has_name, has_component));
                            });

            if(has_component)
            {
                auto& component = obj.entity.get_or_emplace<ctype>();

                serialize_check(keys.name,
                                [&]() -> bool
                                {
                                    return try_serialize_direct(ar, ser20::make_nvp(keys.name, component));
                                });

                emit_on_load<ctype>(*obj.entity.registry(), obj.entity.entity());

            }
            
            if constexpr(std::is_same_v<ctype, id_component>)
            {
                auto& comp = obj.entity.get_or_emplace<ctype>();
                comp.generate_if_nil();
            }
            if constexpr(std::is_same_v<ctype, tag_component>)
            {
                auto& comp = obj.entity.get_or_emplace<ctype>();
                (void)comp;
            }
            if constexpr(std::is_same_v<ctype, layer_component>)
            {
                auto& comp = obj.entity.get_or_emplace<ctype>();
                (void)comp;
            }
            

    
        });


    // if we are cloning
    // we need to generate a new id for the entity
    auto& load_ctx = get_load_context();
    if(load_ctx.is_cloning())
    {
        if(load_ctx.get_clone_mode() != clone_mode_t::cloning_prefab_instance)
        {
            // and are not the root of the prefab,
            obj.entity.remove<prefab_id_component>();
        }

        auto id_comp = obj.entity.try_get<id_component>();
        if(id_comp)
        {
            id_comp->regenerate_id();
        }

    }
}
LOAD_INSTANTIATE(entity_components<entt::handle>, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(entity_components<entt::handle>, ser20::iarchive_binary_t);

SAVE(entity_data<entt::const_handle>)
{
    SAVE_FUNCTION_NAME(ar, obj.components.entity);
    try_save(ar, ser20::make_nvp("components", obj.components));
}
SAVE_INSTANTIATE(entity_data<entt::const_handle>, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(entity_data<entt::const_handle>, ser20::oarchive_binary_t);

LOAD(entity_data<entt::handle>)
{
    entt::handle e;
    {
        // Marks this resolution as a record rather than a link; see
        // load_context::resolving_record.
        auto& load_ctx = get_load_context();
        load_ctx.resolving_record = true;
        load_ctx.pending_nested_owner = {};
        load_ctx.record_instance_path_depth = 0;
        load_ctx.record_matched_scope = false;
        LOAD_FUNCTION_NAME(ar, e);
        load_ctx.resolving_record = false;
    }

    if(e)
    {
        auto& load_ctx = get_load_context();
        const auto owner = load_ctx.pending_nested_owner;
        load_ctx.pending_nested_owner = {};
        load_ctx.current_nested_owner = owner;

        bool pushed = push_entity_path(e);
        obj.components.entity = e;
        try_load(ar, ser20::make_nvp("components", obj.components));
        pop_entity_path(pushed);

        load_ctx.current_nested_owner = {};

        // Who placed a nested instance this record created or matched. A record directly in
        // the document - no instance path - is the document speaking about its own slot, so
        // the instance's container placed it. A record reaching *into* another instance that
        // had to create what it describes was an outer document adding something there; that
        // instance's own document knows nothing about it. A deeper record that matched leaves
        // the verdict alone - the matched instance's own container has already given it.
        //
        // Never the root of the instance being loaded over: that one's placement belongs to
        // whoever put the instance in the scene, not to the document.
        if(const auto* frame = load_ctx.current_instance(); frame != nullptr && e != frame->root)
        {
            if(auto* nested_prefab = e.try_get<prefab_component>())
            {
                if(load_ctx.record_instance_path_depth == 0)
                {
                    nested_prefab->placed_by = instance_placement::container;
                }
                else if(!load_ctx.record_matched_scope)
                {
                    nested_prefab->placed_by = instance_placement::other;
                }
            }
        }
    }
}
LOAD_INSTANTIATE(entity_data<entt::handle>, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(entity_data<entt::handle>, ser20::iarchive_binary_t);

} // namespace ser20

namespace unravel
{
namespace
{

void flatten_hierarchy(entt::const_handle obj, std::vector<entity_data<entt::const_handle>>& entities)
{
    auto& trans_comp = obj.get<transform_component>();
    const auto& children = trans_comp.get_children();

    entity_data<entt::const_handle> data;
    data.components.entity = obj;

    entities.emplace_back(data);

    entities.reserve(entities.size() + children.size());
    for(const auto& child : children)
    {
        flatten_hierarchy(child, entities);
    }
}

template<typename Archive>
void save_to_archive(Archive& ar, entt::const_handle obj)
{
    bool pushed = push_save_context();

    bool is_root = obj.all_of<root_component>();
    if(!is_root)
    {
        const_handle_cast(obj).emplace<root_component>();
    }

    auto& trans_comp = obj.get<transform_component>();

    std::vector<entity_data<entt::const_handle>> entities;
    flatten_hierarchy(obj, entities);

    // A prefab document's own statements - what it says about the content it nests - written
    // before the records, so they are known before any record that depends on them loads.
    if(get_save_context().is_saving_to_prefab())
    {
        const auto statements = fold_document_statements(const_handle_cast(obj));
        try_save(ar, ser20::make_nvp("statements", statements));
    }

    try_save(ar, ser20::make_nvp("entities", entities));

    static const std::string version = "1.0.0";
    try_save(ar, ser20::make_nvp("version", version));

    // Written only when the deploy bake has already resolved nested instances, so an
    // ordinary save never claims it. Absent means "not resolved", which is the safe
    // reading for every document written before this existed.
    if(get_save_context().nesting_resolved)
    {
        try_save(ar, ser20::make_nvp("nesting_resolved", true));
    }

    if(!is_root)
    {
        const_handle_cast(obj).erase<root_component>();
    }

    pop_save_context(pushed);
}

template<typename Archive>
auto load_from_archive_impl(Archive& ar, entt::registry& registry) -> entt::handle
{
    // The document's statements, before its records: the filter asks "does this document state
    // it" while they load.
    {
        prefab_statements statements;
        if(try_load(ar, ser20::make_nvp("statements", statements)))
        {
            auto& load_ctx = get_load_context();
            load_ctx.document_statements = std::move(statements);
            load_ctx.has_document_statements = true;
        }
    }

    std::vector<entity_data<entt::handle>> entities;
    try_load(ar, ser20::make_nvp("entities", entities));

    std::string version;
    try_load(ar, ser20::make_nvp("version", version));

    bool nesting_resolved = false;
    if(try_load(ar, ser20::make_nvp("nesting_resolved", nesting_resolved)) && nesting_resolved)
    {
        get_load_context().nesting_resolved = true;
    }

    entt::handle result{};
    if(!entities.empty())
    {
        result = entities.front().components.entity;
    }

    auto& load_ctx = get_load_context();
    load_ctx.entities.reserve(load_ctx.entities.size() + entities.size());
    std::move(entities.begin(), entities.end(), std::back_inserter(load_ctx.entities));

    return result;
}

template<typename Archive>
void load_from_archive_start(Archive& ar, entt::registry& registry, entt::handle& e)
{
    bool pushed = push_load_context(registry);

    e = load_from_archive_impl(ar, registry);

    pop_load_context(pushed);
}

template<typename Archive>
auto load_from_archive_start(Archive& ar, entt::registry& registry) -> entt::handle
{
    bool pushed = push_load_context(registry);

    auto obj = load_from_archive_impl(ar, registry);

    pop_load_context(pushed);

    return obj;
}

template<typename Archive>
void load_from_archive(Archive& ar, entt::handle& obj)
{
    obj = load_from_archive_start(ar, *obj.registry());
}

template<typename Archive>
void save_to_archive(Archive& ar, const entt::registry& reg)
{
    bool pushed = push_save_context();

    size_t count = 0;
    reg.view<root_component, transform_component>().each(
        [&](auto e, auto&& comp1, auto&& comp2)
        {
            count++;
        });

    try_save(ar, ser20::make_nvp("entities_count", count));

    if(get_save_context().nesting_resolved)
    {
        try_save(ar, ser20::make_nvp("nesting_resolved", true));
    }

    reg.view<root_component, transform_component>().each(
        [&](auto e, auto&& comp1, auto&& comp2)
        {
            save_to_archive(ar, entt::const_handle(reg, e));
        });

    pop_save_context(pushed);
}

template<typename Archive>
void load_from_archive(Archive& ar, entt::registry& reg)
{
    size_t count = 0;
    try_load(ar, ser20::make_nvp("entities_count", count));

    bool pushed = push_load_context(reg);

    bool nesting_resolved = false;
    if(try_load(ar, ser20::make_nvp("nesting_resolved", nesting_resolved)) && nesting_resolved)
    {
        get_load_context().nesting_resolved = true;
    }

    std::vector<entt::handle> roots;
    roots.reserve(count);
    for(size_t i = 0; i < count; ++i)
    {
        // No scratch entity. load_from_archive_impl creates the entities it needs and
        // returns the root; the loaders reassign the handle they are given rather than
        // filling it in, so one handed in here is simply abandoned - a componentless
        // entity per root, on every scene load.
        roots.push_back(load_from_archive_impl(ar, reg));
    }

    // A scene from before ids named their document: attributed here, before any instance in
    // it syncs, so the instances' own assets can tell their content from what was added.
    if(get_load_context().saw_unqualified_ids)
    {
        for(auto& root : roots)
        {
            qualify_legacy_prefab_ids(root, {}, {}, nullptr, true);
        }
    }
    if(!get_load_context().legacy_overrides.empty())
    {
        convert_legacy_override_state(reg, {}, {});
    }

    pop_load_context(pushed);
}

} // namespace

void save_to_stream(std::ostream& stream, entt::const_handle obj)
{
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        // This subtree is becoming a prefab file, so everything nested inside it is that
        // file's content now - including whatever the user added or cloned in since it was
        // last written. Marking is otherwise derived on load, where it can only be inferred
        // from prefab uids that duplicates and clones share; here it is a fact.
        //
        // The const is an artifact of the parameter type. The registry behind a live handle
        // is not const - callers pass entities they are actively editing.
        const bool to_prefab = save_ctx_ptr != nullptr && save_ctx_ptr->is_saving_to_prefab();
        if(to_prefab && obj && obj.registry() != nullptr)
        {
            entt::handle mutable_obj{const_cast<entt::registry&>(*obj.registry()), obj.entity()};
            prepare_prefab_identity(mutable_obj, {}, true, {});
        }

        try
        {
            auto ar = ser20::create_oarchive_associative(stream);
            save_to_archive(ar, obj);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to save entity to stream: {}", e.what());
        }
    }
}

void save_to_file(const std::string& absolute_path, entt::const_handle obj)
{
    {
        std::ofstream stream(absolute_path);

        bool pushed = push_save_context();
        auto& save_ctx = get_save_context();
        save_ctx.save_source = obj;
        save_ctx.to_prefab = true;

        save_to_stream(stream, obj);

        save_ctx.to_prefab = false;
        save_ctx.save_source = {};
        pop_save_context(pushed);
    }
}

namespace asset_writer
{
auto atomic_save_to_file(const fs::path& key, entt::const_handle obj) -> bool
{
    bool saved = false;
    const bool pushed = push_save_context();
    auto& save_ctx = get_save_context();
    const auto previous_document = save_ctx.document_uid;
    try
    {
        fs::path absolute_key = fs::absolute(fs::resolve_protocol(key));

        // The file being written, so the ids this save issues can name it. Registered here if
        // it is new - the watcher would do the same a moment later - so the uid is the one
        // every later load of this file sees.
        auto& am = engine::context().get_cached<asset_manager>();
        save_ctx.document_uid = am.add_asset_for_path(absolute_key);

        fs::error_code err;
        atomic_write_file(
            absolute_key,
            [&](const fs::path& temp)
            {
                save_to_file(temp.string(), obj);
            },
            err);
        saved = !err;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Failed to save object to file: {0}", e.what());
    }
    save_ctx.document_uid = previous_document;
    pop_save_context(pushed);
    return saved;
}

auto atomic_save_to_file(const fs::path& key, entt::handle obj) -> bool
{
    return atomic_save_to_file(key, entt::const_handle{obj});
}
} // namespace asset_writer

void save_to_stream_bin(std::ostream& stream, entt::const_handle obj)
{
    if(stream.good())
    {
        // Same identity pass as the text writer, or a binary prefab save would write a
        // document whose nested instances are unnamed while the text save names them.
        const bool to_prefab = save_ctx_ptr != nullptr && save_ctx_ptr->is_saving_to_prefab();
        if(to_prefab && obj && obj.registry() != nullptr)
        {
            entt::handle mutable_obj{const_cast<entt::registry&>(*obj.registry()), obj.entity()};
            prepare_prefab_identity(mutable_obj, {}, true, {});
        }

        ser20::oarchive_binary_t ar(stream);

        save_to_archive(ar, obj);
    }
}

void save_to_file_bin(const std::string& absolute_path, entt::const_handle obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);

    bool pushed = push_save_context();
    auto& save_ctx = get_save_context();
    save_ctx.save_source = obj;
    save_ctx.to_prefab = true;

    save_to_stream_bin(stream, obj);
    save_ctx.to_prefab = false;
    save_ctx.save_source = {};
    pop_save_context(pushed);
}


void load_from_view(std::string_view view, entt::registry& obj)
{
    if(!view.empty())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            auto ar = ser20::create_iarchive_associative(view.data(), view.size());
            load_from_archive_start(ar, obj);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load entity from view: {}", e.what());
        }
    }
}

void load_from_stream(std::istream& stream, entt::registry& obj)
{
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);
        try
        {
            auto ar = ser20::create_iarchive_associative(stream);
            load_from_archive_start(ar, obj);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load entity from stream: {}", e.what());
        }
    }
}

void load_from_view(std::string_view view, entt::handle& obj)
{
    if(!view.empty())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            auto ar = ser20::create_iarchive_associative(view.data(), view.size());
            load_from_archive(ar, obj);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load entity from view: {}", e.what());
        }
    }
}

void load_from_stream(std::istream& stream, entt::handle& obj)
{
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);
        try
        {
            auto ar = ser20::create_iarchive_associative(stream);
            load_from_archive(ar, obj);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load entity from stream: {}", e.what());
        }
    }
}

void load_from_file(const std::string& absolute_path, entt::handle& obj)
{
    fs::file_istream input(absolute_path);
    if(!input.is_open())
    {
        return;
    }
    load_from_stream(input, obj);
}

void load_from_stream_bin(std::istream& stream, entt::handle& obj)
{
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);
        try
        {
            ser20::iarchive_binary_t ar(stream);
            load_from_archive(ar, obj);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load entity from stream: {}", e.what());
        }
    }
}

void load_from_file_bin(const std::string& absolute_path, entt::handle& obj)
{
    fs::file_istream input(absolute_path, std::ios::binary);
    if(!input.is_open())
    {
        return;
    }
    load_from_stream_bin(input, obj);
}

namespace
{

/// Assets currently being expanded, innermost last. Prefab A may contain an instance of B
/// which contains an instance of A; without this the expansion never terminates.
thread_local std::vector<hpp::uuid> expanding_prefabs;

/// Backstop for anything the cycle check cannot see - a chain long enough to be a mistake.
constexpr size_t max_prefab_nesting_depth = 16;

struct scoped_prefab_expansion
{
    explicit scoped_prefab_expansion(const hpp::uuid& uid)
    {
        if(uid.is_nil())
        {
            return;
        }

        if(expanding_prefabs.size() >= max_prefab_nesting_depth)
        {
            APPLOG_ERROR("Prefab nesting deeper than {} levels; refusing to expand further.",
                         max_prefab_nesting_depth);
            return;
        }

        if(std::find(expanding_prefabs.begin(), expanding_prefabs.end(), uid) != expanding_prefabs.end())
        {
            APPLOG_ERROR("Prefab {} contains itself, directly or through another prefab; "
                         "refusing to expand it again.",
                         hpp::to_string(uid));
            return;
        }

        expanding_prefabs.push_back(uid);
        entered_ = true;
    }

    ~scoped_prefab_expansion()
    {
        if(entered_)
        {
            expanding_prefabs.pop_back();
        }
    }

    scoped_prefab_expansion(const scoped_prefab_expansion&) = delete;
    auto operator=(const scoped_prefab_expansion&) -> scoped_prefab_expansion& = delete;

    /// False when the expansion was refused, in which case the caller must not proceed.
    auto entered() const -> bool
    {
        return entered_;
    }

private:
    bool entered_{};
};

void collect_nested_instance_roots(entt::handle obj, std::vector<entt::handle>& out, bool is_subtree_root)
{
    if(!obj)
    {
        return;
    }

    if(!is_subtree_root && obj.all_of<prefab_component>())
    {
        // Its own sync walks what is nested inside it, so stop here.
        out.push_back(obj);
        return;
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            collect_nested_instance_roots(child, out, false);
        }
    }
}

/**
 * @brief Settles the slots of the instances nested in a clone.
 *
 * A slot is kept when the document that placed it is an instance *inside* the clone - the
 * clone root or something below it. The copy is as much an instance of that document as the
 * original, and the document names the same slot in both, for the same reason two fresh
 * instantiates agree on their slots. Regenerating those (as this used to) made every one a
 * named slot the document does not mention, and the clone's next sync rebuilt or removed them.
 *
 * A slot placed by a document the clone is merely *inside of* goes nil: the copy is an
 * addition to that document, not its slot, and a named, unmentioned copy is what that
 * document's next sync would read as a dropped slot and remove. One rule, at any depth - an
 * instance the outer document added two levels down is told from the asset's own content by
 * whose name its slot carries, not by where it sits.
 */
void reconcile_cloned_slots(entt::handle obj, std::vector<hpp::uuid> instance_chain)
{
    auto* trans_comp = obj.try_get<transform_component>();
    if(trans_comp == nullptr)
    {
        return;
    }

    for(auto child : trans_comp->get_children())
    {
        auto* nested_prefab = child.try_get<prefab_component>();
        if(nested_prefab == nullptr)
        {
            reconcile_cloned_slots(child, instance_chain);
            continue;
        }

        const bool placed_inside_clone =
            !nested_prefab->instance_document.is_nil() &&
            std::find(instance_chain.begin(), instance_chain.end(), nested_prefab->instance_document) !=
                instance_chain.end();
        if(!placed_inside_clone)
        {
            nested_prefab->instance_id = {};
            nested_prefab->instance_document = {};
            nested_prefab->placed_by = instance_placement::other;
        }

        auto chain = instance_chain;
        chain.push_back(nested_prefab->source.uid());
        reconcile_cloned_slots(child, std::move(chain));
    }
}

} // namespace

/**
 * @brief After a fresh instantiate, records that the document placed its directly nested
 *        instances - the legacy discriminator for unnamed slots, which the record post-rule
 *        sets only when loading over an existing instance.
 */
void attribute_fresh_nested_placement(entt::handle root)
{
    std::vector<entt::handle> nested;
    collect_nested_instance_roots(root, nested, true);
    for(auto& instance : nested)
    {
        if(auto* prefab_comp = instance.try_get<prefab_component>())
        {
            prefab_comp->placed_by = instance_placement::container;
        }
    }
}

/**
 * @brief Whether a document's "nesting already resolved" claim can be believed.
 *
 * Only in a deployed build, and the reason is about *who wrote the claim*, not about the
 * claim itself. The marker records that a bake resolved this asset's nested instances as of
 * when the bake ran, and nothing in the asset invalidates it afterwards. Believing it
 * unconditionally means serving stale content the moment an inner prefab is edited - which
 * is what happened, first in the editor and then in a deployed build.
 *
 * What makes it safe here is that deploying is the only way to produce a deployed build,
 * and only the deploy writes marked documents at all: the cook stages them and overlays the
 * deploy destination, never the editor's compiled cache (editor_actions::deploy_project). So
 * a marker seen at runtime was written by the deploy that produced the build being run,
 * describing that build. The freshness comes from the pipeline's ordering, the same way any
 * build system guarantees it - not from anything the document knows.
 *
 * In the editor, assets change under a marker that was written earlier, so it is ignored
 * and nested instances are refreshed on load. That costs an asset load per instance and is
 * always right.
 *
 * If baking ever becomes optional, or anything other than a deploy can write the marker,
 * this stops being true.
 */
auto can_trust_resolved_marker() -> bool
{
    auto* ctx = engine::try_context();
    return ctx != nullptr && ctx->has<deploy>();
}

namespace
{

/// Set while a resync is in progress, between the load and the point where each nested
/// instance has its local overrides back. Not on the load context: the load pushes and pops
/// its own, and this has to outlive that.
thread_local bool defer_nested_sync_flag{};

auto is_nested_sync_deferred() -> bool
{
    return defer_nested_sync_flag;
}
} // namespace

scoped_deferred_nested_sync::scoped_deferred_nested_sync() : previous_(defer_nested_sync_flag)
{
    defer_nested_sync_flag = true;
}

scoped_deferred_nested_sync::~scoped_deferred_nested_sync()
{
    defer_nested_sync_flag = previous_;
}

namespace
{

/**
 * @brief Whether a serialization path addresses a nested instance's own prefab bookkeeping.
 *
 * "entities/<uuid>/components/prefab_component/..." and the instance marker. Those describe
 * the link itself rather than the entity's contents, so the document that owns the instance
 * states them outright.
 */
struct serialization_path_parts
{
    hpp::uuid entity_uuid;

    /// Everything after "components/", so "tag_component/name" or "has_tag_component".
    std::string_view component_path;
    bool valid{};
};

/// Splits "entities/<uuid>/components/<component path>", the form path_context builds while
/// an entity's components are read.
auto split_serialization_path(const std::string& serialization_path) -> serialization_path_parts
{
    serialization_path_parts parts;

    const std::string_view path(serialization_path);
    const auto first = path.find('/');
    if(first == std::string_view::npos)
    {
        return parts;
    }
    const auto second = path.find('/', first + 1);
    if(second == std::string_view::npos)
    {
        return parts;
    }
    const auto third = path.find('/', second + 1);
    if(third == std::string_view::npos || third + 1 >= path.size())
    {
        return parts;
    }

    const auto uuid_opt = hpp::uuid::from_string(path.substr(first + 1, second - first - 1));
    if(!uuid_opt.has_value())
    {
        return parts;
    }

    parts.entity_uuid = uuid_opt.value();
    parts.component_path = path.substr(third + 1);
    parts.valid = true;
    return parts;
}

/// Whether a component path addresses a nested instance's own prefab bookkeeping - the link
/// itself rather than the entity's contents, which the document that owns the instance
/// states outright.
auto is_prefab_bookkeeping_component(std::string_view component_path) -> bool
{
    if(component_path.starts_with("has_"))
    {
        component_path.remove_prefix(4);
    }

    return component_path.starts_with("prefab_component");
}

/// Descendants of an instance whose prefab uid is in `wanted`, without descending into a
/// deeper instance - those belong to another asset and are keyed in its uid space.
void collect_entities_by_prefab_uid(entt::handle obj,
                                    const std::set<hpp::uuid>& wanted,
                                    std::vector<entt::handle>& out,
                                    bool is_subtree_root)
{
    if(!obj)
    {
        return;
    }

    if(!is_subtree_root)
    {
        if(obj.all_of<prefab_component>())
        {
            return;
        }

        if(const auto* id_comp = obj.try_get<prefab_id_component>())
        {
            if(wanted.count(id_comp->id) != 0u)
            {
                out.push_back(obj);
                return;
            }
        }
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            collect_entities_by_prefab_uid(child, wanted, out, false);
        }
    }
}

/**
 * @brief Whether a component path is a nested instance root's placement, or on the way to it.
 *
 * Local position and rotation of an instance root belong to whoever *placed* the instance.
 * For a top-level instance that is the scene, and its own asset never restates them - the
 * "implicit override". For a nested instance the placer is the containing document, and the
 * same implicit rule from the nested asset's side means that if the container did not restate
 * them either, nothing could ever put a nested instance back where its container authored it.
 *
 * Scale and skew are deliberately not here: those follow the nested asset unless overridden,
 * from either side. Nor is the parent link: a reparent made here has no override bookkeeping,
 * and restating it would undo the reparent on every sync.
 */
auto is_nested_root_placement_path(std::string_view component_path) -> bool
{
    constexpr std::string_view transform = "transform_component";
    constexpr std::string_view local = "transform_component/local_transform";
    constexpr std::string_view position = "transform_component/local_transform/position";
    constexpr std::string_view rotation = "transform_component/local_transform/rotation";

    // The two containers on the way down have to pass for the leaves to be reached at all;
    // scale and skew underneath them are still decided leaf by leaf.
    return component_path == transform || component_path == local || component_path.starts_with(position) ||
           component_path.starts_with(rotation);
}

/// A nested instance's overrides, split into the part the containing document authored and
/// the part that was added here. Only the second survives the document being replayed.


/// Every nested instance under `root`, at any depth - unlike collect_nested_instance_roots,
/// which stops at the first one because each instance's own sync continues from there.

/// Nested instance roots directly inside one instance's content (not inside a deeper
/// instance) whose slot is among `slots`.
void collect_nested_instances_by_slot(entt::handle obj,
                                      const std::set<hpp::uuid>& slots,
                                      std::vector<entt::handle>& out,
                                      bool is_subtree_root)
{
    if(!obj)
    {
        return;
    }
    if(!is_subtree_root)
    {
        if(const auto* nested_prefab = obj.try_get<prefab_component>())
        {
            if(slots.count(nested_prefab->instance_id) != 0u)
            {
                out.push_back(obj);
            }
            return;
        }
    }
    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            collect_nested_instances_by_slot(child, slots, out, false);
        }
    }
}

/**
 * @brief Re-applies every removal stated about content under `root` after its replay.
 *
 * The mapping already kept the replayed document from recreating what it lists as removed. What
 * is left is content the document supplies that something *above* it removed - an outer
 * document, the scene - and the document's own newly stated removals of content a nested asset
 * supplies. Both are alive after the replay, and go.
 */
void apply_removal_statements(entt::handle root)
{
    const auto removals = collect_replay_removals(root);
    if(removals.removed_entities.empty() && removals.removed_instances.empty())
    {
        return;
    }

    std::vector<entt::handle> doomed;
    const auto collect_at = [&removals, &doomed](entt::handle instance_root, const std::vector<hpp::uuid>& chain)
    {
        std::set<hpp::uuid> wanted;
        for(const auto& removed : removals.removed_entities)
        {
            if(removed.instance_path == chain)
            {
                wanted.insert(removed.id);
            }
        }
        if(!wanted.empty())
        {
            collect_entities_by_prefab_uid(instance_root, wanted, doomed, true);
        }

        std::set<hpp::uuid> slots;
        for(const auto& removed : removals.removed_instances)
        {
            if(removed.instance_path == chain)
            {
                slots.insert(removed.id);
            }
        }
        if(!slots.empty())
        {
            collect_nested_instances_by_slot(instance_root, slots, doomed, true);
        }
    };

    std::vector<hpp::uuid> chain;
    collect_at(root, chain);
    for_each_addressable_nested_root(root, chain, collect_at);

    for(auto& entity : doomed)
    {
        if(entity)
        {
            scene::destroy_entity(entity);
        }
    }
}

} // namespace

auto sync_prefab_instance(entt::handle instance) -> bool
{
    if(!instance)
    {
        return false;
    }

    auto* prefab_comp = instance.try_get<prefab_component>();
    auto* trans_comp = instance.try_get<transform_component>();
    if(prefab_comp == nullptr || trans_comp == nullptr || !prefab_comp->source)
    {
        return false;
    }

    const auto source = prefab_comp->source;

    auto parent = trans_comp->get_parent();
    const auto pos = trans_comp->get_position_local();
    const auto rot = trans_comp->get_rotation_local();

    // Copied, not referenced. prefab_component sets in_place_delete to false, so its pool
    // swaps elements on removal - and the load below both adds and removes prefab_components
    // while this callback is live.
    // What the replay must leave alone, relative to this instance and at every depth below it:
    // what was stated here (this root's local list, the local lists of the named instances
    // nested in it, an authoring root's adopted list above) and what the documents above state.
    // What it lets through of the nested content is what *this* document states - read from the
    // document as it loads, or from the live list for a file written before documents carried
    // one. Nothing is attributed afterwards; each list has one author.
    const replay_statements protect = collect_replay_statements(instance);
    std::vector<entt::handle> nested_roots;
    collect_nested_instance_roots(instance, nested_roots, true);
    const bool needs_override_tracking = !nested_roots.empty() || !protect.local.empty() || !protect.stated.empty();

    // Slot chains from this instance to each nested owner the filter meets, computed once.
    std::map<entt::entity, std::vector<hpp::uuid>> owner_paths;

    serialization::path_context path_ctx;
    serialization::path_context* old_ctx = serialization::get_path_context();
    if(needs_override_tracking)
    {
        path_ctx.should_serialize_property_callback =
            [&protect, &owner_paths, instance](const std::string& property_path)
        {
            const auto parts = split_serialization_path(property_path);
            if(!parts.valid)
            {
                return true;
            }
            auto component_path = parts.component_path;
            if(component_path.starts_with("has_"))
            {
                component_path.remove_prefix(4);
            }

            auto& load_ctx = get_load_context();
            const auto owner = load_ctx.current_nested_owner;
            if(!owner)
            {
                // This instance's own content: what is stated here or above stays; the rest
                // is the document's to restate. Protection is on-or-above: an override on one
                // field must not shield its siblings, so the level above passes and each field
                // decides for itself.
                return !protect.local.has_override_on_or_above({}, parts.entity_uuid, component_path) &&
                       !protect.stated.has_override_on_or_above({}, parts.entity_uuid, component_path);
            }

            // Content of an instance nested in this one - the document's snapshot of it. Its
            // prefab_component is let through regardless: link, slot and the document's copy
            // of that instance's own list, which loads before anything it has to filter.
            if(is_prefab_bookkeeping_component(component_path))
            {
                return true;
            }
            if(!owner.all_of<prefab_component>())
            {
                return false;
            }
            const std::vector<hpp::uuid>* owner_path = nullptr;
            if(const auto cached = owner_paths.find(owner.entity()); cached != owner_paths.end())
            {
                owner_path = &cached->second;
            }
            else
            {
                std::vector<hpp::uuid> chain;
                if(!instance_path_between(instance, owner, chain))
                {
                    return false;
                }
                owner_path = &owner_paths.emplace(owner.entity(), std::move(chain)).first->second;
            }

            // Stated here, or by a document above this one: kept, whatever this document says.
            // On-or-above, for the same reason as above: a local position override on a nested
            // root must not keep the document's scale out of the same transform.
            if(protect.local.has_override_on_or_above(*owner_path, parts.entity_uuid, component_path) ||
               protect.stated.has_override_on_or_above(*owner_path, parts.entity_uuid, component_path))
            {
                return false;
            }

            // A nested root's placement is the containing document's to restate.
            const auto* owner_id = owner.try_get<prefab_id_component>();
            if(owner_id != nullptr && owner_id->id == parts.entity_uuid &&
               is_nested_root_placement_path(component_path))
            {
                return true;
            }

            const prefab_statements* document = nullptr;
            if(load_ctx.has_document_statements)
            {
                document = &load_ctx.document_statements;
            }
            else if(const auto* own = instance.try_get<prefab_component>())
            {
                document = &own->from_document;
            }
            return document != nullptr &&
                   document->has_override_touching(*owner_path, parts.entity_uuid, component_path);
        };
        path_ctx.enable_recording();
        serialization::set_path_context(&path_ctx);
    }

    APPLOG_TRACE("Syncing prefab instance: {}", source.id());

    // The replay cascades into the nested instances below, once the removals stated about
    // them have been re-applied - not before, from inside the load. Unless a caller deferred
    // the cascade itself: then this is a replay of one document, and the caller's to follow up.
    const bool cascade_deferred_by_caller = is_nested_sync_deferred();
    bool loaded = false;
    {
        // Scoped to the load alone: the cascade below has to run with the caller's flag, or
        // every nested sync would read this one's deferral as its caller's and stop a level
        // short.
        const scoped_deferred_nested_sync defer_nested;
        loaded = scene::instantiate_out(*instance.registry(), source, instance, false);
    }

    if(loaded && instance)
    {
        auto& new_trans = instance.get<transform_component>();
        new_trans.set_position_local(pos);
        new_trans.set_rotation_local(rot);
        new_trans.set_parent(parent, false);
    }

    if(needs_override_tracking)
    {
        serialization::set_path_context(old_ctx);
    }

    if(loaded && instance)
    {
        apply_removal_statements(instance);
        if(!cascade_deferred_by_caller)
        {
            sync_nested_prefab_instances(instance);
        }
    }

    return loaded;
}

auto sync_nested_prefab_instances(entt::handle root) -> size_t
{
    if(!root)
    {
        return 0;
    }

    // Collected before syncing: a sync rebuilds the subtree it targets, so walking and
    // mutating at the same time would iterate a hierarchy being rewritten underneath.
    std::vector<entt::handle> nested;
    collect_nested_instance_roots(root, nested, true);

    size_t synced = 0;
    for(auto& instance : nested)
    {
        if(instance && sync_prefab_instance(instance))
        {
            ++synced;
        }
    }
    return synced;
}

auto sync_all_prefab_instances(entt::registry& registry) -> size_t
{
    std::vector<entt::handle> roots;
    registry.view<root_component, transform_component>().each(
        [&](auto e, auto&&, auto&&)
        {
            roots.emplace_back(registry, e);
        });

    size_t synced = 0;
    for(auto& root : roots)
    {
        if(!root)
        {
            continue;
        }

        // A root that is itself an instance has to be synced here - sync_nested_prefab_instances
        // deliberately skips the subtree root, which is the only difference between a scene
        // and a single-rooted prefab.
        if(root.all_of<prefab_component>() && sync_prefab_instance(root))
        {
            ++synced;
        }

        // Walked afterwards regardless. Syncing the root normally cascades into its nesting
        // (load_from_prefab_out refreshes what it loads), but only when that sync actually
        // ran - it does nothing for an instance with no source, or one refused by the cycle
        // guard. Re-walking costs a reload of instances already refreshed, which is
        // idempotent, and guarantees the recursion does not depend on the step above having
        // succeeded.
        synced += sync_nested_prefab_instances(root);
    }

    return synced;
}

auto load_from_prefab_out(const asset_handle<prefab>& pfb,
                          entt::registry& registry,
                          entt::handle& obj) -> bool
{
    // Nothing to load over: a fresh instantiate rather than a sync.
    const bool fresh = !obj;

    // Refuses re-entry for an asset already being expanded further up the chain.
    scoped_prefab_expansion expansion(pfb.uid());
    if(!expansion.entered())
    {
        return false;
    }

    // copy here to keep it alive
    auto prefab = pfb.get();
    const auto& buffer = prefab->buffer.data;

    // An empty document loads nothing, and must say so: a caller that takes "true" as "the
    // replay ran" goes on to re-derive state from a replay that never happened.
    bool result = !buffer.empty();
    if(!buffer.empty())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            auto ar = ser20::create_iarchive_associative(buffer.data(), buffer.size());
             
            bool pushed = push_load_context(registry);
            get_load_context().document_uid = pfb.uid();

            {
                // Scopes the prefab-uid mapping to this instance. Records inside the frame
                // resolve against the subtree recorded below and nothing else.
                scoped_instance_frame frame;

                add_to_uid_mapping(obj);

                load_from_archive_start(ar, registry, obj);

                cleanup_uid_mapping();

            }

            if(get_load_context().saw_unqualified_ids)
            {
                qualify_legacy_prefab_ids(obj, pfb.uid(), {}, nullptr, true);
            }
            if(!get_load_context().legacy_overrides.empty())
            {
                convert_legacy_override_state(registry, obj, pfb.uid());
            }

            // Read before the pop: pop_load_context destroys the context.
            const bool nesting_resolved = get_load_context().nesting_resolved;
            prefab_statements document_statements = std::move(get_load_context().document_statements);

            pop_load_context(pushed);


            if(obj)
            {
                auto& pfb_comp = obj.get_or_emplace<prefab_component>();
                pfb_comp.source = pfb;
                // The document's own statements, replaced wholesale: one author, and this is it.
                pfb_comp.from_document = std::move(document_statements);
                if(fresh)
                {
                    // A fresh instantiate is placed by whoever asked for it - the scene, an
                    // outer document's record, the user - never by its own document.
                    pfb_comp.placed_by = instance_placement::other;
                }

                // Same staleness as in load_from_prefab: refresh anything nested against
                // its own asset rather than trusting this one's snapshot of it - unless the
                // document says a bake already did exactly that, or the caller is going to do
                // it itself once it has restored what belongs to the instance.
                if((!nesting_resolved || !can_trust_resolved_marker()) && !is_nested_sync_deferred())
                {
                    sync_nested_prefab_instances(obj);
                }

                if(fresh)
                {
                    attribute_fresh_nested_placement(obj);
                }
            }
        }
        catch(const std::exception& e)
        {
            result = false;
            APPLOG_ERROR("Broken prefab {}: {}", pfb.id(), e.what());
        }
    }

    return result;
}

void regenerate_entity_uids(entt::handle obj)
{
    auto& id_comp = obj.get_or_emplace<id_component>();
    id_comp.regenerate_id();
    if(auto transform = obj.try_get<transform_component>())
    {
        for(auto child : transform->get_children())
        {
            regenerate_entity_uids(child);
        }
    }
}

auto load_from_prefab(const asset_handle<prefab>& pfb, entt::registry& registry) -> entt::handle
{
    // Refuses re-entry for an asset already being expanded further up the chain.
    scoped_prefab_expansion expansion(pfb.uid());
    if(!expansion.entered())
    {
        return {};
    }

    entt::handle obj;

    // copy here to keep it alive
    auto prefab = pfb.get();
    const auto& buffer = prefab->buffer.data;

    if(!buffer.empty())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            auto ar = ser20::create_iarchive_associative(buffer.data(), buffer.size());

            // Pushed here rather than inside load_from_archive_start so the document's
            // nesting_resolved flag can be read before the context is destroyed.
            const bool pushed = push_load_context(registry);
            get_load_context().document_uid = pfb.uid();
            obj = load_from_archive_impl(ar, registry);
            if(get_load_context().saw_unqualified_ids)
            {
                qualify_legacy_prefab_ids(obj, pfb.uid(), {}, nullptr, true);
            }
            if(!get_load_context().legacy_overrides.empty())
            {
                convert_legacy_override_state(registry, obj, pfb.uid());
            }
            const bool nesting_resolved = get_load_context().nesting_resolved;
            prefab_statements document_statements = std::move(get_load_context().document_statements);
            pop_load_context(pushed);

            if(obj)
            {
                regenerate_entity_uids(obj);
                auto& pfb_comp = obj.get_or_emplace<prefab_component>();
                pfb_comp.source = pfb;
                pfb_comp.from_document = std::move(document_statements);
                // Always a fresh instantiate on this path: placed by whoever asked, not by
                // its own document.
                pfb_comp.placed_by = instance_placement::other;

                // This asset carries a snapshot of anything nested inside it, taken when it
                // was last saved - so it is stale the moment that inner asset is edited.
                // Refresh each nested instance against its own asset, unless a bake has
                // already resolved them and said so.
                if(!nesting_resolved || !can_trust_resolved_marker())
                {
                    sync_nested_prefab_instances(obj);
                }

                // Always a fresh instantiate on this path.
                attribute_fresh_nested_placement(obj);
            }
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Broken prefab {}: {}", pfb.id(), e.what());
        }
    }


    return obj;
}
auto load_from_prefab_bin(const asset_handle<prefab>& pfb, entt::registry& registry) -> entt::handle
{
    entt::handle obj;

    // copy here to keep it alive
    auto prefab = pfb.get();
    auto buffer = prefab->buffer.get_stream_buf();
    std::istream stream(&buffer);
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            ser20::iarchive_binary_t ar(stream);

            obj = load_from_archive_start(ar, registry);

            if(obj)
            {
                regenerate_entity_uids(obj);
                auto& pfb_comp = obj.get_or_emplace<prefab_component>();
                pfb_comp.source = pfb;
            }
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Broken prefab {}", pfb.id());
        }
    }

    return obj;
}

auto has_prefab_component(const entt::const_handle& obj) -> bool
{

    if(!obj.valid())
    {
        return false;
    }
    if(obj.all_of<prefab_component>())
    {
        return true;
    }
    
    if(auto transform_comp = obj.try_get<transform_component>())
    {
        for(auto child : transform_comp->get_children())
        {
            if(has_prefab_component(child))
            {
                return true;
            }
        }
    }
    return false;
}

void clone_entity_from_stream(entt::const_handle src_obj, entt::handle& dst_obj)
{
    // APPLOG_INFO_PERF(std::chrono::microseconds);

    bool has_prefab_instances = has_prefab_component(src_obj);

    auto clone_mode = has_prefab_instances ? clone_mode_t::cloning_prefab_instance : clone_mode_t::cloning_object;
    bool pushed = push_save_context();
    auto& save_ctx = get_save_context();
    save_ctx.save_source = src_obj;
    save_ctx.to_prefab = false;
    save_ctx.clone_mode = clone_mode;
    std::stringstream ss;
    {
        // This buffer is written and read back a few lines below and then discarded, so
        // there is nobody to indent it for.
        serialization::scoped_output_format compact(serialization::output_format::compact);
        save_to_stream(ss, src_obj);
    }
    save_ctx.to_prefab = false;
    save_ctx.save_source = {};
    save_ctx.clone_mode = clone_mode_t::none;
    pop_save_context(pushed);


    pushed = push_load_context(*dst_obj.registry());
    auto& load_ctx = get_load_context();
    load_ctx.clone_mode = clone_mode;

    {  
        load_from(ss, dst_obj);
    }
    load_ctx.clone_mode = clone_mode_t::none;
    pop_load_context(pushed);

    // The clone root itself is nobody's slot: it is an addition to whatever contains it, and
    // only becomes a slot when that container is written as a prefab. Which of the instances
    // nested below it keep their slots is decided by whose slots they are - see
    // reconcile_cloned_slots.
    if(dst_obj)
    {
        std::vector<hpp::uuid> instance_chain;
        if(auto* prefab_comp = dst_obj.try_get<prefab_component>())
        {
            prefab_comp->instance_id = {};
            prefab_comp->instance_document = {};
            prefab_comp->placed_by = instance_placement::other;
            instance_chain.push_back(prefab_comp->source.uid());
        }
        reconcile_cloned_slots(dst_obj, std::move(instance_chain));
    }
}

void save_to_stream(std::ostream& stream, const scene& scn)
{
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            auto ar = ser20::create_oarchive_associative(stream);
            save_to_archive(ar, *scn.registry);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to save scene to stream: {}", e.what());
        }
    }
}
void save_to_file(const std::string& absolute_path, const scene& scn)
{
    // APPLOG_INFO_PERF(std::chrono::microseconds);

    std::ofstream stream(absolute_path);
    save_to_stream(stream, scn);
}
void save_to_stream_bin(std::ostream& stream, const scene& scn)
{
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            ser20::oarchive_binary_t ar(stream);
            save_to_archive(ar, *scn.registry);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to save scene to stream: {}", e.what());
        }
    }
}
void save_to_file_bin(const std::string& absolute_path, const scene& scn)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    save_to_stream_bin(stream, scn);
}


void load_from_view(std::string_view view, scene& scn)
{
    if(!view.empty())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            auto ar = ser20::create_iarchive_associative(view.data(), view.size());
            load_from_archive(ar, *scn.registry);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load scene from view: {}", e.what());
        }
    }
}

void load_from_stream(std::istream& stream, scene& scn)
{
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        stream.seekg(0);

        try
        {
            auto ar = ser20::create_iarchive_associative(stream);
            load_from_archive(ar, *scn.registry);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load scene from stream: {}", e.what());
        }
    }
}
void load_from_file(const std::string& absolute_path, scene& scn)
{
    fs::file_istream input(absolute_path);
    if(!input.is_open())
    {
        return;
    }
    load_from_stream(input, scn);
}
void load_from_stream_bin(std::istream& stream, scene& scn)
{
    if(stream.good())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        stream.seekg(0);

        try
        {
            scn.unload();
            ser20::iarchive_binary_t ar(stream);
            load_from_archive(ar, *scn.registry);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load scene from stream: {}", e.what());
        }
    }
}
void load_from_file_bin(const std::string& absolute_path, scene& scn)
{
    fs::file_istream input(absolute_path, std::ios::binary);
    if(!input.is_open())
    {
        return;
    }
    load_from_stream_bin(input, scn);
}

auto load_from_prefab(const asset_handle<scene_prefab>& pfb, scene& scn) -> bool
{
    // copy here to keep it alive
    auto prefab = pfb.get();
    const auto& buffer = prefab->buffer.data;

    if(!buffer.empty())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);
        try
        {
            scn.unload();
            auto ar = ser20::create_iarchive_associative(buffer.data(), buffer.size());
            load_from_archive(ar, *scn.registry);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("Failed to load scene from prefab: {}", e.what());
        }
    }

    return true;
}
auto load_from_prefab_bin(const asset_handle<scene_prefab>& pfb, scene& scn) -> bool
{
    // copy here to keep it alive
    auto prefab = pfb.get();
    auto buffer = prefab->buffer.get_stream_buf();

    // APPLOG_INFO_PERF(std::chrono::microseconds);
    std::istream stream(&buffer);
    if(!stream.good())
    {
        return false;
    }

    load_from_stream_bin(stream, scn);

    return true;
}

void clone_scene_from_stream(const scene& src_scene, scene& dst_scene)
{
    dst_scene.unload();

    auto& src = src_scene.registry;
    auto& dst = dst_scene.registry;

    // APPLOG_INFO_PERF(std::chrono::microseconds);

    // Every root goes through a throwaway buffer on its way across. Indenting those is
    // pure cost: this is the edit-scene -> play-scene path, so it runs on every play press.
    serialization::scoped_output_format compact(serialization::output_format::compact);

    // One load context for the whole scene, not one per root.
    //
    // The roots are serialized separately, but an entity link that crosses from one root
    // into another is written as the target's uid and resolved through the context's uid
    // map. With a context per root that map only ever held the root being loaded, so a
    // cross-root link found nothing, fell through to "create a new entity", and the link
    // ended up aimed at an empty entity nothing else referenced - silently, on every play
    // press.
    //
    // Sharing the context turns the same path into a forward reference: the first mention
    // creates the entity and records its uid, and the record that actually defines it -
    // loaded with whichever root owns it - resolves to that same entity and fills it in.
    // That is already how a parent's "children" list works within a single root; it just
    // never spanned roots before.
    //
    // load_from_archive_start still pushes per root, but push_load_context returns false
    // when one is already active and the matching pop is then a no-op, so the inner calls
    // join this context instead of replacing it.
    //
    // A side effect worth knowing: the post-load callbacks now fire once with every
    // entity, the way a scene loaded from file does, instead of once per root.
    const bool pushed = push_load_context(*dst);

    src->view<root_component, transform_component>().each(
        [&](auto e, auto&& comp1, auto&& comp2)
        {
            std::stringstream ss;
            save_to_stream(ss, src_scene.create_handle(e));

            entt::registry& reg = *dst;
            load_from(ss, reg);
        });

    pop_load_context(pushed);
}
} // namespace unravel
