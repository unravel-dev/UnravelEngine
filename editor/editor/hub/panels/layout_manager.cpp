#include "layout_manager.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <logging/logging.h>

#include <fstream>
#include <sstream>

namespace unravel
{

void layout_manager::init(const fs::path& layouts_directory)
{
    layouts_directory_ = layouts_directory;

    fs::error_code err;
    fs::create_directories(layouts_directory_, err);

    load_presets_from_disk();
}

void layout_manager::save_preset(const std::string& name)
{
    size_t ini_size = 0;
    const char* ini_data = ImGui::SaveIniSettingsToMemory(&ini_size);
    if(!ini_data || ini_size == 0)
    {
        APPLOG_WARNING("Failed to capture current layout for preset '{}'", name);
        return;
    }

    std::string data(ini_data, ini_size);
    presets_[name] = data;
    save_preset_to_disk(name, data);

    APPLOG_INFO("Layout preset '{}' saved", name);
}

void layout_manager::load_preset(const std::string& name)
{
    auto it = presets_.find(name);
    if(it == presets_.end())
    {
        APPLOG_WARNING("Layout preset '{}' not found", name);
        return;
    }

    ImGui::ClearIniSettings();
    ImGui::LoadIniSettingsFromMemory(it->second.c_str(), it->second.size());
    ImGui::MarkIniSettingsDirty();

    APPLOG_INFO("Layout preset '{}' loaded", name);
}

void layout_manager::delete_preset(const std::string& name)
{
    auto it = presets_.find(name);
    if(it == presets_.end())
    {
        return;
    }

    presets_.erase(it);
    delete_preset_from_disk(name);

    APPLOG_INFO("Layout preset '{}' deleted", name);
}

void layout_manager::reset_to_default()
{
    ImGui::ClearIniSettings();

    // Force ImGui to mark settings as dirty so it saves the fresh state.
    // Don't reset SettingsLoaded - that would reload the old ini from disk.
    ImGui::MarkIniSettingsDirty();

    APPLOG_INFO("Layout reset to default");
}

auto layout_manager::get_preset_names() const -> std::vector<std::string>
{
    std::vector<std::string> names;
    names.reserve(presets_.size());
    for(const auto& [name, data] : presets_)
    {
        names.push_back(name);
    }
    return names;
}

auto layout_manager::has_preset(const std::string& name) const -> bool
{
    return presets_.contains(name);
}

auto layout_manager::get_layouts_directory() const -> const fs::path&
{
    return layouts_directory_;
}

void layout_manager::load_presets_from_disk()
{
    fs::error_code err;
    if(!fs::exists(layouts_directory_, err))
    {
        return;
    }

    for(const auto& entry : fs::directory_iterator(layouts_directory_, err))
    {
        if(!entry.is_regular_file())
        {
            continue;
        }

        const auto& path = entry.path();
        if(path.extension() != ".ini")
        {
            continue;
        }

        std::ifstream file(path.string(), std::ios::binary);
        if(!file.is_open())
        {
            continue;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string data = ss.str();

        if(!data.empty())
        {
            auto name = path.stem().string();
            presets_[name] = std::move(data);
        }
    }
}

void layout_manager::save_preset_to_disk(const std::string& name, const std::string& ini_data)
{
    auto path = get_preset_path(name);

    std::ofstream file(path.string(), std::ios::binary | std::ios::trunc);
    if(!file.is_open())
    {
        APPLOG_WARNING("Failed to save layout preset '{}' to disk", name);
        return;
    }

    file.write(ini_data.data(), std::streamsize(ini_data.size()));
}

void layout_manager::delete_preset_from_disk(const std::string& name)
{
    auto path = get_preset_path(name);
    fs::error_code err;
    fs::remove(path, err);
}

auto layout_manager::get_preset_path(const std::string& name) const -> fs::path
{
    return layouts_directory_ / (name + ".ini");
}

} // namespace unravel
