#pragma once

#include "audit_types.h"

#include <filesystem/filesystem.h>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel::audit
{

/// An issue the project chose to keep, and why.
struct ignore_entry
{
    /// The issue's id (issue::get_id).
    std::string issue;
    /// Why it stays, in a sentence; required, so a reviewer of the file can tell.
    std::string reason;

    friend auto operator==(const ignore_entry& lhs, const ignore_entry& rhs) -> bool = default;
};

/// A rule's threshold the project sets for one platform.
struct threshold_value
{
    std::string rule;
    std::string threshold;
    target_platform platform{target_platform::desktop};
    std::int64_t value{};

    friend auto operator==(const threshold_value& lhs, const threshold_value& rhs) -> bool = default;
};

/**
 * @brief What a project changes about its audit: settings/audit.cfg, shared with the project in version control.
 */
struct config
{
    std::vector<ignore_entry> ignores;
    /// Rules the project turned off, by id.
    std::vector<std::string> disabled_rules;
    std::vector<threshold_value> thresholds;

    /// The entry that ignores the issue with this id, or null.
    auto find_ignore(const std::string& issue_id) const -> const ignore_entry*;
    /// Ignores the issue with this id, or changes the reason it is ignored for.
    void ignore(const std::string& issue_id, const std::string& reason);
    /// Stops ignoring the issue with this id; false when it was not ignored.
    auto unignore(const std::string& issue_id) -> bool;

    auto is_rule_enabled(const std::string& rule_id) const -> bool;
    void set_rule_enabled(const std::string& rule_id, bool is_enabled);

    /// The project's value of a threshold on a platform, else the rule's own.
    auto get_threshold(const rule& owner, const std::string& name, target_platform platform) const -> std::int64_t;
    /// Sets the project's value; a value equal to the rule's own drops the entry.
    void set_threshold(const rule& owner, const std::string& name, target_platform platform, std::int64_t value);

    friend auto operator==(const config& lhs, const config& rhs) -> bool = default;
};

/// The config file of the project at `project_root`.
auto get_config_path(const fs::path& project_root) -> fs::path;

/// The config as the file holds it.
auto write_config(const config& value) -> std::string;
/**
 * @brief Reads a config file's text.
 * @return False with `error` set when the text is not a config this engine reads.
 */
auto read_config(const std::string& text, config& value, std::string& error) -> bool;

/**
 * @brief Reads the project's config; a missing file is an empty config.
 * @return False with `error` set when the file exists but cannot be read.
 */
auto load_config(const fs::path& project_root, config& value, std::string& error) -> bool;
/// Writes the project's config, replacing the file in one step.
auto save_config(const fs::path& project_root, const config& value) -> bool;

/// Marks the report's issues the config ignores, and clears the mark on the others.
void apply_ignores(const config& value, report& target);

/// Writes `text` to `file` in one step: a reader sees the old file or the new one, never part of one.
auto write_text_file(const fs::path& file, const std::string& text) -> bool;

} // namespace unravel::audit
