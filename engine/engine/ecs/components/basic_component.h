#pragma once
#include <engine/engine_export.h>

#include "../ecs.h"
#include <serialization/associative_archive.h>

namespace unravel
{

/**
 * @brief Consumer slots for the indexed dirty flags a component carries
 * (is_dirty(id) / set_dirty(id, ...)).
 *
 * ONE numbering, shared by every component that has them, so a slot means the same thing
 * wherever it is read: a consumer that watches an entity's transform AND its model asks both
 * for the same id. A component only ever sets the slots that apply to it, and the rest stay
 * clean. Register new consumers here rather than per component, so slots cannot collide - the
 * bitset has 32 of them.
 */
struct dirty_ids
{
    enum : uint8_t
    {
        /// Physics backend transform sync (see the physics backends). On transform_component.
        physics = 1,
        /// Model pose refresh: submesh/bone poses and cached render-proxy bounds
        /// (see model_component::update_armature). On transform_component.
        model_pose = 2,
        /// The same refresh, consumed on the MODEL'S OWN entity for submeshes the owner
        /// places directly (meshes without an armature node for them). A separate slot
        /// from model_pose: an entity can be an armature node of one model and the owner
        /// of another, and the two refreshes run in parallel. On transform_component.
        model_owner_pose = 3,
        /// Velocity (motion vector) mover detection: consumed once per render frame by
        /// model_system's before-render promotion (model_component::record_velocity_state).
        /// A set bit means the world transform changed since the last consumption, so the
        /// entity is drawn into the velocity buffer with per-object motion this frame.
        /// On transform_component.
        velocity = 4,
        /// Shadow caster cache invalidation: consumed once per render frame by
        /// model_system::on_frame_before_render, from BOTH components - the transform says
        /// where a caster is, the model what it renders as (membership flags and world
        /// bounds), and no transform change reports the latter. A set bit on a STATIC caster
        /// retires the cached caster lists (see shadow_caster_revision).
        shadow_caster = 5,
    };
};

/// Initial value of those flags: a component nothing has consumed from yet must never look
/// clean. The entt create hook re-arms them anyway; this also covers paths that assign over
/// an existing component (deserialization, cloning), which would otherwise copy the source's
/// already-consumed flags.
static constexpr unsigned long long ALL_CONSUMERS_DIRTY = ~0ULL;

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
