#pragma once

#include <engine/rendering/material_instance.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(material_instance);
LOAD_EXTERN(material_instance);
REFLECT_EXTERN(material_instance);

/**
 * @brief Whether the path filter in force lists the material instance at the current path in part: some of its
 *        overrides, as editing several entities at once loads one edited value, and not the instance as a whole.
 *
 * Such a load merges what it lists into the instance it loads over: a listed override the document has is set, one
 * it lacks is reverted, and every other override stays.
 */
auto is_instance_listed_in_part() -> bool;

/**
 * @brief Reverts the overrides of @p instance that the path filter in force lists under the current path: what a
 *        load listing them in part does when the document has no instance there (a slot without overrides).
 */
void revert_listed_overrides(material_instance& instance);

} // namespace unravel
