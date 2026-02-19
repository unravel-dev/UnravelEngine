#include "ui_document_component.h"

#include <RmlUi/Core/ElementDocument.h>

namespace unravel
{

// The ui_document_component is now primarily a data container.
// All loading/unloading logic is handled by the ui_system in its update loop.

auto ui_document_component::get_world_space_scale() const -> math::vec2
{
    return {static_cast<float>(size.width) / pixels_per_world_unit, static_cast<float>(size.height) / pixels_per_world_unit};
}


auto ui_document_component::get_bounds() const -> math::bbox
{
    auto scale = get_world_space_scale();
    math::bbox bbox;
    bbox.min.x = -scale.x * 0.5f;
    bbox.min.y = scale.y * 0.5f;
    bbox.min.z = 0;
    bbox.max.x = scale.x * 0.5f;
    bbox.max.y = -scale.y * 0.5f;
    bbox.max.z = 0.001f;
    return bbox;
}
auto ui_document_component::is_loaded() const -> bool
{
    return document != nullptr;
}

auto ui_document_component::is_enabled() const -> bool
{
    return enabled_;
}

void ui_document_component::set_enabled(bool enabled)
{
    enabled_ = enabled;
}

} // namespace unravel