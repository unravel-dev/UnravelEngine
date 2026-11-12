#pragma once

#include <engine/rendering/mesh_shape.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

#include <string>

namespace unravel
{
SAVE_EXTERN(mesh_shape);
LOAD_EXTERN(mesh_shape);
REFLECT_EXTERN(mesh_shape);

/// Writes `obj` as the text an .emesh file holds.
void save_to_file(const std::string& absolute_path, const mesh_shape& obj);

/**
 * @brief Reads the shape an .emesh file describes.
 * @return False when the file cannot be opened. A file that names no known generator loads as
 *         mesh_shape_kind::none.
 */
auto load_from_file(const std::string& absolute_path, mesh_shape& obj) -> bool;

} // namespace unravel
