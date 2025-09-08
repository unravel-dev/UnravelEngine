#pragma once
#include <engine/engine_export.h>

#include "../ecs.h"
#include <serialization/associative_archive.h>

namespace unravel
{

/**
 * @struct basic_component
 * @brief Basic component structure that other components can inherit from.
 */
struct basic_component
{
    /// Disable empty type optimizations
    bool eto{};

    /**
     * @brief Marks the component as 'touched'.
     */
    void touch()
    {
    }
};

/**
 * @class owned_component
 * @brief Component that is owned by an entity.
 */
class owned_component : public basic_component
{
public:

    template<typename T>
    static void on_create_component(entt::registry& r, entt::entity e)
    {
        entt::handle entity(r, e);

        auto& component = entity.get<T>();
        component.set_owner(entity);
    }

    template<typename T>
    static void on_destroy_component(entt::registry& r, entt::entity e)
    {
    }

    auto operator=(const owned_component& other) -> owned_component& = default;

    /**
     * @brief Sets the owner of the component.
     * @param owner The entity handle representing the owner.
     */
    void set_owner(entt::handle owner)
    {
        owner_ = owner;
    }

    /**
     * @brief Gets the owner of the component.
     * @return A constant handle to the owner entity.
     */
    [[nodiscard]] auto get_owner() const noexcept -> entt::const_handle
    {
        return owner_;
    }

    /**
     * @brief Gets the owner of the component.
     * @return A handle to the owner entity.
     */
    [[nodiscard]] auto get_owner() noexcept -> entt::handle
    {
        return owner_;
    }

private:
    /// The owner entity handle.
    entt::handle owner_{};
};

/**
 * @struct component_crtp
 * @brief CRTP (Curiously Recurring Template Pattern) base structure for components.
 * @tparam T The derived component type.
 * @tparam Base The base component type, defaults to basic_component.
 */
template<typename T, typename Base = basic_component>
struct component_crtp : Base
{
    /// Indicates if the component can be deleted in place.
    static constexpr bool in_place_delete = true;

    using base = Base;
};


template<typename T>
struct component_meta
{
    static auto exists(entt::handle entity) -> bool
    {
        return entity.all_of<T>();
    }

    static auto add(entt::handle entity) -> bool
    {
        bool has_component = entity.all_of<T>();
        if(has_component)
        {
            return false;
        }
        entity.emplace<T>();
        return true;
    }

    static auto remove(entt::handle entity) -> bool
    {
        size_t removed = entity.remove<T>();
        return removed > 0;
    }
    
    static auto save(entt::handle entity, std::stringstream& stream) -> bool
    {
        try
        {
            auto ar = ser20::create_oarchive_associative(stream);
            ar(ser20::make_nvp("component", entity.get<T>()));
            return true;
        }
        catch(const ser20::Exception& e)
        {
            APPLOG_ERROR("Failed to save component to stream: {}", e.what());
            return false;
        }
    }

    static auto load(entt::handle entity, std::stringstream& stream) -> bool
    {
        try
        {
        auto ar = ser20::create_iarchive_associative(stream);
            ar(ser20::make_nvp("component", entity.get<T>()));
            return true;
        }
        catch(const ser20::Exception& e)
        {
            APPLOG_ERROR("Failed to load component from stream: {}", e.what());
            return false;
        }
        return false;
    }
};

} // namespace unravel
