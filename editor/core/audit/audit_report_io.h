#pragma once

#include "audit_types.h"

#include <filesystem/filesystem.h>

#include <string>

namespace unravel::audit
{

/// The report format this engine writes; a newer one is refused rather than read in part.
constexpr std::uint32_t REPORT_FORMAT_VERSION = 1;

/// The report as JSON: what the editor keeps between sessions, what --audit writes and what CI compares.
auto write_report(const report& value) -> std::string;
/**
 * @brief Reads a report's JSON.
 * @return False with `error` set when the text is not a report this engine reads.
 */
auto read_report(const std::string& text, report& value, std::string& error) -> bool;

auto save_report(const fs::path& file, const report& value) -> bool;
auto load_report(const fs::path& file, report& value, std::string& error) -> bool;

/// The issues as comma-separated values with a header row, for a spreadsheet: severity, rule, title, subject,
/// line, profiles, message, ignored and the ignore reason.
auto write_issues_csv(const report& value) -> std::string;

/// Where the editor keeps the project's last report: in its cache, beside the compiled files.
auto get_last_report_path(const fs::path& project_root) -> fs::path;

} // namespace unravel::audit
