#pragma once
#include <editor/project/project_info.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(project_info);
LOAD_EXTERN(project_info);
REFLECT_EXTERN(project_info);

void save_to_file(const std::string& absolute_path, const project_info& obj);
auto load_from_file(const std::string& absolute_path, project_info& obj) -> bool;

} // namespace unravel
