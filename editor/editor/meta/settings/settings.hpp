#pragma once

#include <editor/settings/settings.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(editor_settings);
LOAD_EXTERN(editor_settings);
REFLECT_EXTERN(editor_settings);

void save_to_file(const std::string& absolute_path, const editor_settings& obj);
void save_to_file_bin(const std::string& absolute_path, const editor_settings& obj);
auto load_from_file(const std::string& absolute_path, editor_settings& obj) -> bool;
auto load_from_file_bin(const std::string& absolute_path, editor_settings& obj) -> bool;

} // namespace unravel
