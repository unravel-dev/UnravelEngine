#pragma once
#include <engine/engine_export.h>

#include "../ecs.h"

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
auto component_exists(entt::handle entity) -> bool
{
    return entity.all_of<T>();
}

} // namespace unravel
