#pragma once

#include <context/context.hpp>
#include <entt/entity/handle.hpp>
#include <entt/meta/meta.hpp>
#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>
#include <uuid/uuid.h>

#include <string>
#include <vector>

namespace unravel
{

struct composite_action_t;

/**
 * @brief Copy and paste of components and of single property values through the OS clipboard's text.
 *
 * A copied component is what the component's own save writes (a script: what the per-script save writes), so pasting
 * it is the component's own load, and undo, prefab overrides and the unsaved flag follow as for any edit. A copied
 * value is what the MCP tools read and write for a property, so a value copied in the Inspector pastes into an MCP
 * call and back.
 */
namespace component_clipboard
{

/// One component of a copy: a native component by its reflected type, or a script by its full C# type name.
struct copied_component
{
    /// The component's type; empty for a script.
    entt::meta_type type{};
    /// A script's full C# type name; empty for a native component.
    std::string script_type{};
    /// What menus call it: the component's pretty name, the script's type name.
    std::string name{};
    /// What the component's save wrote; for a script, what the per-script save wrote.
    std::string state{};

    auto is_script() const -> bool
    {
        return !script_type.empty();
    }
};

/// Whether a component may be copied: not those every entity has (id, name, layers) or keeps for its prefab.
auto is_copyable(const entt::meta_type& type) -> bool;

/// The copy document of one component of @p entity; empty when it has none of that type.
auto copy_component(entt::handle entity, const entt::meta_type& type) -> std::string;

/// The copy document of the script in @p slot; empty when there is none.
auto copy_script(entt::handle entity, const hpp::uuid& slot) -> std::string;

/// The copy document of every copyable component and script of @p entity.
auto copy_all_components(entt::handle entity) -> std::string;

/// The components a copy document holds; empty when @p text is none.
auto read_components(hpp::string_view text) -> std::vector<copied_component>;

/// The components the OS clipboard holds.
auto clipboard_components() -> std::vector<copied_component>;

/// The first component of the OS clipboard that is @p type (or, for a script, of @p script_type).
auto find_clipboard_component(const entt::meta_type& type, const std::string& script_type = {}) -> hpp::optional<copied_component>;

/**
 * @brief Adds to @p edits the steps that load a copied component's values into the component of each target that has
 *        it, for a caller that groups several pastes into one undo step. Native components are loaded at once: the
 *        steps find the edit made, as the actions recording an edit made through setters do.
 */
void add_paste_values_steps(composite_action_t& edits,
                            const std::vector<entt::handle>& targets,
                            const copied_component& item,
                            const hpp::uuid& script_slot = {});

/**
 * @brief Loads a copied component's values into the component of each target that has it, as one undo step.
 *
 * A script's values go into @p script_slot when it is set (the script the menu was opened on), else into each
 * target's first script of that type.
 * @return How many components took the values.
 */
auto paste_values(rtti::context& ctx,
                  const std::vector<entt::handle>& targets,
                  const copied_component& item,
                  const hpp::uuid& script_slot = {}) -> size_t;

/**
 * @brief Adds each copied component to each target with its values, as one undo step. A native component a target
 *        has already is left as it is there; a script is always added, as another of its type may be.
 * @return How many components were added.
 */
auto paste_as_new(rtti::context& ctx, const std::vector<entt::handle>& targets, const std::vector<copied_component>& items)
    -> size_t;

/// Puts @p document on the OS clipboard; nothing for an empty one.
void set_clipboard(const std::string& document);

/**
 * @brief The copy document of one property value: what the MCP tools read and write for a property of that type.
 * @param value_json The value as JSON.
 * @param type_name The value's type, as entt names it.
 */
auto make_value_document(const std::string& value_json, hpp::string_view type_name, hpp::string_view label) -> std::string;

/**
 * @brief The JSON of the value the OS clipboard holds for a property of @p type_name: a copied value of that type,
 *        or text that is JSON itself (a number, a string in quotes, an array), or else the text as a JSON string.
 *        Nothing for a copied value of another type or an empty clipboard.
 */
auto clipboard_value_json(hpp::string_view type_name) -> hpp::optional<std::string>;

} // namespace component_clipboard
} // namespace unravel
