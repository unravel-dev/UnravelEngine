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

auto ui_document_component::is_enabled() const -> bool
{
    return enabled_;
}

void ui_document_component::set_enabled(bool enabled)
{
    enabled_ = enabled;
    if(document)
    {
        if(enabled)
        {
            document->Show();
        }
        else
        {
            document->Hide();
        }
    }
}

} // namespace unravel