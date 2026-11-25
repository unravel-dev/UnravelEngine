#pragma once

#include "audit_document_scan.h"
#include "audit_snapshot.h"

#include <audit/audit_config.h>
#include <audit/audit_facts.h>
#include <audit/audit_run.h>
#include <audit/audit_types.h>
#include <cmd_line/parser.h>
#include <context/context.hpp>
#include <engine/frame_times.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace unravel
{

/**
 * @brief The open project's audit: runs it, keeps its last report and the project's audit settings, ignores issues
 * and applies fixes.
 *
 * A run takes a snapshot of the project on the main thread, loads its documents a few each frame (scenes and prefabs
 * load only on the main thread), then runs the rules on a pool thread; the report comes back to the main thread with
 * no side waiting on the other. The last report is kept in the project's cache, so it is there when the project opens
 * again. Edit mode only: a run is refused while the game plays, and a scan stops when play starts.
 */
class audit_manager
{
public:
    /// Where a run is.
    enum class phase : std::uint8_t
    {
        idle,
        /// Loading documents on the main thread.
        scanning,
        /// The rules run on a pool thread.
        running,
    };

    /// What a run checks.
    struct run_request
    {
        /// Load scenes, prefabs, materials and controllers: missing references, joints, components.
        bool scan_documents{true};
        /// Run the script analysis over the compiled scripts.
        bool analyze_code{true};
    };

    /// Where one stage of a run is.
    struct stage_progress
    {
        enum class state : std::uint8_t
        {
            pending,
            running,
            done,
            /// The run does not do it: the request left it out.
            skipped,
        };

        state status{state::pending};
        std::size_t done{};
        /// 0 when the stage cannot count its work.
        std::size_t total{};
        /// How long the stage took, or has taken so far.
        double seconds{};
    };

    /// A run's progress, as the editor shows it.
    struct progress
    {
        phase state{phase::idle};
        /// By audit::run_stage.
        std::array<stage_progress, audit::RUN_STAGE_COUNT> stages{};
        /// The stage that runs now.
        audit::run_stage current{audit::run_stage::snapshot};
        /// What the stage reads now ("app:/data/Scenes/Main.spfb"); empty when it reads no one thing.
        std::string item;
        /// Since the run started.
        double seconds{};
        /// The run's share done, 0 to 1, from the stages it does.
        float done{};
    };

    /// Registers --audit: audit the project opened with --project, write the report to a file and quit.
    explicit audit_manager(cmd_line::parser& parser);

    auto init(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Starts a run.
     * @return False with `error` set, in a sentence, when one cannot start now: no project, the game plays, assets are
     * compiling, or a run is going.
     */
    auto start(rtti::context& ctx, const run_request& request, std::string& error) -> bool;
    /// Stops the run; the report of a run stopped while its rules ran says it is incomplete.
    void cancel();
    auto is_running() const -> bool;
    auto get_progress() const -> progress;

    /// The last report, or null before the project's first run.
    auto get_report() const -> const audit::report*;
    /// What changed from the run before the last one; empty when there was none.
    auto get_change() const -> const audit::report_change&;
    /// Counts changes to the report, its ignores and the settings, so a view can notice one.
    auto get_revision() const -> std::uint64_t;
    /// Why the last run failed; empty when it did not.
    auto get_run_error() const -> const std::string&;

    auto get_config() const -> const audit::config&;
    /// Why the project's audit settings could not be read; empty when they could. Ignores cannot change then.
    auto get_config_error() const -> const std::string&;

    /// Ignores an issue for the reason given, in the project's audit settings.
    auto ignore(const std::string& issue_id, const std::string& reason, std::string& error) -> bool;
    auto unignore(const std::string& issue_id, std::string& error) -> bool;
    auto set_rule_enabled(const std::string& rule_id, bool is_enabled, std::string& error) -> bool;
    auto set_threshold(const std::string& rule_id,
                       const std::string& name,
                       target_platform platform,
                       std::int64_t value,
                       std::string& error) -> bool;

    /**
     * @brief Applies the issue's fix through the editor's own functions: import settings are written as the inspector
     * writes them, settings as Project Settings overrides them.
     * @return False with `error` set when the issue has no fix, or the fix could not be applied.
     */
    auto apply_fix(rtti::context& ctx, const std::string& issue_id, std::string& error) -> bool;
    /// The ids of the last report's issues of the rule whose fix can still be applied: they have one, it was not
    /// applied since the last run, and the issue is not ignored.
    auto get_fixable_issues(const std::string& rule_id) const -> std::vector<std::string>;
    /**
     * @brief Applies the fix of every issue of the rule that get_fixable_issues lists, stopping at the first that
     * cannot be applied.
     * @return How many fixes were applied; `error` says why it stopped early.
     */
    auto apply_rule_fixes(rtti::context& ctx, const std::string& rule_id, std::string& error) -> std::size_t;
    /// Whether the issue's fix was applied since the last run; the next run confirms it.
    auto is_fixed(const std::string& issue_id) const -> bool;

private:
    /// The progress the main thread and the run's thread both write; the panel reads a copy.
    struct shared_progress
    {
        mutable std::mutex mutex;
        std::array<stage_progress, audit::RUN_STAGE_COUNT> stages{};
        std::array<std::chrono::steady_clock::time_point, audit::RUN_STAGE_COUNT> started{};
        audit::run_stage current{audit::run_stage::snapshot};
        std::string item;
        std::chrono::steady_clock::time_point run_started{std::chrono::steady_clock::now()};

        /// Moves the run to the report's stage: the stages before it that ran are done.
        void report(const audit::stage_report& value);
    };

    void on_frame_ui_render(rtti::context& ctx, const frame_times& times);
    void on_open_project(rtti::context& ctx);
    void on_close_project(rtti::context& ctx);

    void scan_documents(rtti::context& ctx);
    /// Drives a --audit run: waits for the project and the compiler, runs, writes the report and quits.
    void drive_command_line(rtti::context& ctx);
    void finish_command_line(const std::string& error);
    void start_rules();
    void store_report(audit::report result, std::uint64_t run_id);
    void reset();
    auto save_config(std::string& error) -> bool;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    phase phase_{phase::idle};
    fs::path project_root_;
    audit::input input_;
    std::vector<audit_document> documents_;
    std::size_t next_document_{};
    audit_document_scanner scanner_;

    /// When the current run started: its report counts the scan's frames too.
    std::chrono::steady_clock::time_point run_started_{};
    /// Counts runs: a report from a run that is not the current one is dropped.
    std::uint64_t run_id_{};
    std::shared_ptr<std::atomic<bool>> stop_ = std::make_shared<std::atomic<bool>>(false);
    /// Shared with a run in flight, which can outlive deinit.
    std::shared_ptr<std::atomic<bool>> is_closing_ = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<shared_progress> progress_ = std::make_shared<shared_progress>();
    std::shared_ptr<audit::facts_cache> facts_ = std::make_shared<audit::facts_cache>();

    std::unique_ptr<audit::report> report_;
    audit::report_change change_;
    std::string run_error_;
    std::uint64_t revision_{1};
    audit::config config_;
    std::string config_error_;
    std::unordered_set<std::string> fixed_;

    /// The report file --audit names; empty without it.
    std::string command_line_report_;
    bool command_line_started_{false};
    bool command_line_done_{false};
    std::chrono::steady_clock::time_point command_line_since_{};
};

} // namespace unravel
