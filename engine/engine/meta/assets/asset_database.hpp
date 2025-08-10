#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_manager.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(asset_meta);
LOAD_EXTERN(asset_meta);

SAVE_EXTERN(asset_database::meta);
LOAD_EXTERN(asset_database::meta);

SAVE_EXTERN(asset_database);
LOAD_EXTERN(asset_database);

void save_to_file(const std::string& absolute_path, const asset_database& obj);
void save_to_file_bin(const std::string& absolute_path, const asset_database& obj);
auto load_from_file(const std::string& absolute_path, asset_database& obj) -> bool;
auto load_from_file_bin(const std::string& absolute_path, asset_database& obj) -> bool;


void save_to_file(const std::string& absolute_path, const asset_meta& obj);
void save_to_file_bin(const std::string& absolute_path, const asset_meta& obj);
auto load_from_file(const std::string& absolute_path, asset_meta& obj) -> bool;
auto load_from_file_bin(const std::string& absolute_path, asset_meta& obj) -> bool;


template<typename Importer>
auto load_importer_from_file(const std::string& absolute_path, asset_meta& obj) -> bool
{
    asset_meta loaded_meta;
    if(load_from_file(absolute_path, loaded_meta))
    {
        auto obj_importer = std::dynamic_pointer_cast<Importer>(obj.importer);;
        auto loaded_importer = std::dynamic_pointer_cast<Importer>(loaded_meta.importer);;

        if(obj_importer && loaded_importer)
        {
            *obj_importer = *loaded_importer;
            return true;
        }
    }
    return false;
}

} // namespace unravel
