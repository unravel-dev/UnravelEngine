#pragma once

#include <engine/rendering/render_feature_support.h>
#include <reflection/reflection.h>

#include <string>
#include <utility>
#include <vector>

namespace unravel
{

/// Meta attribute of a component type holding its @ref component_disable_rules.
constexpr const char* COMPONENT_DISABLES_ATTRIBUTE = "disables";

/// Meta attribute of a component type holding the render_feature it asks for: the inspector says on the
/// component when the device does not run it.
constexpr const char* COMPONENT_RENDER_FEATURE_ATTRIBUTE = "render_feature";

/**
 * @brief One component whose work another component takes over, and what to tell the user about it.
 *
 * The component that takes over declares the rule, so the knowledge lives next to the behavior that
 * causes it: the pipeline skips the disabled pass because this component runs. The inspector reads
 * the rules of every component on an entity and shows the messages on the components they name,
 * without either side knowing the other's type.
 */
struct component_disable_rule
{
    /// entt::type_id hash of the component that stops having an effect.
    entt::id_type disabled_type{};
    /// Reads the component that declared the rule and answers whether it takes over right now.
    /// Empty means the rule applies whenever that component is present.
    entt::property_predicate_t<bool> is_active;
    /// Shown in the disabled component's inspector while @ref is_active holds.
    std::string message;
};

using component_disable_rules = std::vector<component_disable_rule>;

/**
 * @brief Builds a rule naming @p Disabled as the component that stops having an effect.
 * @param message What the disabled component's inspector shows.
 * @param is_active Reads the declaring component; omitted means the rule always applies.
 */
template<typename Disabled>
auto make_component_disable_rule(std::string message, entt::property_predicate_t<bool> is_active = {})
    -> component_disable_rule
{
    return component_disable_rule{entt::type_id<Disabled>().hash(), std::move(is_active), std::move(message)};
}

/// The rules a component type declares; empty when it declares none.
inline auto get_component_disable_rules(const entt::meta_type& type) -> const component_disable_rules&
{
    static const component_disable_rules none;
    const auto* rules = entt::get_attribute(type, COMPONENT_DISABLES_ATTRIBUTE).try_cast<component_disable_rules>();
    return rules != nullptr ? *rules : none;
}

/// What the inspector shows on a component of @p type whose rendering feature the device does not run; empty
/// when it runs or the type names no feature.
inline auto get_component_device_note(const entt::meta_type& type) -> std::string
{
    const auto attribute = entt::get_attribute(type, COMPONENT_RENDER_FEATURE_ATTRIBUTE);
    const auto* feature = attribute.try_cast<render_feature>();
    return feature != nullptr ? get_render_feature_support().get_hint(*feature) : std::string{};
}

/// Whether @p rule applies to @p source, the instance of the component that declared it.
inline auto is_component_disable_rule_active(const component_disable_rule& rule, const entt::meta_any& source) -> bool
{
    return rule.is_active ? rule.is_active(source) : true;
}

} // namespace unravel
