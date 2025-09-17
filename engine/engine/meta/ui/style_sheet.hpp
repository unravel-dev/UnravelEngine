#pragma once

#include <engine/ui/style_sheet.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(style_sheet);
LOAD_EXTERN(style_sheet);
REFLECT_EXTERN(style_sheet);

void save_to_file(const std::string& absolute_path, const style_sheet::sptr& obj);
void save_to_file_bin(const std::string& absolute_path, const style_sheet::sptr& obj);
void load_from_file(const std::string& absolute_path, style_sheet::sptr& obj);
void load_from_file_bin(const std::string& absolute_path, style_sheet::sptr& obj);

} // namespace unravel
