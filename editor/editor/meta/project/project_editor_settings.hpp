#pragma once
#include <editor/project/project_editor_settings.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(project_editor_settings);
LOAD_EXTERN(project_editor_settings);
REFLECT_EXTERN(project_editor_settings);

void save_to_file(const std::string& absolute_path, const project_editor_settings& obj);
auto load_from_file(const std::string& absolute_path, project_editor_settings& obj) -> bool;

} // namespace unravel
