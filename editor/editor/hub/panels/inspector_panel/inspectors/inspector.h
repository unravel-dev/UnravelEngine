#pragma once

#include <context/context.hpp>
#include <reflection/reflection.h>
#include <reflection/registration.h>

#include <editor/imgui/integration/imgui.h>

namespace unravel
{

struct property_layout_group
{
    property_layout_group(const std::string& name);
    ~property_layout_group();
};
/**
 * @brief Manages ImGui layout for property inspection in the editor
 * 
 * Handles the visual layout of properties in the inspector panel, including
 * column-based layouts, tree nodes, tooltips, and context menus. Uses RAII
 * to automatically manage ImGui state.
 */
class property_layout
{
public:
    /// Disable copy operations due to RAII nature and global stack management
    property_layout(const property_layout&) = delete;
    auto operator=(const property_layout&) -> property_layout& = delete;
    
    /// Disable move operations to prevent stack corruption
    property_layout(property_layout&&) = delete;
    auto operator=(property_layout&&) -> property_layout& = delete;
    /**
     * @brief Default constructor that registers this layout in the global stack
     */
    property_layout();

    /**
     * @brief Constructs layout from meta property data
     * @param prop Meta property containing name and tooltip information
     * @param columns Whether to use column-based layout (default: true)
     */
    property_layout(const entt::meta_data& prop, const entt::meta_any& object, bool columns = true);
    
    /**
     * @brief Constructs layout with property name only
     * @param name Display name for the property
     * @param columns Whether to use column-based layout (default: true)
     */
    property_layout(const std::string& name, bool columns = true);
    
    /**
     * @brief Constructs layout with name and tooltip
     * @param name Display name for the property
     * @param tooltip Help text shown on hover
     * @param columns Whether to use column-based layout (default: true)
     */
    property_layout(const std::string& name, const std::string& tooltip, bool columns = true);
    

        /**
     * @brief Constructs layout with name, tooltip and hint
     * @param name Display name for the property
     * @param tooltip Help text shown on hover
     * @param columns Whether to use column-based layout (default: true)
     */
     property_layout(const std::string& name, const std::string& tooltip, const std::string& hint, bool columns = true);
    
    /**
     * @brief Constructs layout with custom rendering callback
     * @param name Display name for the property
     * @param callback Custom function to render the property label
     * @param columns Whether to use column-based layout (default: true)
     */
    property_layout(const std::string& name, const std::function<void()>& callback, bool columns = true);

    /**
     * @brief Destructor that cleans up ImGui state and removes from stack
     */
    ~property_layout();

    /**
     * @brief Updates layout data from meta property
     * @param prop Meta property containing name and tooltip
     * @param columns Whether to use column-based layout
     */
    void set_data(const entt::meta_data& prop, const entt::meta_any& object, bool columns = true);
    
    /**
     * @brief Updates layout data with name and tooltip
     * @param name Display name for the property
     * @param tooltip Help text shown on hover
     * @param columns Whether to use column-based layout
     */
    void set_data(const std::string& name, const std::string& tooltip = "", const std::string& hint = "", bool columns = true);

    /**
     * @brief Initializes ImGui layout with tables and property label
     * @param auto_proceed_to_next_column Whether to automatically move to next column
     */
    void push_layout(bool auto_proceed_to_next_column = true);
    
    /**
     * @brief Creates a collapsible tree node layout for nested properties
     * @param flags ImGui tree node flags for customization
     * @return true if tree node is open and children should be rendered
     */
    auto push_tree_layout(ImGuiTreeNodeFlags flags = 0, bool default_open = true) -> bool;
    
    /**
     * @brief Cleans up ImGui state (IDs, tables, tree nodes)
     */
    void pop_layout();

    /**
     * @brief Prepares ImGui for rendering the property value widget
     */
    void prepare_for_item();

    /**
     * @brief Gets the currently active property layout from the global stack
     * @return Pointer to current layout, or nullptr if stack is empty
     */
    static auto get_current() -> property_layout*;

private:
    hpp::optional<property_layout_group> group_;
    /// Whether ImGui state has been pushed and needs cleanup
    bool pushed_{};
    /// Display name for the property
    std::string name_;
    /// Hint text shown on hover
    std::string hint_;
    /// Help text shown on hover
    std::string tooltip_;
    /// Custom callback for rendering property label
    std::function<void()> callback_;
    /// Whether to use column-based layout
    bool columns_{};
    /// Whether tree node is currently open
    bool open_{};
    /// Whether ImGui table is currently active
    bool columns_open_{};
};

/**
 * @brief Metadata about a variable being inspected
 */
struct var_info
{
    /// Whether the variable should be displayed as read-only
    bool read_only{};
    /// Whether this is a property that can be overridden in prefabs
    bool is_property{};

    bool is_copyable{true};
};

/**
 * @brief Result of an inspection operation indicating what changes occurred
 */
struct inspect_result
{
    /// Whether the value was modified during inspection
    bool changed{};
    /// Whether user finished editing (e.g., released mouse or pressed enter)
    bool edit_finished{};
    /// Whether the change was recorded for undo/redo system
    bool change_recorded{};

    /**
     * @brief Combines this result with another using logical OR
     * @param rhs Other result to combine with
     * @return Reference to this result after combination
     */
    auto operator|=(const inspect_result& rhs) -> inspect_result&
    {
        changed |= rhs.changed;
        edit_finished |= rhs.edit_finished;
        change_recorded |= rhs.change_recorded;
        return *this;
    }

    /**
     * @brief Creates new result by combining two results with logical OR
     * @param rhs Other result to combine with
     * @return New combined result
     */
    auto operator|(const inspect_result& rhs) const -> inspect_result
    {
        inspect_result result{};
        result.changed |= rhs.changed;
        result.edit_finished |= rhs.edit_finished;
        result.change_recorded |= rhs.change_recorded;
        return result;
    }
};

/**
 * @brief Safe deferred property access proxy for arbitrary object properties
 * 
 * Instead of storing direct references or pointers to properties (which can become invalid
 * when objects are destroyed, moved, or reorganized), this proxy stores the "path" or "recipe"
 * to reach a property. Each access re-evaluates the path from scratch, providing:
 * 
 * - Lifetime safety: No dangling pointers when objects are destroyed/moved
 * - Graceful failure: Invalid paths return false rather than crashing
 * - Composability: Proxies can be chained to access deeply nested properties
 * - Undo/redo safety: Actions store stable paths, not volatile references
 * 
 * Example: Instead of storing `&entity.transform.position.x`, we store a chain
 * of functions that can navigate: entity → transform → position → x each time.
 */
struct meta_any_proxy
{
    /**
     * @brief Implementation containing the deferred access functions
     * 
     * These functions are evaluated fresh on each property access, ensuring
     * the property path is validated and navigated safely every time.
    */
    struct meta_any_proxy_impl
    {
        
        /// Points to the root proxy's impl in a proxy chain.
        /// Allows detaching the root from any proxy in the chain
        /// (e.g., from the leaf proxy stored in an action).
        std::shared_ptr<meta_any_proxy_impl> parent;

        std::string type_name;

        std::string name;


        auto get(entt::meta_any& result) -> bool
        {
            return getter(result);
        }

        auto set(meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) -> bool
        {
            return setter(proxy, value, execution_count);
        }

        void detach()
        {
            if(resolver)
            {
                getter = resolver;
            }

            if(parent)
            {
                parent->detach();
            }
        }


        std::function<bool(entt::meta_any&)> resolver;

        /// Function to retrieve the current value by evaluating the property path
        /// Returns false if any step in the path is invalid (safe failure)
        std::function<bool(entt::meta_any&)> getter;
        
        /// Function to set a new value by navigating the property path
        /// execution_count tracks undo/redo cycles to avoid duplicate recordings
        std::function<bool(meta_any_proxy& proxy, const entt::meta_any&, uint64_t execution_count)> setter;

    };

    /// Shared implementation allows copying proxies without duplicating the path logic
    /// Multiple proxies can safely reference the same property access method
    std::shared_ptr<meta_any_proxy_impl> impl = std::make_shared<meta_any_proxy_impl>();


    /// Detach the root proxy, switching from reference-based to resolver-based access.
    /// Safe to call multiple times; no-op if no resolver is set.
    void detach()
    {
        impl->detach();
    }
};

/**
 * @brief Creates a simple proxy for direct variable access
 * @param var Variable to create proxy for
 * @return Proxy that provides direct access to the variable
 */
auto make_proxy(entt::meta_any& var, const std::string& name = {}) -> meta_any_proxy;

/**
 * @brief Like make_proxy, but for entt::handle roots resolved by id_component UUID each access.
 *
 * Survives entity destruction/recreation when the recreated row keeps the same id_component id.
 * Falls back to make_proxy for non-handles, empty handles, or entities without id_component.
 */
auto make_entity_proxy(entt::meta_any& var, const std::string& name = {}) -> meta_any_proxy;

auto make_property_proxy(const meta_any_proxy& var_proxy, const entt::meta_data& prop) -> meta_any_proxy;

/**
 * @brief Base class for type-specific property inspectors in the editor
 * 
 * Inspectors provide custom UI for editing different data types in the
 * inspector panel. Each inspector handles rendering and interaction for
 * a specific type (e.g., floats, vectors, textures).
 */
struct inspector : crtp_meta_type<inspector>
{
    /**
     * @brief Factory method to create and register inspector instances
     * @tparam T Inspector type to create
     * @param inspected_type Type that this inspector handles
     * @param type_map Registry map to store the inspector instance
     */
    template<typename T>
    static void create_and_register(const entt::meta_type& inspected_type,
                                    std::unordered_map<entt::id_type, std::shared_ptr<inspector>>& type_map)
    {
        type_map[inspected_type.info().index()] = std::make_shared<T>();
    }

    /// Function type for retrieving meta attributes by name
    using attribute_getter = std::function<entt::meta_any(const char*)>;

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~inspector() = default;

    /**
     * @brief Called before inspecting a property to set up layout
     * @param prop Meta property being inspected
     * @param object Object being inspected
     */
    virtual void before_inspect(const entt::meta_data& prop, const entt::meta_any& object);
    
    /**
     * @brief Called after inspecting a property to clean up layout
     * @param prop Meta property that was inspected
     * @param object Object being inspected
     */
    virtual void after_inspect(const entt::meta_data& prop, const entt::meta_any& object);
    
    /**
     * @brief Pure virtual method to render and handle interaction for a variable
     * @param ctx Runtime type information context
     * @param var Variable being inspected
     * @param var_proxy Proxy for accessing the variable
     * @param info Metadata about the variable
     * @param custom Custom attributes for the variable
     * @return Result indicating what changes occurred
     */
    virtual auto inspect(rtti::context& ctx,
                             entt::meta_any& var,
                             const meta_any_proxy& var_proxy,
                             const var_info& info,
                             const entt::meta_custom& custom) -> inspect_result = 0;

protected:
    /// Layout manager for this inspector's UI
    std::unique_ptr<property_layout> layout_;
    /// Whether this inspector's UI section is expanded
    bool open_{};
};

/**
 * @brief Reflection registration for the base inspector class
 */
REFLECT_INLINE(inspector)
{
    entt::meta_factory<inspector>{}.type("inspector"_hs);
}

/**
 * @brief Macro to register an inspector inline with its inspected type
 * @param inspector_type The inspector class to registerb
 * @param inspected_type The type this inspector handles
 */
#define REFLECT_INSPECTOR_INLINE(inspector_type, inspected_type)                                                       \
    REFLECT_INLINE(inspector_type)                                                                                     \
    {                                                                                                                  \
        entt::meta_factory<inspector_type>{}                                                                           \
            .type(entt::hashed_string{#inspector_type})                                                                \
            .custom<entt::attributes>(                                                                                 \
                entt::attributes{entt::attribute{"inspected_type", entt::resolve<inspected_type>()}})                  \
            .base<inspector>()                                                                                         \
            .func<&inspector::create_and_register<inspector_type>>("create_and_register"_hs);                          \
    }

/**
 * @brief Macro to register an inspector with its inspected type
 * @param inspector_type The inspector class to register
 * @param inspected_type The type this inspector handles
 */
#define REFLECT_INSPECTOR(inspector_type, inspected_type)                                                              \
    REFLECT(inspector_type)                                                                                            \
    {                                                                                                                  \
        entt::meta_factory<inspector_type>{}                                                                           \
            .type(entt::hashed_string{#inspector_type})                                                                \
            .custom<entt::attributes>(                                                                                 \
                entt::attributes{entt::attribute{"inspected_type", entt::resolve<inspected_type>()}})                  \
            .base<inspector>()                                                                                         \
            .func<&inspector::create_and_register<inspector_type>>("create_and_register"_hs);                          \
    }

} // namespace unravel