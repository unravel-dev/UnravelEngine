#pragma once
#include <engine/scripting/ecs/components/script_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

#include <optional>
#include <string>
#include <string_view>

namespace ser20
{
    SAVE_EXTERN(dotnet::object);
    LOAD_EXTERN(dotnet::object);
}

namespace unravel
{
/// The key a saved script keeps its slot under, beside its type. Not a C# identifier, so it
/// cannot collide with a field of the script.
inline constexpr const char* SCRIPT_SLOT_KEY = "@slot";

/**
 * @brief How a script is named in a property path: "[<slot>]" after "script_components".
 *
 * By slot rather than position, so an override follows the script wherever it sits in the
 * list - with several scripts of one type, and after another script is removed.
 */
auto script_slot_segment(const hpp::uuid& slot) -> std::string;

/// The slot a script property path names ("script_components[<slot>]/..."), if it names one.
auto parse_script_slot(hpp::string_view property_path) -> std::optional<hpp::uuid>;

SAVE_EXTERN(script_component);
LOAD_EXTERN(script_component);
REFLECT_EXTERN(script_component);

LOAD_EXTERN(script_component::script_object);
SAVE_EXTERN(script_component::script_object);

auto save_to_stream(std::ostream& stream, entt::const_handle e, const script_component::script_object& obj) -> bool;
auto load_from_stream(std::istream& stream, entt::handle e, script_component::script_object& obj) -> bool;


} // namespace unravel
