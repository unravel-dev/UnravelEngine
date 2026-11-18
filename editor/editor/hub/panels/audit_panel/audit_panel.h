#pragma once

#include <audit/audit_catalog.h>
#include <audit/audit_types.h>
#include <context/context.hpp>

#include <imgui/imgui.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace unravel
{
class imgui_panels;

/**
 * @brief The Project Audit window: runs the audit and shows its report.
 *
 * A run shows each of its stages with what it has done. Summary shows the counts by severity, the top issues, the
 * rules, areas and profiles they fall in, and what the run could not check. Issues lists them grouped by rule,
 * filtered by severity, profile, area and text, beside the selected issue's details: what it found and where, what
 * to do first, why it matters, and its fix and ignore actions. Insights shows the report's tables of assets, builds
 * and scripts, and Rules turns rules on and off and sets their thresholds, in the project's audit settings.
 */
class audit_panel
{
public:
    explicit audit_panel(imgui_panels* parent);

    void on_frame_ui_render(rtti::context& ctx, const char* name);
    void show(bool s);
    /// Shows the window on the issues of one rule.
    void show_rule(const std::string& rule_id);
    /// Shows the window on the issues of one profile's builds.
    void show_profile(const std::string& profile);
    /// Selects an issue in the Issues view, by id, and shows the window.
    void select_issue(const std::string& issue_id);

private:
    enum class view : std::uint8_t
    {
        summary,
        issues,
        insights,
        rules,
    };

    void draw_ui(rtti::context& ctx);
    void draw_toolbar(rtti::context& ctx);
    void draw_run_controls(rtti::context& ctx);
    void draw_view_switch(const audit::report* report);
    void draw_export_menu(const audit::report* report);
    void draw_messages(rtti::context& ctx);
    void draw_empty_state(rtti::context& ctx);
    /// The run's stages, what each has done and the whole run's share; compact over an earlier report.
    void draw_run_progress(rtti::context& ctx, bool is_compact);

    // audit_panel_summary.cpp
    void draw_summary(rtti::context& ctx, const audit::report& report);
    void draw_summary_header(rtti::context& ctx, const audit::report& report);
    void draw_summary_cards(const audit::report& report);
    void draw_top_issues(const audit::report& report);
    void draw_rule_chart(const audit::report& report);
    void draw_area_chart(const audit::report& report);
    void draw_profile_list(const audit::report& report);
    void draw_notes(const audit::report& report);

    // audit_panel_issues.cpp
    void draw_issues(rtti::context& ctx, const audit::report& report);
    void draw_issue_filters(const audit::report& report);
    void draw_issue_list(const audit::report& report, const std::vector<const audit::issue*>& visible);
    void draw_issue_row(const audit::issue& item, int row_index);
    void draw_issue_details(rtti::context& ctx, const audit::report& report);
    void draw_issue_header(rtti::context& ctx, const audit::issue& item);
    void draw_issue_advice(rtti::context& ctx, const audit::issue& item);
    void draw_issue_facts(rtti::context& ctx, const audit::issue& item);
    void draw_issue_why(const audit::rule& owner);
    void draw_ignore_popup(rtti::context& ctx, const audit::issue& item);

    // audit_panel_insights.cpp
    void draw_insights(const audit::report& report);
    void draw_insight_navigation(const audit::report& report);
    void draw_insight_table(const audit::insight_table& table);

    // audit_panel_rules.cpp
    void draw_rules(rtti::context& ctx);
    void draw_rule_list(rtti::context& ctx, const std::map<std::string, std::size_t>& counts);
    void draw_rule_details(rtti::context& ctx, const audit::rule& owner, std::size_t issue_count);

    auto is_visible(const audit::issue& item) const -> bool;
    auto collect_visible(const audit::report& report) const -> std::vector<const audit::issue*>;
    void open_subject(rtti::context& ctx, const audit::issue& item);
    void open_script_line(const std::string& key, std::uint32_t line);
    void export_report(const audit::report& report, bool as_csv);
    void show_issues_of(const std::string& rule_id, int severity_only);
    void set_message(std::string text, bool is_error);

    imgui_panels* parent_{};
    bool show_{false};
    bool show_request_{false};
    view view_{view::summary};

    bool scan_documents_{true};
    bool analyze_code_{true};
    std::string message_;
    bool message_is_error_{false};

    ImGuiTextFilter filter_;
    /// Shown severities, by audit::severity.
    bool shows_severity_[3]{true, true, true};
    bool shows_ignored_{false};
    std::string profile_filter_;
    std::uint16_t area_filter_{};
    std::string rule_filter_;
    std::set<std::string> collapsed_rules_;
    std::string selected_issue_;
    bool scroll_to_selected_{false};
    bool shows_why_{true};
    /// The first pane's share of each view's split, side by side and one over the other.
    float issues_split_{0.55f};
    float rules_split_{0.46f};
    float rules_stacked_split_{0.5f};
    float insights_split_{0.16f};

    std::string selected_insight_;
    ImGuiTextFilter insight_filter_;
    /// The shown insight table's rows in the order shown and the largest value of each column, worked out again
    /// only when the report, the table, its search or its sort changes.
    std::vector<std::size_t> insight_order_;
    std::vector<double> insight_largest_;
    std::string insight_filter_key_;
    std::string insight_sort_key_;
    std::string selected_rule_;
    ImGuiTextFilter rule_filter_text_;
    std::set<int> collapsed_rule_sources_;
    char ignore_reason_[512]{};
};

} // namespace unravel
