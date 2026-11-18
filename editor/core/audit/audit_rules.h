#pragma once

#include "audit_config.h"
#include "audit_input.h"

#include <string>
#include <vector>

/// What the rule files share: the context each check reports through. Not for use outside the audit library.
namespace unravel::audit::detail
{

/// What a check reads and where it reports.
class rule_context
{
public:
    rule_context(const input& in, const config& settings, std::vector<issue>& out);

    auto get_input() const -> const input&;
    auto is_enabled(const char* rule_id) const -> bool;
    /// The project's value of a rule's threshold on a platform, else the rule's own.
    auto get_threshold(const char* rule_id, const char* name, target_platform platform) const -> std::int64_t;
    /// An issue of the rule, with the rule's severity and areas.
    auto make(const char* rule_id, subject where, std::string message) const -> issue;
    /// Reports an issue of an enabled rule.
    void add(issue item);
    /// Counts an asset whose compiled file could not be read, for the run's note.
    void count_unread();
    auto get_unread_count() const -> std::size_t;

private:
    const input& in_;
    const config& settings_;
    std::vector<issue>& out_;
    std::size_t unread_{};
};

/// Whether the profile's deploy ships the asset.
auto is_shipped_by(const profile_record& profile, const std::string& key) -> bool;
/// The key of a file of the project ("app:/data/scripts/a.cs"); the path as it is for a file outside it.
auto make_project_key(const fs::path& project_root, const std::string& file) -> std::string;
/// The text with its first letter upper case and a period at its end, when it lacks one.
auto make_sentence(std::string text) -> std::string;

void check_assets(rule_context& context);
void check_settings(rule_context& context);
void check_content(rule_context& context);
void check_scenes(rule_context& context);
void check_scripts(rule_context& context);
void check_code(rule_context& context);

auto make_insights(const input& in) -> std::vector<insight_table>;

} // namespace unravel::audit::detail
