#pragma once

#include <engine/meta/ecs/entity.hpp>

namespace unravel
{

/**
 * @brief Saves an entity reference as a link, the way script Entity fields are saved: it is remapped to the copy
 *        when the hierarchy is duplicated or instantiated, and an external target in a prefab is dropped rather than
 *        turned into a stub entity.
 */
template<typename Archive>
void save_entity_reference(Archive& ar, const char* name, const entt::handle& handle)
{
    // Always written, empty when the save names no registry: binary archives are positional.
    ser20::const_entity_handle_link link;
    if(const auto* registry = get_save_context().get_registry())
    {
        link.handle = entt::const_handle(*registry, handle ? handle.entity() : entt::entity(entt::null));
    }
    try_save(ar, ser20::make_nvp(name, link));
}

/// Loads an entity reference saved by save_entity_reference; a missing key leaves @p handle as it is.
template<typename Archive>
void load_entity_reference(Archive& ar, const char* name, entt::handle& handle)
{
    ser20::entity_handle_link link;
    if(try_load(ar, ser20::make_nvp(name, link)))
    {
        handle = link.handle;
    }
}

} // namespace unravel
