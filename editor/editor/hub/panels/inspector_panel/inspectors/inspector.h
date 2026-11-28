#pragma once

#include <context/context.hpp>
#include <reflection/reflection.h>
#include <reflection/registration.h>

#include <editor/imgui/integration/imgui.h>

#include <functional>
#include <string>
#include <vector>

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
     * @brief Constructs layout with name and a tooltip given as text: a literal would otherwise take the
     * (name, columns) overload, a pointer converting to bool before it converts to std::string, and lose the tooltip.
     */
    property_layout(const std::string& name, const char* tooltip, bool columns = true);
    

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
     *
     * Every row of a window puts its value at the same x, the window's label column divider, whatever its
     * indentation; a row drawn inside another row's value splits that value on its own. A label wider than its
     * cell ends in an ellipsis.
     * @param auto_proceed_to_next_column Whether to automatically move to next column
     * @param label_reserve Width the caller draws into the label cell after the label (buttons), kept free
     */
    void push_layout(bool auto_proceed_to_next_column = true, float label_reserve = 0.0f);

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
     * @brief Marks a row drawn by name when the value at @p path of the drawn component differs between the
     *        selected entities, as a reflected row is marked by its own path.
     */
    void set_mixed_path(std::string path);

    /**
     * @brief Gets the currently active property layout from the global stack
     * @return Pointer to current layout, or nullptr if stack is empty
     */
    static auto get_current() -> property_layout*;

private:
    /// Opens the row's table, its label column sized to the window's divider, and drags the divider.
    void begin_columns();

    /// The label's context menu (resets, reverts) and tooltip, and the help marker when it fits. A shortened label
    /// gets a tooltip naming it in full.
    void draw_label_menu_and_tooltip(bool show_help_marker, bool is_label_shortened);

    /// Copy and Paste of the row's value in its label menu; nothing for a row drawn by name.
    void draw_value_clipboard_items();

    /// Whether the row is drawn inside the value cell of another row.
    static auto is_inside_value_cell(const property_layout* self) -> bool;

    /// An overridden row's mark: a bar at the window's content edge, in the color of who overrides it, and the
    /// button reverting it in the row's gutter.
    void draw_override_gutter();

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
    /// Whether the row keeps the gutter right of its value for the revert button: every row of a window does,
    /// except rows drawn inside another row's value.
    bool has_gutter_{};
    /// Whether the row stacks its label over its value, as every row of a narrow window does.
    bool stacked_{};
    /// The reflected property the row shows and the object holding it, for its Copy and Paste; empty for a row drawn
    /// by name.
    entt::meta_data prop_{};
    entt::meta_any object_{};
    /// The path a row drawn by name is marked by; empty for none.
    std::string mixed_path_;
};

/**
 * @brief A tinted note above an object's values: @p icon in @p color, a bold @p title over a muted, wrapped
 * @p message, then what @p draw_actions draws under them (the note's buttons). For what holds for the whole object:
 * where an asset comes from, why its values cannot be changed.
 */
void draw_inspector_banner(const char* id,
                           const char* icon,
                           ImU32 color,
                           const std::string& title,
                           const std::string& message,
                           const std::function<void()>& draw_actions = {});

/**
 * @brief What an object's inspector says about one of the rows it draws: that the row's value does not do what it
 * asks for (a warning), or what the value means here. Drawn under the row's value, where the eye is when the value is
 * edited. A warning also colors the row's label and outlines the row's asset field. With a fix, a button under the
 * text runs it.
 */
struct row_note
{
    bool is_warning{true};
    /// Full sentences.
    std::string text;
    /// The button under the text: its label, its tooltip and what it runs. No button without a label.
    std::string fix_label;
    std::string fix_tooltip;
    std::function<void()> fix;
};

/// Whether an asset suits the row that would hold it.
struct asset_fit
{
    enum class state
    {
        /// Not known yet: the asset still loads.
        unknown,
        fits,
        unfit,
    };
    state is{state::unknown};
    /// A sentence saying what it does in the row, or why it does not suit it.
    std::string reason;
};

/// How the picker of an asset row tells the assets that suit the row from the rest.
struct asset_fit_rule
{
    /// What the row needs its asset for, as the picker names the ones that suit: "Particles".
    std::string use;
    /// Whether the asset with this key suits the row.
    std::function<asset_fit(const std::string& key)> get_fit;
};

/**
 * @brief The notes and fit rules of an object's rows, by property name. The object's inspector fills one before it
 * draws the object's properties and keeps it alive while they are drawn: each row then shows its notes, and its asset
 * picker follows its fit rule.
 *
 * The rows that show them are the ones inspect_property draws as one value: an enum, or a type with an inspector of
 * its own (numbers, asset fields, curves). A list, a map, a sub-object and a row a custom drawer takes show none.
 */
class row_notes
{
public:
    /// @param object The object whose rows the notes are about, as the rows hold it.
    explicit row_notes(const void* object);
    ~row_notes();
    row_notes(const row_notes&) = delete;
    auto operator=(const row_notes&) -> row_notes& = delete;

    /// Puts @p note on the row of @p property; a row shows its notes in the order they were added.
    void add(std::string property, row_note note);
    /// Gives the picker of the asset row of @p property its fit rule.
    void set_fit_rule(std::string property, asset_fit_rule rule);
    /// A sentence naming the rows that carry a warning, each name a link that scrolls to its row and drops the
    /// inspector's filter, which may hide it; nothing without a warning. @p type gives the rows their names, with
    /// their group where the name alone does not say it ("Collision > Mode").
    void draw_summary(const entt::meta_type& type) const;

    /// The notes of the row showing @p property of @p object; null without any.
    static auto find_notes(const void* object, const std::string& property) -> const std::vector<row_note>*;
    /// The fit rule of that row; null without one.
    static auto find_fit_rule(const void* object, const std::string& property) -> const asset_fit_rule*;
    /// Whether one of that row's notes is a warning.
    static auto has_warning(const void* object, const std::string& property) -> bool;

private:
    struct row
    {
        std::string property;
        std::vector<row_note> notes;
        hpp::optional<asset_fit_rule> fit_rule;
    };

    auto get_row(std::string property) -> row&;
    static auto find_row(const void* object, const std::string& property) -> const row*;

    const void* object_{};
    std::vector<row> rows_;
};

/**
 * @brief The row inspect_property draws, for what the row's value asks row_notes about: the asset field whether to
 * outline itself and which assets suit it. Scrolls the inspector to the row when a summary's link asked for it.
 */
class current_row_scope
{
public:
    /// @param object The object the row belongs to, @p property the row's, @p value the value the row draws: only
    /// the widget of that value is the row's, not the fields of values nested in it.
    current_row_scope(const void* object, std::string property, const void* value);
    ~current_row_scope();
    current_row_scope(const current_row_scope&) = delete;
    auto operator=(const current_row_scope&) -> current_row_scope& = delete;

    /// Whether @p value is the value of the row being drawn and that row carries a warning.
    static auto has_warning(const void* value) -> bool;
    /// The fit rule of the row being drawn when @p value is its value; null without one.
    static auto get_fit_rule(const void* value) -> const asset_fit_rule*;
    /// Draws the notes of the row being drawn, under its value.
    static void draw_notes();
};

/**
 * @brief Asks the row showing @p property of @p object to take @p value_json as an edit: the row's label menu
 *        cannot write the value, inspect_property does, so the edit records undo and prefab overrides as typing does.
 */
void request_property_paste(const void* object, const entt::meta_data& property, std::string value_json);

/// The value a paste asked the row showing @p property of @p object to take, once.
auto take_property_paste(const void* object, const entt::meta_data& property) -> hpp::optional<std::string>;

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
 * of functions that can navigate: entity -> transform -> position -> x each time.
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
 * @brief While one is alive, inspector edits are written straight to the inspected value instead
 * of through an undo action.
 *
 * For values whose owner records its own undo step: settings staged outside the scene (an asset's
 * import settings before Apply) and documents with their own history (an animation controller).
 * The edit does not mark the scene unsaved.
 */
struct inspect_without_undo_scope
{
    inspect_without_undo_scope();
    ~inspect_without_undo_scope();
    inspect_without_undo_scope(const inspect_without_undo_scope&) = delete;
    auto operator=(const inspect_without_undo_scope&) -> inspect_without_undo_scope& = delete;

    static auto is_active() -> bool;
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

/// The title row a C# [Header] or a C++ "header" attribute puts above a property: bold text on a separator line,
/// after a blank line when rows are drawn above it.
void draw_property_header(const std::string& text, bool has_rows_above);

/// The name of the proxy make_property_proxy makes for `prop` under a proxy named `parent_name`. Undo steps take
/// their proxy's name, and undo and redo point at the property whose name is the step's.
auto make_property_proxy_name(const std::string& parent_name, const entt::meta_data& prop) -> std::string;

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
     * @brief Called when the inspector panel stops showing an object this inspector drew (another
     * selection, a lock change, the panel closing), so per-object state such as staged edits is
     * settled instead of left behind.
     * @param ctx Runtime type information context
     */
    virtual void end_inspection(rtti::context& ctx)
    {
    }

    /**
     * @brief Whether the inspector draws rows of its own rather than one widget in a value cell: an array
     * element it inspects folds out under its label, and its rows line up with every other row.
     */
    virtual auto is_block() const -> bool
    {
        return false;
    }

    /**
     * @brief The object whose reflected properties a block draws as its rows, for the inspector's search; empty
     * when it has none.
     */
    virtual auto get_rows_object(const entt::meta_any& value) const -> entt::meta_any
    {
        return {};
    }

    /**
     * @brief Whether a block draws a row with no reflected property behind it whose name holds `lowercase_text`, for
     * the inspector's search: the rows get_rows_object cannot name.
     */
    virtual auto holds_row_name(const entt::meta_any& value, const std::string& lowercase_text) const -> bool
    {
        return false;
    }

    /// @brief Whether a block draws values something overrides, for the inspector's overridden filter.
    virtual auto has_overridden_rows(const entt::meta_any& value) const -> bool
    {
        return false;
    }

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