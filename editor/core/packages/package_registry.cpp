#include "package_registry.h"

#include <engine/meta/core/common/basetypes.hpp>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <algorithm>
#include <fstream>

namespace unravel::packages
{

SAVE(installed_file)
{
    try_save(ar, ser20::make_nvp("uid", obj.uid));
    try_save(ar, ser20::make_nvp("source_uid", obj.source_uid));
    try_save(ar, ser20::make_nvp("path", obj.path));
    try_save(ar, ser20::make_nvp("hash", obj.hash));
    try_save(ar, ser20::make_nvp("meta_hash", obj.meta_hash));
}

LOAD(installed_file)
{
    try_load(ar, ser20::make_nvp("uid", obj.uid));
    try_load(ar, ser20::make_nvp("source_uid", obj.source_uid));
    try_load(ar, ser20::make_nvp("path", obj.path));
    try_load(ar, ser20::make_nvp("hash", obj.hash));
    try_load(ar, ser20::make_nvp("meta_hash", obj.meta_hash));
}

SAVE(installed_package)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("display_name", obj.display_name));
    try_save(ar, ser20::make_nvp("version", obj.version));
    try_save(ar, ser20::make_nvp("root", obj.root));
    try_save(ar, ser20::make_nvp("description", obj.description));
    try_save(ar, ser20::make_nvp("author", obj.author));
    try_save(ar, ser20::make_nvp("source_file", obj.source_file));
    try_save(ar, ser20::make_nvp("source_hash", obj.source_hash));
    try_save(ar, ser20::make_nvp("installed_with", obj.installed_with));
    try_save(ar, ser20::make_nvp("files", obj.files));
}

LOAD(installed_package)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("display_name", obj.display_name));
    try_load(ar, ser20::make_nvp("version", obj.version));
    try_load(ar, ser20::make_nvp("root", obj.root));
    try_load(ar, ser20::make_nvp("description", obj.description));
    try_load(ar, ser20::make_nvp("author", obj.author));
    try_load(ar, ser20::make_nvp("source_file", obj.source_file));
    try_load(ar, ser20::make_nvp("source_hash", obj.source_hash));
    try_load(ar, ser20::make_nvp("installed_with", obj.installed_with));
    try_load(ar, ser20::make_nvp("files", obj.files));
}

SAVE(package_registry)
{
    try_save(ar, ser20::make_nvp("packages", obj.packages));
}

LOAD(package_registry)
{
    try_load(ar, ser20::make_nvp("packages", obj.packages));
}

namespace
{
constexpr const char* REGISTRY_NODE_NAME = "installed";
} // namespace

auto installed_package::find_by_source(const hpp::uuid& source_uid) const -> const installed_file*
{
    const auto found = std::find_if(files.begin(),
                                    files.end(),
                                    [&source_uid](const installed_file& file)
                                    {
                                        return file.source_uid == source_uid;
                                    });
    return found == files.end() ? nullptr : &*found;
}

auto package_registry::find(const std::string& name) const -> const installed_package*
{
    const auto found = std::find_if(packages.begin(),
                                    packages.end(),
                                    [&name](const installed_package& package)
                                    {
                                        return package.name == name;
                                    });
    return found == packages.end() ? nullptr : &*found;
}

auto package_registry::find_owner(const hpp::uuid& uid) const -> const installed_package*
{
    for(const auto& package : packages)
    {
        const bool owns = std::any_of(package.files.begin(),
                                      package.files.end(),
                                      [&uid](const installed_file& file)
                                      {
                                          return file.uid == uid;
                                      });
        if(owns)
        {
            return &package;
        }
    }
    return nullptr;
}

void package_registry::put(installed_package package)
{
    const auto found = std::find_if(packages.begin(),
                                    packages.end(),
                                    [&package](const installed_package& other)
                                    {
                                        return other.name == package.name;
                                    });
    if(found != packages.end())
    {
        *found = std::move(package);
        return;
    }
    packages.push_back(std::move(package));
    std::sort(packages.begin(),
              packages.end(),
              [](const installed_package& lhs, const installed_package& rhs)
              {
                  return lhs.name < rhs.name;
              });
}

auto package_registry::remove(const std::string& name) -> bool
{
    const auto size = packages.size();
    packages.erase(std::remove_if(packages.begin(),
                                  packages.end(),
                                  [&name](const installed_package& package)
                                  {
                                      return package.name == name;
                                  }),
                   packages.end());
    return packages.size() != size;
}

auto get_registry_path(const fs::path& project_root) -> fs::path
{
    return project_root / PACKAGE_REGISTRY_FILE_NAME;
}

auto load_registry(const fs::path& file, package_registry& registry, std::string& err) -> bool
{
    registry = {};
    fs::error_code ec;
    if(!fs::exists(file, ec))
    {
        return true;
    }
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        err = "Could not read " + file.generic_string() + ".";
        return false;
    }
    try
    {
        auto ar = ser20::create_iarchive_associative(stream);
        // A file without the record (a merge left something else there) is no empty registry: saving would drop it.
        if(!try_load(ar, ser20::make_nvp(REGISTRY_NODE_NAME, registry)))
        {
            err = file.generic_string() + " holds no record of packages.";
            return false;
        }
    }
    catch(const std::exception& e)
    {
        std::string reason = e.what();
        while(!reason.empty() && (reason.back() == '.' || reason.back() == ' '))
        {
            reason.pop_back();
        }
        err = file.generic_string() + " does not read (" + reason + ").";
        return false;
    }
    return true;
}

auto save_registry(const fs::path& file, const package_registry& registry, std::string& err) -> bool
{
    fs::error_code ec;
    if(registry.packages.empty())
    {
        fs::remove(file, ec);
        return true;
    }
    const auto temporary = file.parent_path() / (file.filename().string() + ".tmp");
    {
        std::ofstream stream(temporary, std::ios::binary);
        if(!stream.good())
        {
            err = "Could not write " + file.generic_string() + ".";
            return false;
        }
        {
            auto ar = ser20::create_oarchive_associative(stream);
            try_save(ar, ser20::make_nvp(REGISTRY_NODE_NAME, registry));
        }
        stream.close();
        if(!stream.good())
        {
            err = "Could not write " + file.generic_string() + ".";
            return false;
        }
    }
    fs::rename(temporary, file, ec);
    if(ec)
    {
        err = "Could not write " + file.generic_string() + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace unravel::packages
