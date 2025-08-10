#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_handle.h>
#include <engine/ecs/components/basic_component.h>
#include <engine/physics/ecs/components/physics_component.h>

#include <engine/scripting/script.h>
#include <monort/monort.h>

#include <math/math.h>

namespace unravel
{

/**
 * @class script_component
 * @brief Class that contains core data for audio listeners.
 * There can only be one instance of it per scene.
 */
class script_component : public component_crtp<script_component, owned_component>
{
public:
    using scoped_object_ptr = std::shared_ptr<mono::mono_scoped_object>;

    struct script_object_state
    {
        int active{-1};
        bool create_called{};
        bool start_called{};
        bool marked_for_destroy{};
    };

    struct script_object
    {
        script_object() = default;
        script_object(const mono::mono_object& obj)
        {
            scoped = std::make_shared<mono::mono_scoped_object>(obj);
            state = std::make_shared<script_component::script_object_state>();
        }

        auto is_marked_for_destroy() const -> bool
        {
            if(!state)
            {
                return false;
            }
            return state->marked_for_destroy;
        }

        auto is_create_called() const -> bool
        {
            if(!state)
            {
                return false;
            }
            return state->create_called;
        }

        auto is_start_called() const -> bool
        {
            if(!state)
            {
                return false;
            }
            return state->start_called;
        }

        auto is_enabled() const -> bool
        {
            if(!state)
            {
                return false;
            }

            if(state->active == -1)
            {
                return false;
            }
            return state->active != 0;
        }

        auto is_disabled() const -> bool
        {
            if(!state)
            {
                return false;
            }

            if(state->active == -1)
            {
                return false;
            }
            return state->active == 0;
        }

        scoped_object_ptr scoped;
        std::shared_ptr<script_object_state> state;
    };

    using script_components_t = std::vector<script_object>;
    /**
     * @brief Called when the component is created.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_create_component(entt::registry& r, entt::entity e);

    /**
     * @brief Called when the component is destroyed.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_destroy_component(entt::registry& r, entt::entity e);

    void process_pending_deletions();
    void process_pending_creates();
    void process_pending_starts();
    void process_pending_actions();
    void process_pending_actions(script_object script_obj);
    void process_pending_actions_create(script_object script_obj);

    auto add_script_component(const mono::mono_type& type) -> script_object;
    auto add_script_component(const mono::mono_object& obj) -> script_object;
    auto add_script_component(const script_object& obj, bool process_callbacks = true) -> script_object;
    void add_script_components(const script_components_t& comps);
    void add_missing_script_components(const script_components_t& comps);

    auto add_native_component(const mono::mono_type& type) -> script_object;

    auto remove_script_component(const mono::mono_object& obj) -> bool;
    auto remove_script_component(const mono::mono_type& type) -> bool;

    auto remove_native_component(const mono::mono_object& obj) -> bool;
    auto remove_native_component(const mono::mono_type& type) -> bool;

    auto get_script_components(const mono::mono_type& type) -> std::vector<mono::mono_object>;
    auto get_script_component(const mono::mono_type& type) -> script_object;
    auto get_native_component(const mono::mono_type& type) -> script_object;

    auto get_script_components() const -> const script_components_t&;
    auto has_script_components() const -> bool;
    auto has_script_components(const std::string& type_name) const -> bool;

    auto get_script_source_location(const script_object& obj) const -> std::string;

    void create();
    void start();
    void destroy();
    void enable();
    void disable();

    void on_sensor_enter(entt::handle other);
    void on_sensor_exit(entt::handle other);

    void on_collision_enter(entt::handle other, const std::vector<manifold_point>& manifolds, bool use_b);
    void on_collision_exit(entt::handle other, const std::vector<manifold_point>& manifolds, bool use_b);

private:
    void enable(script_object& script_obj, bool check_order);
    void disable(script_object& script_obj, bool check_order);
    void create(script_object& script_obj);
    void start(script_object& script_obj);
    void destroy(script_object& script_obj);
    void set_entity(const mono::mono_object& obj, entt::handle e);
    void on_sensor_enter(const mono::mono_object& obj, entt::handle other);
    void on_sensor_exit(const mono::mono_object& obj, entt::handle other);

    void on_collision_enter(const mono::mono_object& obj,
                            entt::handle other,
                            const std::vector<manifold_point>& manifolds,
                            bool use_b);
    void on_collision_exit(const mono::mono_object& obj,
                           entt::handle other,
                           const std::vector<manifold_point>& manifolds,
                           bool use_b);

    template<typename F>
    auto safe_foreach(script_components_t& components, F&& f)
    {
        auto comps = components;

        for(auto& script : comps)
        {
            f(script);
        }
    }

    script_components_t script_components_;
    script_components_t script_components_to_create_;
    script_components_t script_components_to_start_;

    script_components_t native_components_;
};

} // namespace unravel
