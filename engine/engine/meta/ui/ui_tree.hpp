#pragma once

#include <engine/ui/ui_tree.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(ui_tree);
LOAD_EXTERN(ui_tree);
REFLECT_EXTERN(ui_tree);

void save_to_file(const std::string& absolute_path, const ui_tree::sptr& obj);
void save_to_file_bin(const std::string& absolute_path, const ui_tree::sptr& obj);
void load_from_file(const std::string& absolute_path, ui_tree::sptr& obj);
void load_from_file_bin(const std::string& absolute_path, ui_tree::sptr& obj);

} // namespace unravel
