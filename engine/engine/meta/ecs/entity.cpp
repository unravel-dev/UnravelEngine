#include "entity.hpp"

#include <chrono>
#include <serialization/archives/yaml.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <filesystem/file_istream.h>
#include <serialization/serialization.h>
#include "components/all_components.h"
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
        if(const auto* prefab_comp = obj.try_get<prefab_component>())
        {
            // Entities this instance dropped. Null so the document's record for them is
            // skipped - the removal is the instance's own state, not something the document
            // gets to undo by still carrying a snapshot of them.
            for(const auto& entity_uuid : prefab_comp->removed_entities)
            {
                scope.by_prefab_uid[entity_uuid] = {};
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

    for(const auto& removed : prefab_comp->removed_instances)
    {
        auto removed_path = path;
        removed_path.push_back(removed);
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
        if(frame.foreign_entities.count(id_comp->id) == 0u)
        {
            frame.mapping_by_prefab_uid[id_comp->id].handle = obj;
        }
    }

    if(auto* prefab_comp = obj.try_get<prefab_component>())
    {
        for(auto& entity_uuid : prefab_comp->removed_entities)
        {
            frame.mapping_by_prefab_uid[entity_uuid].handle = {};
        }

        for(const auto& removed : prefab_comp->removed_instances)
        {
            frame.removed_instance_paths.insert({removed});
        }
    }

    // Added by whatever contains this instance, so its asset knows nothing about them.
    // Leaving them out of the mapping is what leaves them alone - the same position an entity
    // added by hand is in, which has no prefab uid to be found by.
    if(is_instance_root)
    {
        if(const auto* prefab_comp = obj.try_get<prefab_component>())
        {
            for(const auto& foreign : prefab_comp->foreign_entities)
            {
                frame.foreign_entities.insert(foreign);
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
    add_to_uid_mapping_impl(obj, *frame, true);

    // Recorded before any record is read, because adoption adds scopes of its own and the
    // question is about what was here to begin with.
    frame->has_named_instances = !frame->nested_scopes.empty();
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
        if(!mapping.consumed() && !mapping.shadowed && mapping.handle)
        {
            // APPLOG_TRACE("destroying entity: {}", uid.to_string());
            scene::destroy_entity(mapping.handle);
        }
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

        scene::destroy_entity(scope.root);
    }

}

namespace
{
/// Defined further down, next to the other nesting walkers.
void collect_nested_instance_roots(entt::handle obj, std::vector<entt::handle>& out, bool is_subtree_root);
} // namespace

/**
 * @brief Settles the identities a prefab document needs, before any of it is written.
 *
 * Two of them, and both have to happen up front rather than as each entity is reached: an
 * entity's record is written after its parent's, so anything recorded on the parent while
 * walking the children lands in the file a version late.
 *
 * - Every nested instance gets an id if it has none. That is the moment a slot in this file
 *   comes into existence, including for instances added or cloned in since the last write.
 *   Deliberately not done on load: an id the file does not carry would differ between
 *   instances of it, and every instance has to agree with the document about which slot is
 *   which.
 *
 * - Every entity inside a nested instance that has no prefab uid is one added here rather
 *   than supplied by that instance's asset - everything from the asset arrived with one. It
 *   is given a uid and listed on the enclosing instance, so that instance's own sync leaves
 *   it alone instead of removing it as something its asset no longer has.
 */
void prepare_prefab_identity(entt::handle obj, entt::handle enclosing_instance, bool is_subtree_root)
{
    if(!obj)
    {
        return;
    }

    if(!is_subtree_root)
    {
        if(auto* prefab_comp = obj.try_get<prefab_component>())
        {
            if(prefab_comp->instance_id.is_nil())
            {
                prefab_comp->instance_id = generate_uuid();
            }

            enclosing_instance = obj;
        }
        else if(enclosing_instance)
        {
            auto& id_comp = obj.get_or_emplace<prefab_id_component>();
            if(id_comp.id.is_nil())
            {
                id_comp.generate_if_nil();
                enclosing_instance.get<prefab_component>().foreign_entities.insert(id_comp.id);
            }
        }
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            prepare_prefab_identity(child, enclosing_instance, false);
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
        id_comp.generate_if_nil();

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
    if(frame.has_named_instances)
    {
        return frame.nested_scopes.end();
    }

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

        // A copy of a nested instance is a different slot in the containing document, so it
        // cannot keep the id naming the one it was copied from - both would answer to it.
        // The clone's own root has no marker at all (should_save_component drops it), so
        // this only reaches instances nested further inside the subtree being cloned.
        if(auto* cloned_prefab = obj.entity.try_get<prefab_component>())
        {
            if(!cloned_prefab->instance_id.is_nil())
            {
                cloned_prefab->instance_id = generate_uuid();
            }
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

    for(size_t i = 0; i < count; ++i)
    {
        // No scratch entity. load_from_archive_impl creates the entities it needs and
        // returns the root; the loaders reassign the handle they are given rather than
        // filling it in, so one handed in here is simply abandoned - a componentless
        // entity per root, on every scene load.
        load_from_archive_impl(ar, reg);
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
            prepare_prefab_identity(mutable_obj, {}, true);
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
    try
    {
        fs::path absolute_key = fs::absolute(fs::resolve_protocol(key));
        fs::error_code err;
        atomic_write_file(
            absolute_key,
            [&](const fs::path& temp)
            {
                save_to_file(temp.string(), obj);
            },
            err);
        return !err;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Failed to save object to file: {0}", e.what());
        return false;
    }
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

} // namespace

/**
 * @brief After a fresh instantiate, attributes what the document states about its directly
 *        nested instances to the document.
 *
 * A replay over an existing instance sets this memo as it goes (apply_nested_override_state).
 * A fresh instantiate runs no replay, so without this the memo is whatever the file happened
 * to store - empty for a prefab authored from a plain object - and every override the prefab
 * authored on its nested instances reads as local until the first resync. That is wrong in the
 * changes view, and wrong for the next replay's local-half computation. On a fresh instance
 * the answer is by definition "all of it": the document is the only author so far.
 *
 * Directly nested only. Deeper instances were refreshed by their own container's sync on the
 * way here, which set their memos relative to *that* container.
 */
void attribute_fresh_nested_memos(entt::handle root)
{
    std::vector<entt::handle> nested;
    collect_nested_instance_roots(root, nested, true);
    for(auto& instance : nested)
    {
        if(auto* prefab_comp = instance.try_get<prefab_component>())
        {
            prefab_comp->inherited_overrides = prefab_comp->property_overrides;
            prefab_comp->inherited_removed_entities = prefab_comp->removed_entities;
            prefab_comp->inherited_removed_instances = prefab_comp->removed_instances;
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

struct scoped_deferred_nested_sync
{
    scoped_deferred_nested_sync() : previous(defer_nested_sync_flag)
    {
        defer_nested_sync_flag = true;
    }

    ~scoped_deferred_nested_sync()
    {
        defer_nested_sync_flag = previous;
    }

    scoped_deferred_nested_sync(const scoped_deferred_nested_sync&) = delete;
    auto operator=(const scoped_deferred_nested_sync&) -> scoped_deferred_nested_sync& = delete;

    bool previous{};
};

auto is_nested_sync_deferred() -> bool
{
    return defer_nested_sync_flag;
}

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
struct nested_override_state
{
    entt::handle root;

    /// How far inside the subtree being replayed this instance sits. Only the first level is
    /// the replaying document's to claim; anything deeper belongs to a document in between.
    int depth{};

    std::set<prefab_property_override_data> scene_only;

    /// The *locally made* half of this instance's removals - the full sets minus what the
    /// containing document stated last time. The document's record replaces the sets outright,
    /// and only this half has to survive it.
    std::set<hpp::uuid> removed_entities;
    std::set<hpp::uuid> removed_instances;
};

void collect_nested_override_state_impl(entt::handle obj,
                                        std::vector<nested_override_state>& out,
                                        bool is_subtree_root,
                                        int depth)
{
    if(!obj)
    {
        return;
    }

    if(!is_subtree_root)
    {
        const auto* prefab_comp = obj.try_get<prefab_component>();

        // Unnamed ones included: a document that names its instances adopts them mid-load,
        // and from that moment its records are replayed over them like any other.
        if(prefab_comp != nullptr)
        {
            ++depth;
            nested_override_state state;
            state.root = obj;
            state.depth = depth;
            std::set_difference(prefab_comp->removed_entities.begin(),
                                prefab_comp->removed_entities.end(),
                                prefab_comp->inherited_removed_entities.begin(),
                                prefab_comp->inherited_removed_entities.end(),
                                std::inserter(state.removed_entities, state.removed_entities.end()));
            std::set_difference(prefab_comp->removed_instances.begin(),
                                prefab_comp->removed_instances.end(),
                                prefab_comp->inherited_removed_instances.begin(),
                                prefab_comp->inherited_removed_instances.end(),
                                std::inserter(state.removed_instances, state.removed_instances.end()));
            std::set_difference(prefab_comp->property_overrides.begin(),
                                prefab_comp->property_overrides.end(),
                                prefab_comp->inherited_overrides.begin(),
                                prefab_comp->inherited_overrides.end(),
                                std::inserter(state.scene_only, state.scene_only.end()));
            out.push_back(std::move(state));
        }
    }

    if(auto* trans_comp = obj.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            collect_nested_override_state_impl(child, out, false, depth);
        }
    }
}

/// Every nested instance under `root`, at any depth - unlike collect_nested_instance_roots,
/// which stops at the first one because each instance's own sync continues from there.
auto collect_nested_override_state(entt::handle root) -> std::vector<nested_override_state>
{
    std::vector<nested_override_state> out;
    collect_nested_override_state_impl(root, out, true, 0);
    return out;
}

/**
 * @brief Re-merges each nested instance's overrides after the containing document replayed.
 *
 * The document's record replaced property_overrides with what it authors, which is the right
 * answer for that half and wrong for the other: edits made here are not in it. They are put
 * back, and what the document authored is remembered separately so the next replay can tell
 * the two apart again.
 */
void apply_nested_override_state(const std::vector<nested_override_state>& states)
{
    for(const auto& state : states)
    {
        if(!state.root)
        {
            // Destroyed by this load: the document stopped mentioning it.
            continue;
        }

        auto* prefab_comp = state.root.try_get<prefab_component>();
        if(prefab_comp == nullptr)
        {
            continue;
        }

        // Only what this document directly contains is its to claim. An instance further in
        // belongs to a document in between, and an override on it that came from further out
        // has to keep looking local from that document's side - otherwise the next replay of
        // the middle document treats it as its own and drops it.
        if(state.depth <= 1)
        {
            prefab_comp->inherited_overrides = prefab_comp->property_overrides;

            // Same memo for removals: what the sets hold right now is exactly what the
            // document stated, and the local halves are about to be unioned back in.
            prefab_comp->inherited_removed_entities = prefab_comp->removed_entities;
            prefab_comp->inherited_removed_instances = prefab_comp->removed_instances;
        }

        prefab_comp->property_overrides.insert(state.scene_only.begin(), state.scene_only.end());

        // Unioned, not diffed. A removal made here has to survive the document's record
        // replacing the set; the cost is that the document cannot bring an entity *back*,
        // which is the safe half of the trade and the rarer request.
        prefab_comp->removed_entities.insert(state.removed_entities.begin(), state.removed_entities.end());
        prefab_comp->removed_instances.insert(state.removed_instances.begin(), state.removed_instances.end());

        // Entities the containing document says this instance no longer has. Its record is a
        // list of what it *contains*, so a deletion inside a nested instance arrives as an
        // addition to removed_entities and nothing else - the entity simply stops being
        // mentioned, which is indistinguishable from the document never having known it.
        //
        // Applied rather than diffed: an entity already gone is not found, so running this
        // every time costs a lookup and settles at the same place.
        if(prefab_comp->removed_entities.empty())
        {
            continue;
        }

        std::vector<entt::handle> to_destroy;
        collect_entities_by_prefab_uid(state.root, prefab_comp->removed_entities, to_destroy, true);
        for(auto& doomed : to_destroy)
        {
            if(doomed)
            {
                scene::destroy_entity(doomed);
            }
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
    const prefab_component overrides_snapshot = *prefab_comp;

    // What the nested instances under this one have been overridden with *here*, as opposed
    // to by the prefab that contains them. The document about to be replayed carries its own
    // authoring for those instances and is entitled to update it - but not to reach past it
    // and undo an edit made in this scene, so those paths are held back.
    //
    // Collected before the load: it rebuilds the subtree, and the answer is about what is
    // here now.
    std::vector<nested_override_state> nested_states = collect_nested_override_state(instance);

    prefab_component local_edits_guard;
    for(const auto& state : nested_states)
    {
        local_edits_guard.property_overrides.insert(state.scene_only.begin(), state.scene_only.end());
    }

    // Any nested instance is reason enough, overrides or not: without the filter the
    // document's snapshot of that instance is replayed in full and reverts everything done
    // to it here.
    const bool needs_override_tracking = !overrides_snapshot.get_all_overrides().empty() ||
                                         !local_edits_guard.property_overrides.empty() ||
                                         !nested_states.empty();

    serialization::path_context path_ctx;
    serialization::path_context* old_ctx = serialization::get_path_context();

    if(needs_override_tracking)
    {
        path_ctx.should_serialize_property_callback =
            [&overrides_snapshot, &local_edits_guard](const std::string& property_path)
        {
            // Edits made here always win, whichever document is being replayed.
            if(local_edits_guard.has_serialization_override(property_path))
            {
                return false;
            }

            auto& load_ctx = get_load_context();
            if(const auto owner = load_ctx.current_nested_owner)
            {
                const auto parts = split_serialization_path(property_path);

                // The instance's own bookkeeping - which asset it is, what it overrides,
                // what it removed - is the containing document's to state, and has to arrive
                // before it can be used to filter anything else.
                if(parts.valid && is_prefab_bookkeeping_component(parts.component_path))
                {
                    return true;
                }

                // Inside a nested instance: the containing document owns only what it
                // overrides there. The rest of its snapshot is the nested asset's business,
                // and replaying it would revert whatever has changed here since.
                // Not a component path at all - one of the container nodes on the way down
                // ("entities[3]/<uuid>/components"). Refusing those would skip everything
                // underneath, filter included.
                if(!parts.valid)
                {
                    return true;
                }

                const auto* owner_prefab = owner.try_get<prefab_component>();
                if(owner_prefab == nullptr)
                {
                    return false;
                }

                // "has_<component>" only says the record carries the component; which of its
                // properties are let through is decided one by one further down. It has to be
                // allowed whenever anything inside that component is overridden, because a
                // property cannot arrive without the component that holds it.
                auto component_path = parts.component_path;
                if(component_path.starts_with("has_"))
                {
                    component_path.remove_prefix(4);
                }

                // The nested root's own placement is the containing document's to state. A
                // local move is an override and the guard above already held it back; anything
                // else goes back to where the container put it - which is what makes reverting
                // such a move mean something.
                const auto* owner_id = owner.try_get<prefab_id_component>();
                if(owner_id != nullptr && owner_id->id == parts.entity_uuid &&
                   is_nested_root_placement_path(component_path))
                {
                    return true;
                }

                return owner_prefab->has_override_touching(parts.entity_uuid, component_path);
            }

            return !overrides_snapshot.has_serialization_override(property_path);
        };
        path_ctx.enable_recording();
        serialization::set_path_context(&path_ctx);
    }

    APPLOG_TRACE("Syncing prefab instance: {}", source.id());

    // The load replaces each nested instance's override set with what the document authors.
    // Refreshing those instances against their own assets before the local half is put back
    // would let the refresh overwrite exactly the properties it was meant to protect.
    const scoped_deferred_nested_sync defer_nested;

    const bool loaded = scene::instantiate_out(*instance.registry(), source, instance, false);
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

    apply_nested_override_state(nested_states);

    // Now that each nested instance knows again what was overridden here, it is safe to bring
    // them up to date with their own assets.
    if(loaded && instance)
    {
        sync_nested_prefab_instances(instance);
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

    bool result = true;

    // copy here to keep it alive
    auto prefab = pfb.get();
    const auto& buffer = prefab->buffer.data;

    if(!buffer.empty())
    {
        // APPLOG_INFO_PERF(std::chrono::microseconds);

        try
        {
            auto ar = ser20::create_iarchive_associative(buffer.data(), buffer.size());
             
            bool pushed = push_load_context(registry);

            {
                // Scopes the prefab-uid mapping to this instance. Records inside the frame
                // resolve against the subtree recorded below and nothing else.
                scoped_instance_frame frame;

                add_to_uid_mapping(obj);

                load_from_archive_start(ar, registry, obj);

                cleanup_uid_mapping();

            }

            // Read before the pop: pop_load_context destroys the context.
            const bool nesting_resolved = get_load_context().nesting_resolved;

            pop_load_context(pushed);


            if(obj)
            {
                auto& pfb_comp = obj.get_or_emplace<prefab_component>();
                pfb_comp.source = pfb;

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
                    attribute_fresh_nested_memos(obj);
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
            obj = load_from_archive_impl(ar, registry);
            const bool nesting_resolved = get_load_context().nesting_resolved;
            pop_load_context(pushed);

            if(obj)
            {
                regenerate_entity_uids(obj);
                auto& pfb_comp = obj.get_or_emplace<prefab_component>();
                pfb_comp.source = pfb;

                // This asset carries a snapshot of anything nested inside it, taken when it
                // was last saved - so it is stale the moment that inner asset is edited.
                // Refresh each nested instance against its own asset, unless a bake has
                // already resolved them and said so.
                if(!nesting_resolved || !can_trust_resolved_marker())
                {
                    sync_nested_prefab_instances(obj);
                }

                // Always a fresh instantiate on this path.
                attribute_fresh_nested_memos(obj);
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

    // The clone's own root is nobody's slot. Its descendants were given fresh ids during the
    // load - they are slots in the subtree being copied, and two entities answering to one id
    // would be as ambiguous as none - but the root is an addition to whatever contains it,
    // and only becomes a slot if that container is written as a prefab.
    if(dst_obj)
    {
        if(auto* prefab_comp = dst_obj.try_get<prefab_component>())
        {
            prefab_comp->instance_id = {};
        }
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
