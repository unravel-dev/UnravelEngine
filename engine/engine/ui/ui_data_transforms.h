#pragma once

#include <RmlUi/Core/Types.h>
#include <entt/entity/fwd.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Rml
{
class Context;
}

namespace unravel
{

/**
 * @brief Serves a data transform call ({{ value | name }}) for a document's entity.
 * @param owner The entity of the document.
 * @param name The transform name.
 * @param arguments The value, then the transform's arguments.
 * @param result Receives the transformed value.
 * @return false when the entity has no such transform: the value then shows unchanged.
 */
using ui_transform_handler = std::function<
    bool(entt::entity owner, const std::string& name, const Rml::VariantList& arguments, Rml::Variant& result)>;

/**
 * @brief Sets who serves data transforms (scripting); without one every transform passes values through.
 */
void set_ui_transform_handler(ui_transform_handler handler);

/**
 * @brief Registers transform @p name in a document's context, once per context. Calls go to the
 * handler; until it serves the name, the value passes through instead of failing the expression.
 * Does nothing for a context without data models or for RmlUi's built-in transforms.
 */
void ensure_ui_transform(Rml::Context& context, uint64_t context_generation, entt::entity owner, const std::string& name);

/**
 * @brief The transform names a document's markup uses after a '|' (built-in transforms excluded).
 */
auto collect_ui_transform_names(const std::string& rml) -> std::vector<std::string>;

} // namespace unravel
