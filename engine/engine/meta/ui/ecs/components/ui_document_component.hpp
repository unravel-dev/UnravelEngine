#pragma once
#include <engine/ui/ecs/components/ui_document_component.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(ui_document_component);
LOAD_EXTERN(ui_document_component);
REFLECT_EXTERN(ui_document_component);
} // namespace unravel
