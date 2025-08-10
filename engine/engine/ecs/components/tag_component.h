#pragma once
#include <engine/engine_export.h>

#include <string>

namespace unravel
{

/**
 * @struct tag_component
 * @brief Component that provides a tag (name or label) for an entity.
 */
struct tag_component
{
    /**
     * @brief The name of the entity.
     */
    std::string name{};
    /**
     * @brief The tag of the entity.
     */
    std::string tag{};
};

} // namespace unravel
