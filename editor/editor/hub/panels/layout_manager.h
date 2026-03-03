#pragma once

#include <filesystem/filesystem.h>

#include <map>
#include <string>
#include <vector>

namespace unravel
{

class layout_manager
{
public:
    void init(const fs::path& layouts_directory);

    void save_preset(const std::string& name);
    void load_preset(const std::string& name);
    void delete_preset(const std::string& name);
    void reset_to_default();

    auto get_preset_names() const -> std::vector<std::string>;
    auto has_preset(const std::string& name) const -> bool;
    auto get_layouts_directory() const -> const fs::path&;

private:
    void load_presets_from_disk();
    void save_preset_to_disk(const std::string& name, const std::string& ini_data);
    void delete_preset_from_disk(const std::string& name);
    auto get_preset_path(const std::string& name) const -> fs::path;

    fs::path layouts_directory_;
    std::map<std::string, std::string> presets_;
};

} // namespace unravel
