#include "input.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

namespace input
{

REFLECT(action_map)
{
    rttr::registration::class_<action_map>("action_map")(rttr::metadata("pretty_name", "Action Map")).constructor<>()()

        /*.property("keyboard_map", &input::action_map::keyboard_map)(rttr::metadata("pretty_name", "Keyboard Map"),
                                                                    rttr::metadata("tooltip", "Missing..."))
        .property("gamepad_map", &input::action_map::gamepad_map)(rttr::metadata("pretty_name", "Gamepad Map"),
                                                                  rttr::metadata("tooltip", "Missing..."))
        .property("mouse_map", &input::action_map::mouse_map)(rttr::metadata("pretty_name", "Mouse Map"),
                                                              rttr::metadata("tooltip", "Missing..."))*/
        ;

    // Register action_map with entt
    entt::meta_factory<action_map>{}
        .type("action_map"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Action Map"},
        });
}

SAVE_INLINE(mouse_action_map::mouse_entry)
{
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("range", obj.range));
    try_save(ar, ser20::make_nvp("value", obj.value));
}

LOAD_INLINE(mouse_action_map::mouse_entry)
{
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("range", obj.range));
    try_load(ar, ser20::make_nvp("value", obj.value));
}

SAVE(mouse_action_map)
{
    try_save(ar, ser20::make_nvp("entries_by_action_id", obj.entries_by_action_id_));
}
SAVE_INSTANTIATE(mouse_action_map, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(mouse_action_map, ser20::oarchive_binary_t);

LOAD(mouse_action_map)
{
    try_load(ar, ser20::make_nvp("entries_by_action_id", obj.entries_by_action_id_));
}
LOAD_INSTANTIATE(mouse_action_map, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mouse_action_map, ser20::iarchive_binary_t);

SAVE_INLINE(gamepad_action_map::gamepad_entry)
{
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("range", obj.range));
    try_save(ar, ser20::make_nvp("value", obj.value));
    try_save(ar, ser20::make_nvp("min_analog_value", obj.min_analog_value));
    try_save(ar, ser20::make_nvp("max_analog_value", obj.max_analog_value));
}

LOAD_INLINE(gamepad_action_map::gamepad_entry)
{
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("range", obj.range));
    try_load(ar, ser20::make_nvp("value", obj.value));
    try_load(ar, ser20::make_nvp("min_analog_value", obj.min_analog_value));
    try_load(ar, ser20::make_nvp("max_analog_value", obj.max_analog_value));
}

SAVE(gamepad_action_map)
{
    try_save(ar, ser20::make_nvp("entries_by_action_id", obj.entries_by_action_id_));
}
SAVE_INSTANTIATE(gamepad_action_map, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gamepad_action_map, ser20::oarchive_binary_t);

LOAD(gamepad_action_map)
{
    try_load(ar, ser20::make_nvp("entries_by_action_id", obj.entries_by_action_id_));
}
LOAD_INSTANTIATE(gamepad_action_map, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gamepad_action_map, ser20::iarchive_binary_t);

SAVE_INLINE(keyboard_action_map::key_entry)
{
    try_save(ar, ser20::make_nvp("key", obj.key));
    try_save(ar, ser20::make_nvp("modifiers", obj.modifiers));
    try_save(ar, ser20::make_nvp("analog_value", obj.analog_value));
}

LOAD_INLINE(keyboard_action_map::key_entry)
{
    try_load(ar, ser20::make_nvp("key", obj.key));
    try_load(ar, ser20::make_nvp("modifiers", obj.modifiers));
    try_load(ar, ser20::make_nvp("analog_value", obj.analog_value));
}

SAVE(keyboard_action_map)
{
    try_save(ar, ser20::make_nvp("entries_by_action_id", obj.entries_by_action_id_));
}
SAVE_INSTANTIATE(keyboard_action_map, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(keyboard_action_map, ser20::oarchive_binary_t);

LOAD(keyboard_action_map)
{
    try_load(ar, ser20::make_nvp("entries_by_action_id", obj.entries_by_action_id_));
}
LOAD_INSTANTIATE(keyboard_action_map, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(keyboard_action_map, ser20::iarchive_binary_t);

SAVE(action_map)
{
    try_save(ar, ser20::make_nvp("keyboard_map", obj.keyboard_map));
    try_save(ar, ser20::make_nvp("gamepad_map", obj.gamepad_map));
    try_save(ar, ser20::make_nvp("mouse_map", obj.mouse_map));
}
SAVE_INSTANTIATE(action_map, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(action_map, ser20::oarchive_binary_t);

LOAD(action_map)
{
    try_load(ar, ser20::make_nvp("keyboard_map", obj.keyboard_map));
    try_load(ar, ser20::make_nvp("gamepad_map", obj.gamepad_map));
    try_load(ar, ser20::make_nvp("mouse_map", obj.mouse_map));
}
LOAD_INSTANTIATE(action_map, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(action_map, ser20::iarchive_binary_t);

// void save_to_file(const std::string& absolute_path, const settings& obj)
// {
//     std::ofstream stream(absolute_path);
//     if(stream.good())
//     {
//         auto ar = ser20::create_oarchive_associative(stream);
//         try_save(ar, ser20::make_nvp("settings", obj));
//     }
// }

// void save_to_file_bin(const std::string& absolute_path, const settings& obj)
// {
//     std::ofstream stream(absolute_path, std::ios::binary);
//     if(stream.good())
//     {
//         ser20::oarchive_binary_t ar(stream);
//         try_save(ar, ser20::make_nvp("settings", obj));
//     }
// }

// auto load_from_file(const std::string& absolute_path, settings& obj) -> bool
// {
//     std::ifstream stream(absolute_path);
//     if(stream.good())
//     {
//         auto ar = ser20::create_iarchive_associative(stream);
//         return try_load(ar, ser20::make_nvp("settings", obj));
//     }

//     return false;
// }

// auto load_from_file_bin(const std::string& absolute_path, settings& obj) -> bool
// {
//     std::ifstream stream(absolute_path, std::ios::binary);
//     if(stream.good())
//     {
//         ser20::iarchive_binary_t ar(stream);
//         return try_load(ar, ser20::make_nvp("settings", obj));
//     }

//     return false;
// }
} // namespace input
