#pragma once
#include <engine/rendering/gi/lumen_mesh_cards.h>

#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(lumen_card);
LOAD_EXTERN(lumen_card);

SAVE_EXTERN(lumen_mesh_cards);
LOAD_EXTERN(lumen_mesh_cards);

} // namespace unravel
