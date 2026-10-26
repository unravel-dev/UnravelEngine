#pragma once
#include <engine/rendering/material.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(material);
LOAD_EXTERN(material);
REFLECT_EXTERN(material);

void save_to_file(const std::string& absolute_path, const std::shared_ptr<material>& obj);
void save_to_file_bin(const std::string& absolute_path, const std::shared_ptr<material>& obj);
void load_from_file(const std::string& absolute_path, std::shared_ptr<material>& obj);
void load_from_file_bin(const std::string& absolute_path, std::shared_ptr<material>& obj);

/// @p attributes plus what every reflected material property carries from its table entry
/// (get_material_property_traits): "material_property" and "per_instance".
inline auto make_material_property_attributes(material_property_id property, entt::attributes attributes)
    -> entt::attributes
{
    attributes.emplace("material_property", property);
    attributes.emplace("per_instance", get_material_property_traits(property).is_per_instance);
    return attributes;
}

} // namespace unravel
