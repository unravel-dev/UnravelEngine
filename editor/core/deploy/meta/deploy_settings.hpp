#pragma once
#include <deploy/deploy_settings.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(android_deploy_settings);
LOAD_EXTERN(android_deploy_settings);
REFLECT_EXTERN(android_deploy_settings);

/// Saves and loads the settings every profile shares: not the profile's own fields, not the device.
SAVE_EXTERN(deploy_settings);
LOAD_EXTERN(deploy_settings);
REFLECT_EXTERN(deploy_settings);

void save_to_file(const std::string& absolute_path, const deploy_settings& obj);
auto load_from_file(const std::string& absolute_path, deploy_settings& obj) -> bool;

} // namespace unravel
