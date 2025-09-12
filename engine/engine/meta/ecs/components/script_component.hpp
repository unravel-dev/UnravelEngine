#pragma once
#include <engine/scripting/ecs/components/script_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(script_component);
LOAD_EXTERN(script_component);
REFLECT_EXTERN(script_component);

LOAD_EXTERN(script_component::script_object);
SAVE_EXTERN(script_component::script_object);

auto save_to_stream(std::ostream& stream, entt::const_handle e, const script_component::script_object& obj) -> bool;
auto load_from_stream(std::istream& stream, entt::handle e, script_component::script_object& obj) -> bool;


} // namespace unravel
