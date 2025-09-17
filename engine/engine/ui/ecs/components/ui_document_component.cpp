#include "ui_document_component.h"

#include <RmlUi/Core/ElementDocument.h>

namespace unravel
{

// The ui_document_component is now primarily a data container.
// All loading/unloading logic is handled by the ui_system in its update loop.

auto ui_document_component::is_loaded() const -> bool
{
    return document != nullptr;
}

auto ui_document_component::is_visible() const -> bool
{
    return document && document->IsVisible();
}

} // namespace unravel