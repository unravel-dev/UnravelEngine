#include "audit_manager.h"
#include "audit_fixes.h"

#include <audit/audit_catalog.h>
#include <audit/audit_report_io.h>
#include <audit/audit_run.h>
#include <deploy/deploy_settings.h>
#include <editor/deploy/deploy_pipeline.h>
#include <editor/deploy/deploy_preflight.h>
#include <editor/events.h>
#include <editor/system/project_manager.h>
#include <engine/engine.h>
#include <engine/play_mode.h>
#include <logging/logging.h>
#include <threadpp/future.hpp>

#include <algorithm>
#include <chrono>
#include <exception>

namespace unravel
{
namespace
{

/// The time a frame spends loading documents before it lets the editor draw.
constexpr auto AUDIT_SCAN_FRAME_BUDGET = std::chrono::milliseconds(8);
/// Each stage's share of a run's time, by audit::run_stage, for the run's progress bar. A stage the run leaves out
/// gives its share to the others.
constexpr std::array<float, audit::RUN_STAGE_COUNT> AUDIT_STAGE_SHARES{0.01f, 0.15f, 0.25f, 0.15f, 0.25f, 0.12f, 0.07f};
/// How done a running stage that cannot count its work counts as, for the progress bar.
constexpr float AUDIT_UNCOUNTED_STAGE_DONE = 0.5f;
/// How long --audit waits for --project's project to be open.
constexpr auto AUDIT_COMMAND_LINE_PROJECT_WAIT = std::chrono::seconds(30);
/// How long --audit waits for the project, the compiler and the run, all together.
constexpr auto AUDIT_COMMAND_LINE_TIMEOUT = std::chrono::minutes(60);

} // namespace

audit_manager::audit_manager(cmd_line::parser& parser)
{
    parser.set_optional<std::string>("",
                                     "audit",
                                     "",
                                     "Audit the project --project opens once its assets and scripts compiled, write "
                                     "the report to this JSON file and quit.");
}

auto audit_manager::init(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    parser.try_get("audit", command_line_report_);
    command_line_since_ = std::chrono::steady_clock::now();
    auto& ui_ev = ctx.get_cached<ui_events>();
    ui_ev.on_frame_ui_render.connect(sentinel_, this, &audit_manager::on_frame_ui_render);
    ui_ev.on_open_project.connect(sentinel_, this, &audit_manager::on_open_project);
    ui_ev.on_close_project.connect(sentinel_, this, &audit_manager::on_close_project);
    // A project opened from the command line (-p) opened before this manager listened.
    if(ctx.get_cached<project_manager>().has_open_project())
    {
        on_open_project(ctx);
    }
    return true;
}

auto audit_manager::deinit(rtti::context& ctx) -> bool
{
    (void)ctx;
    is_closing_->store(true);
    stop_->store(true);
    sentinel_.reset();
    return true;
}

auto audit_manager::start(rtti::context& ctx, const run_request& request, std::string& error) -> bool
{
    auto& pm = ctx.get_cached<project_manager>();
    if(!pm.has_open_project())
    {
        error = "Open a project first.";
        return false;
    }
    if(phase_ != phase::idle)
    {
        error = "An audit is already running. Wait for it to finish, or cancel it.";
        return false;
    }
    if(ctx.get_cached<play_mode>().is_active())
    {
        error = "Stop play mode first: the audit loads scenes and prefabs as they are saved.";
        return false;
    }
    if(const auto pending = deployment::get_pending_compile_reason(ctx); !pending.empty())
    {
        error = pending + " Run the audit once the compiler is idle, so it reads current files.";
        return false;
    }
    ++run_id_;
    run_started_ = std::chrono::steady_clock::now();
    stop_ = std::make_shared<std::atomic<bool>>(false);
    progress_ = std::make_shared<shared_progress>();
    if(!request.scan_documents)
    {
        progress_->stages[static_cast<std::size_t>(audit::run_stage::documents)].status = stage_progress::state::skipped;
    }
    if(!request.analyze_code)
    {
        progress_->stages[static_cast<std::size_t>(audit::run_stage::scripts)].status = stage_progress::state::skipped;
    }
    run_error_.clear();
    scanner_.begin_run();
    project_root_ = deployment::get_project_root();
    progress_->report({audit::run_stage::snapshot});
    input_ = make_audit_input(ctx, request.analyze_code);
    documents_ = request.scan_documents ? list_audit_documents(input_) : std::vector<audit_document>{};
    next_document_ = 0;
    if(request.scan_documents)
    {
        progress_->report({audit::run_stage::documents, 0, documents_.size()});
    }
    input_.scan = {};
    input_.scan.ran = request.scan_documents;
    phase_ = phase::scanning;
    ++revision_;
    return true;
}

void audit_manager::cancel()
{
    if(phase_ == phase::idle)
    {
        return;
    }
    // A run in flight finishes on its own; its report is dropped and never replaces the last one.
    ++run_id_;
    reset();
    ++revision_;
}

auto audit_manager::is_running() const -> bool
{
    return phase_ != phase::idle;
}

auto audit_manager::get_progress() const -> progress
{
    progress copy;
    copy.state = phase_;
    const auto now = std::chrono::steady_clock::now();
    {
        const std::lock_guard<std::mutex> lock(progress_->mutex);
        copy.stages = progress_->stages;
        copy.current = progress_->current;
        copy.item = progress_->item;
        copy.seconds = std::chrono::duration<double>(now - progress_->run_started).count();
        for(std::size_t index = 0; index < copy.stages.size(); ++index)
        {
            if(copy.stages[index].status == stage_progress::state::running)
            {
                copy.stages[index].seconds = std::chrono::duration<double>(now - progress_->started[index]).count();
            }
        }
    }
    float shares = 0.0f;
    float done = 0.0f;
    for(std::size_t index = 0; index < copy.stages.size(); ++index)
    {
        const auto& stage = copy.stages[index];
        if(stage.status == stage_progress::state::skipped)
        {
            continue;
        }
        shares += AUDIT_STAGE_SHARES[index];
        if(stage.status == stage_progress::state::done)
        {
            done += AUDIT_STAGE_SHARES[index];
        }
        else if(stage.status == stage_progress::state::running)
        {
            const float part = stage.total > 0 ? float(stage.done) / float(stage.total) : AUDIT_UNCOUNTED_STAGE_DONE;
            done += AUDIT_STAGE_SHARES[index] * part;
        }
    }
    copy.done = shares > 0.0f ? std::clamp(done / shares, 0.0f, 1.0f) : 0.0f;
    return copy;
}

void audit_manager::shared_progress::report(const audit::stage_report& value)
{
    const std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    const auto index = static_cast<std::size_t>(value.stage);
    for(std::size_t before = 0; before < index; ++before)
    {
        auto& stage = stages[before];
        if(stage.status == stage_progress::state::running)
        {
            stage.status = stage_progress::state::done;
            stage.seconds = std::chrono::duration<double>(now - started[before]).count();
            stage.done = std::max(stage.done, stage.total);
        }
        else if(stage.status == stage_progress::state::pending)
        {
            // A stage with nothing to do, such as a scan of a project without documents.
            stage.status = stage_progress::state::done;
        }
    }
    auto& stage = stages[index];
    if(stage.status != stage_progress::state::running)
    {
        stage.status = stage_progress::state::running;
        started[index] = now;
    }
    stage.done = value.done;
    stage.total = value.total;
    current = value.stage;
    item = value.item;
}

auto audit_manager::get_report() const -> const audit::report*
{
    return report_.get();
}

auto audit_manager::get_change() const -> const audit::report_change&
{
    return change_;
}

auto audit_manager::get_revision() const -> std::uint64_t
{
    return revision_;
}

auto audit_manager::get_run_error() const -> const std::string&
{
    return run_error_;
}

auto audit_manager::get_config() const -> const audit::config&
{
    return config_;
}

auto audit_manager::get_config_error() const -> const std::string&
{
    return config_error_;
}

auto audit_manager::ignore(const std::string& issue_id, const std::string& reason, std::string& error) -> bool
{
    if(reason.empty())
    {
        error = "Give the reason the issue is fine: the project's audit settings keep it for whoever reads them.";
        return false;
    }
    auto changed = config_;
    changed.ignore(issue_id, reason);
    std::swap(config_, changed);
    if(!save_config(error))
    {
        std::swap(config_, changed);
        return false;
    }
    return true;
}

auto audit_manager::unignore(const std::string& issue_id, std::string& error) -> bool
{
    auto changed = config_;
    if(!changed.unignore(issue_id))
    {
        error = "The issue is not ignored.";
        return false;
    }
    std::swap(config_, changed);
    if(!save_config(error))
    {
        std::swap(config_, changed);
        return false;
    }
    return true;
}

auto audit_manager::set_rule_enabled(const std::string& rule_id, bool is_enabled, std::string& error) -> bool
{
    if(audit::find_rule(rule_id) == nullptr)
    {
        error = "No rule has the id " + rule_id + ".";
        return false;
    }
    auto changed = config_;
    changed.set_rule_enabled(rule_id, is_enabled);
    std::swap(config_, changed);
    if(!save_config(error))
    {
        std::swap(config_, changed);
        return false;
    }
    return true;
}

auto audit_manager::set_threshold(const std::string& rule_id,
                                  const std::string& name,
                                  target_platform platform,
                                  std::int64_t value,
                                  std::string& error) -> bool
{
    const auto* owner = audit::find_rule(rule_id);
    const bool has_threshold = owner != nullptr && std::any_of(owner->thresholds.begin(),
                                                               owner->thresholds.end(),
                                                               [&name](const audit::threshold& item)
                                                               {
                                                                   return item.name == name;
                                                               });
    if(!has_threshold)
    {
        error = "The rule " + rule_id + " has no threshold named " + name + ".";
        return false;
    }
    if(value < 0)
    {
        error = "A threshold cannot be negative.";
        return false;
    }
    auto changed = config_;
    changed.set_threshold(*owner, name, platform, value);
    std::swap(config_, changed);
    if(!save_config(error))
    {
        std::swap(config_, changed);
        return false;
    }
    return true;
}

auto audit_manager::apply_fix(rtti::context& ctx, const std::string& issue_id, std::string& error) -> bool
{
    const auto* found = report_ ? report_->find(issue_id) : nullptr;
    if(found == nullptr)
    {
        error = "The last report has no issue with this id.";
        return false;
    }
    if(found->fix.empty())
    {
        error = "This issue has no fix the editor can apply.";
        return false;
    }
    if(ctx.get_cached<play_mode>().is_active())
    {
        error = "Stop play mode first: a fix changes the project's files.";
        return false;
    }
    if(deployment::is_deploy_running())
    {
        error = "Wait for the deploy to finish: it reads the project's files until it is done.";
        return false;
    }
    if(!apply_audit_fix(ctx, *found, config_, error))
    {
        return false;
    }
    fixed_.insert(issue_id);
    ++revision_;
    return true;
}

auto audit_manager::get_fixable_issues(const std::string& rule_id) const -> std::vector<std::string>
{
    std::vector<std::string> ids;
    if(!report_)
    {
        return ids;
    }
    for(const auto& item : report_->issues)
    {
        if(item.rule != rule_id || item.fix.empty() || item.is_ignored)
        {
            continue;
        }
        auto id = item.get_id();
        if(!is_fixed(id))
        {
            ids.push_back(std::move(id));
        }
    }
    return ids;
}

auto audit_manager::apply_rule_fixes(rtti::context& ctx, const std::string& rule_id, std::string& error) -> std::size_t
{
    std::size_t applied = 0;
    for(const auto& id : get_fixable_issues(rule_id))
    {
        if(!apply_fix(ctx, id, error))
        {
            break;
        }
        ++applied;
    }
    return applied;
}

auto audit_manager::is_fixed(const std::string& issue_id) const -> bool
{
    return fixed_.count(issue_id) > 0;
}

void audit_manager::on_frame_ui_render(rtti::context& ctx, const frame_times& times)
{
    (void)times;
    drive_command_line(ctx);
    if(phase_ != phase::scanning)
    {
        return;
    }
    if(ctx.get_cached<play_mode>().is_active())
    {
        reset();
        run_error_ = "The audit stopped because play mode started. Run it again in edit mode.";
        ++revision_;
        return;
    }
    scan_documents(ctx);
    if(next_document_ >= documents_.size())
    {
        start_rules();
    }
}

void audit_manager::drive_command_line(rtti::context& ctx)
{
    if(command_line_report_.empty() || command_line_done_)
    {
        return;
    }
    if(std::chrono::steady_clock::now() - command_line_since_ > AUDIT_COMMAND_LINE_TIMEOUT)
    {
        finish_command_line(command_line_started_ ? "The audit did not finish in time."
                                                  : "The project did not open, or its assets did not finish compiling, in time.");
        return;
    }
    if(!ctx.get_cached<project_manager>().has_open_project())
    {
        // --project opens it before the first frame; without one nothing will.
        if(std::chrono::steady_clock::now() - command_line_since_ > AUDIT_COMMAND_LINE_PROJECT_WAIT)
        {
            finish_command_line("No project is open. Pass the project's folder with --project.");
        }
        return;
    }
    if(!command_line_started_)
    {
        // The compiler first catches up with the project; the run reads current files only.
        if(!deployment::get_pending_compile_reason(ctx).empty())
        {
            return;
        }
        std::string error;
        if(!start(ctx, {}, error))
        {
            finish_command_line(error);
            return;
        }
        command_line_started_ = true;
        return;
    }
    if(!is_running())
    {
        finish_command_line(report_ ? std::string{} : run_error_);
    }
}

void audit_manager::finish_command_line(const std::string& error)
{
    command_line_done_ = true;
    const fs::path file = fs::from_utf8_string(command_line_report_);
    if(error.empty() && report_ != nullptr && audit::save_report(file, *report_))
    {
        APPLOG_INFO("Project audit: the report was written to {}.", file.generic_string());
    }
    else
    {
        // A report CI can read either way: the error says why there are no issues to judge.
        const auto reason = error.empty() ? std::string("The report could not be written.") : error;
        audit::report failed;
        failed.project = report_ ? report_->project : std::string{};
        failed.error = reason;
        audit::write_text_file(file, audit::write_report(failed));
        APPLOG_ERROR("Project audit: {}", reason);
    }
    engine::interrupt();
}

void audit_manager::on_open_project(rtti::context& ctx)
{
    (void)ctx;
    reset();
    report_.reset();
    change_ = {};
    fixed_.clear();
    run_error_.clear();
    facts_->clear();
    project_root_ = deployment::get_project_root();
    config_ = {};
    config_error_.clear();
    if(!audit::load_config(project_root_, config_, config_error_))
    {
        APPLOG_WARNING("Project audit: {}", config_error_);
    }
    audit::report cached;
    std::string error;
    fs::error_code ec;
    const auto cached_file = audit::get_last_report_path(project_root_);
    if(fs::exists(cached_file, ec) && audit::load_report(cached_file, cached, error))
    {
        audit::apply_ignores(config_, cached);
        report_ = std::make_unique<audit::report>(std::move(cached));
    }
    ++revision_;
}

void audit_manager::on_close_project(rtti::context& ctx)
{
    (void)ctx;
    reset();
    report_.reset();
    change_ = {};
    fixed_.clear();
    run_error_.clear();
    config_ = {};
    config_error_.clear();
    ++revision_;
}

void audit_manager::scan_documents(rtti::context& ctx)
{
    const auto started = std::chrono::steady_clock::now();
    do
    {
        if(next_document_ >= documents_.size())
        {
            return;
        }
        const auto& document = documents_[next_document_];
        progress_->report({audit::run_stage::documents, next_document_, documents_.size(), document.key});
        try
        {
            scanner_.scan(ctx, document, project_root_, input_.scan);
        }
        catch(const std::exception& e)
        {
            input_.scan.unreadable.push_back({document.key, std::string("Checking it failed: ") + e.what()});
        }
        ++next_document_;
    } while(std::chrono::steady_clock::now() - started < AUDIT_SCAN_FRAME_BUDGET);
}

void audit_manager::start_rules()
{
    phase_ = phase::running;
    documents_.clear();
    const auto run_id = run_id_;
    const auto stop = stop_;
    const auto is_closing = is_closing_;
    const auto progress = progress_;
    const auto facts = facts_;
    const auto root = project_root_;
    // The scan's frames count toward the run's time.
    const auto scan_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - run_started_).count();
    auto config = config_;
    tpp::async(
        [in = std::move(input_), config = std::move(config), stop, progress, facts, root, scan_seconds]() mutable
        {
            audit::run_options options;
            options.cache = facts.get();
            options.should_stop = [stop]()
            {
                return stop->load();
            };
            options.on_progress = [progress](const audit::stage_report& value)
            {
                progress->report(value);
            };
            auto result = audit::run_audit(in, config, options);
            result.seconds += scan_seconds;
            // A stopped run's report is incomplete: the cache keeps the last complete one.
            if(!stop->load() && !audit::save_report(audit::get_last_report_path(root), result))
            {
                result.notes.emplace_back("The report could not be saved in the project's cache folder.");
            }
            return result;
        })
        .then(tpp::this_thread::get_id(),
              [this, run_id, is_closing](auto outcome)
              {
                  // Once the editor closes, the manager may be gone.
                  if(is_closing->load())
                  {
                      return;
                  }
                  try
                  {
                      store_report(outcome.get(), run_id);
                  }
                  catch(const std::exception& e)
                  {
                      if(run_id == run_id_)
                      {
                          reset();
                          run_error_ = std::string("The audit failed: ") + e.what();
                          ++revision_;
                      }
                  }
              });
    input_ = {};
}

void audit_manager::store_report(audit::report result, std::uint64_t run_id)
{
    if(run_id != run_id_)
    {
        return;
    }
    audit::apply_ignores(config_, result);
    change_ = report_ ? audit::compare_reports(*report_, result) : audit::report_change{};
    report_ = std::make_unique<audit::report>(std::move(result));
    fixed_.clear();
    phase_ = phase::idle;
    ++revision_;
    APPLOG_INFO("Project audit: {} error(s), {} warning(s), {} suggestion(s) in {:.1f} s.",
                report_->count(audit::severity::error),
                report_->count(audit::severity::warning),
                report_->count(audit::severity::info),
                report_->seconds);
}

void audit_manager::reset()
{
    stop_->store(true);
    phase_ = phase::idle;
    input_ = {};
    documents_.clear();
    next_document_ = 0;
}

auto audit_manager::save_config(std::string& error) -> bool
{
    if(deployment::is_deploy_running())
    {
        error = "Wait for the deploy to finish: it reads the project's files until it is done.";
        return false;
    }
    if(!config_error_.empty())
    {
        error = "The project's audit settings could not be read, so they are not written over: " + config_error_;
        return false;
    }
    if(project_root_.empty() || !audit::save_config(project_root_, config_))
    {
        error = "The project's audit settings could not be written.";
        return false;
    }
    if(report_)
    {
        audit::apply_ignores(config_, *report_);
    }
    ++revision_;
    return true;
}


} // namespace unravel
