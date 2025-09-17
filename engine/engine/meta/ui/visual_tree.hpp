#pragma once

#include <engine/ui/visual_tree.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(visual_tree);
LOAD_EXTERN(visual_tree);
REFLECT_EXTERN(visual_tree);

void save_to_file(const std::string& absolute_path, const visual_tree::sptr& obj);
void save_to_file_bin(const std::string& absolute_path, const visual_tree::sptr& obj);
void load_from_file(const std::string& absolute_path, visual_tree::sptr& obj);
void load_from_file_bin(const std::string& absolute_path, visual_tree::sptr& obj);

} // namespace unravel
