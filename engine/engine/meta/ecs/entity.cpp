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
void add_to_uid_mapping(entt::handle& obj, bool recursive = true)
{
    auto& load_ctx = get_load_context();

    auto id_comp = obj.try_get<prefab_id_component>();
    if(id_comp)
    {
        id_comp->generate_if_nil();
        load_ctx.mapping_by_prefab_uid[id_comp->id].handle = obj;
    }


    if(auto prefab_comp = obj.try_get<prefab_component>())
    {
        for(auto& entity_uuid : prefab_comp->removed_entities)
        {
            load_ctx.mapping_by_prefab_uid[entity_uuid].handle = {};
        }
    }

    if(recursive )
    {
        if(auto trans_comp = obj.try_get<transform_component>())
        {
            for(auto child : trans_comp->get_children())
            {
                add_to_uid_mapping(child, recursive);
            }
        }
    }
}

void cleanup_uid_mapping()
{
    // Unconsumed load stubs. They were never visible to gameplay, so they must not
    // produce pre-destroy notifications on the way out.
    scene::scoped_destroy_suppression no_pre_destroy;

    auto& load_ctx = get_load_context();
    for(auto& [uid, mapping] : load_ctx.mapping_by_prefab_uid)
    {
        if(!mapping.consumed && mapping.handle)
        {
            // APPLOG_TRACE("destroying entity: {}", uid.to_string());
            scene::destroy_entity(mapping.handle);
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

template<typename Archive>
auto load_entity_from_prefab_uid(Archive& ar, entt::handle& obj, entity_flags flags) -> bool
{
    hpp::uuid uid;
    try_load(ar, ser20::make_nvp("prefab_uid", uid));


    auto& load_ctx = get_load_context();
    auto it = load_ctx.mapping_by_prefab_uid.find(uid);
    if(it != load_ctx.mapping_by_prefab_uid.end())
    {
        // APPLOG_TRACE("found in cache entity from uid: {}", uid.to_string());
        obj = it->second.handle;
        it->second.consumed = true;
        return true;
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
        // if we are saving to prefab, we don't want to save the prefab component
        auto& save_ctx = get_save_context();

        if(save_ctx.is_saving_to_prefab())
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
    LOAD_FUNCTION_NAME(ar, e);

    if(e)
    {
        bool pushed = push_entity_path(e);
        obj.components.entity = e;
        try_load(ar, ser20::make_nvp("components", obj.components));
        pop_entity_path(pushed);
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

auto load_from_prefab_out(const asset_handle<prefab>& pfb,
                          entt::registry& registry,
                          entt::handle& obj) -> bool
{
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

            auto& load_ctx = get_load_context();

            add_to_uid_mapping(obj);

            load_from_archive_start(ar, registry, obj);

            cleanup_uid_mapping();

            pop_load_context(pushed);


            if(obj)
            {
                auto& pfb_comp = obj.get_or_emplace<prefab_component>();
                pfb_comp.source = pfb;
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

    src->view<root_component, transform_component>().each(
        [&](auto e, auto&& comp1, auto&& comp2)
        {
            std::stringstream ss;
            save_to_stream(ss, src_scene.create_handle(e));

            load_from(ss, *dst);
        });
}
} // namespace unravel
