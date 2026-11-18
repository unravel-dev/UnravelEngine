#include "audit_config.h"

#include <engine/assets/impl/asset_writer.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace unravel::audit
{

SAVE(ignore_entry)
{
    try_save(ar, ser20::make_nvp("issue", obj.issue));
    try_save(ar, ser20::make_nvp("reason", obj.reason));
}

LOAD(ignore_entry)
{
    try_load(ar, ser20::make_nvp("issue", obj.issue));
    try_load(ar, ser20::make_nvp("reason", obj.reason));
}

SAVE(threshold_value)
{
    try_save(ar, ser20::make_nvp("rule", obj.rule));
    try_save(ar, ser20::make_nvp("threshold", obj.threshold));
    try_save(ar, ser20::make_nvp("platform", std::string(get_platform_name(obj.platform))));
    try_save(ar, ser20::make_nvp("value", obj.value));
}

LOAD(threshold_value)
{
    try_load(ar, ser20::make_nvp("rule", obj.rule));
    try_load(ar, ser20::make_nvp("threshold", obj.threshold));
    std::string platform;
    try_load(ar, ser20::make_nvp("platform", platform));
    obj.platform = parse_platform(platform).value_or(target_platform::desktop);
    try_load(ar, ser20::make_nvp("value", obj.value));
}

namespace
{

constexpr const char* CONFIG_NODE_NAME = "audit";
constexpr const char* CONFIG_FOLDER = "settings";
constexpr const char* CONFIG_FILE_NAME = "audit.cfg";
/// The config format this engine writes; a newer one is refused rather than read in part.
constexpr std::uint32_t CONFIG_VERSION = 1;

auto find_threshold_entry(std::vector<threshold_value>& entries,
                          const std::string& rule_id,
                          const std::string& name,
                          target_platform platform) -> std::vector<threshold_value>::iterator
{
    return std::find_if(entries.begin(),
                        entries.end(),
                        [&](const threshold_value& entry)
                        {
                            return entry.rule == rule_id && entry.threshold == name && entry.platform == platform;
                        });
}

auto find_rule_threshold(const rule& owner, const std::string& name) -> const threshold*
{
    const auto found = std::find_if(owner.thresholds.begin(),
                                    owner.thresholds.end(),
                                    [&name](const threshold& item)
                                    {
                                        return item.name == name;
                                    });
    return found != owner.thresholds.end() ? &*found : nullptr;
}

} // namespace

auto config::find_ignore(const std::string& issue_id) const -> const ignore_entry*
{
    const auto found = std::find_if(ignores.begin(),
                                    ignores.end(),
                                    [&issue_id](const ignore_entry& entry)
                                    {
                                        return entry.issue == issue_id;
                                    });
    return found != ignores.end() ? &*found : nullptr;
}

void config::ignore(const std::string& issue_id, const std::string& reason)
{
    for(auto& entry : ignores)
    {
        if(entry.issue == issue_id)
        {
            entry.reason = reason;
            return;
        }
    }
    ignores.push_back({issue_id, reason});
}

auto config::unignore(const std::string& issue_id) -> bool
{
    const auto before = ignores.size();
    std::erase_if(ignores,
                  [&issue_id](const ignore_entry& entry)
                  {
                      return entry.issue == issue_id;
                  });
    return ignores.size() != before;
}

auto config::is_rule_enabled(const std::string& rule_id) const -> bool
{
    return std::find(disabled_rules.begin(), disabled_rules.end(), rule_id) == disabled_rules.end();
}

void config::set_rule_enabled(const std::string& rule_id, bool is_enabled)
{
    std::erase(disabled_rules, rule_id);
    if(!is_enabled)
    {
        disabled_rules.push_back(rule_id);
        std::sort(disabled_rules.begin(), disabled_rules.end());
    }
}

auto config::get_threshold(const rule& owner, const std::string& name, target_platform platform) const -> std::int64_t
{
    for(const auto& entry : thresholds)
    {
        if(entry.rule == owner.id && entry.threshold == name && entry.platform == platform)
        {
            return entry.value;
        }
    }
    const auto* own = find_rule_threshold(owner, name);
    return own != nullptr ? own->values[static_cast<std::size_t>(platform)] : 0;
}

void config::set_threshold(const rule& owner, const std::string& name, target_platform platform, std::int64_t value)
{
    const auto* own = find_rule_threshold(owner, name);
    const auto found = find_threshold_entry(thresholds, owner.id, name, platform);
    const bool is_own_value = own != nullptr && own->values[static_cast<std::size_t>(platform)] == value;
    if(is_own_value)
    {
        if(found != thresholds.end())
        {
            thresholds.erase(found);
        }
        return;
    }
    if(found != thresholds.end())
    {
        found->value = value;
        return;
    }
    thresholds.push_back({owner.id, name, platform, value});
}

auto get_config_path(const fs::path& project_root) -> fs::path
{
    return project_root / CONFIG_FOLDER / CONFIG_FILE_NAME;
}

auto write_config(const config& value) -> std::string
{
    std::ostringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("version", CONFIG_VERSION));
        try_save(ar, ser20::make_nvp("ignores", value.ignores));
        try_save(ar, ser20::make_nvp("disabled_rules", value.disabled_rules));
        try_save(ar, ser20::make_nvp("thresholds", value.thresholds));
    }
    return stream.str();
}

auto read_config(const std::string& text, config& value, std::string& error) -> bool
{
    config read;
    try
    {
        std::istringstream stream(text);
        auto ar = ser20::create_iarchive_associative(stream);
        std::uint32_t version{};
        if(!try_load(ar, ser20::make_nvp("version", version)))
        {
            error = "The audit settings have no version: the file is not an audit settings file.";
            return false;
        }
        if(version > CONFIG_VERSION)
        {
            error = "A newer engine wrote the audit settings. Update the engine to read them.";
            return false;
        }
        try_load(ar, ser20::make_nvp("ignores", read.ignores));
        try_load(ar, ser20::make_nvp("disabled_rules", read.disabled_rules));
        try_load(ar, ser20::make_nvp("thresholds", read.thresholds));
    }
    catch(const std::exception& e)
    {
        error = std::string("The audit settings could not be read: ") + e.what();
        return false;
    }
    value = std::move(read);
    return true;
}

auto load_config(const fs::path& project_root, config& value, std::string& error) -> bool
{
    const auto file = get_config_path(project_root);
    fs::error_code ec;
    if(!fs::exists(file, ec))
    {
        value = {};
        return true;
    }
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        error = "The audit settings file could not be opened.";
        return false;
    }
    return read_config(fs::read_stream_str(stream), value, error);
}

auto save_config(const fs::path& project_root, const config& value) -> bool
{
    return write_text_file(get_config_path(project_root), write_config(value));
}

void apply_ignores(const config& value, report& target)
{
    for(auto& item : target.issues)
    {
        const auto* entry = value.find_ignore(item.get_id());
        item.is_ignored = entry != nullptr;
        item.ignore_reason = entry != nullptr ? entry->reason : std::string{};
    }
}

auto write_text_file(const fs::path& file, const std::string& text) -> bool
{
    fs::error_code error;
    fs::create_directories(file.parent_path(), error);
    bool is_written = false;
    asset_writer::atomic_write_file(
        file,
        [&](const fs::path& temp)
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), std::streamsize(text.size()));
            stream.close();
            is_written = stream.good();
        },
        error);
    return is_written && !error;
}

} // namespace unravel::audit
