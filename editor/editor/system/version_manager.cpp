#include "version_manager.h"

#include <editor/imgui/integration/imgui_notify.h>
#include <editor/system/project_manager.h>
#include <imgui/imgui.h>
#include <logging/logging.h>
#include <threadpp/future.hpp>
#include <update_check/update_fetch.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace unravel
{
namespace
{

/// The update check's toast: a later answer replaces the one shown. Distinct from the other toasts' ids.
constexpr std::uint64_t UPDATE_TOAST_ID = 100;
constexpr int UPDATE_NOTICE_DURATION_MS = 60000;
constexpr int UPDATE_ANSWER_DURATION_MS = 5000;

auto is_startup_check_enabled(rtti::context& ctx) -> bool
{
    return ctx.get_cached<project_manager>().get_editor_settings().updates.check_at_startup;
}

/// A version as a package names it: without the commit.
auto get_update_display_version(version::engine_version version) -> std::string
{
    version.sha.clear();
    return version.to_string();
}

void show_update_answer(ImGuiToastType type, const std::string& title, const std::string& content)
{
    ImGuiToast toast(type, UPDATE_ANSWER_DURATION_MS);
    toast.set_title("%s", title.c_str());
    toast.set_content("%s", content.c_str());
    toast.set_horizontal_pos(ImGuiToastHorizontalPos_Left);
    ImGui::PushNotification(UPDATE_TOAST_ID, toast);
}

void log_update_outcome(const update_check::check_outcome& outcome)
{
    if(!outcome.has_answer)
    {
        APPLOG_TRACE("Update check: no answer. {}", outcome.error);
        return;
    }
    if(!outcome.build)
    {
        APPLOG_TRACE("Update check: latest.json lists no build for {}.", update_check::get_own_platform());
        return;
    }
    APPLOG_TRACE("Update check: the newest build for {} is {} in {}.",
                 outcome.build->platform,
                 outcome.build->version.to_string(),
                 outcome.build->tag);
}

} // namespace

auto version_manager::init(rtti::context& ctx) -> bool
{
    own_version_ = update_check::get_own_version();
    state_path_ = update_check::get_update_state_path(update_check::get_own_platform());
    state_ = update_check::load_update_state(state_path_);
    if(!own_version_ || !is_startup_check_enabled(ctx))
    {
        return true;
    }
    // A due check announces with its own answer, so a build it no longer lists is never shown first.
    if(update_check::is_check_due(state_, std::chrono::system_clock::now()))
    {
        start_check(false);
        return true;
    }
    if(auto build = update_check::get_build_to_announce(state_, *own_version_, false))
    {
        announce_update(*build);
    }
    return true;
}

auto version_manager::deinit(rtti::context& ctx) -> bool
{
    is_stopping_->store(true);
    return true;
}

void version_manager::check_for_update_async()
{
    if(!own_version_)
    {
        show_update_answer(ImGuiToastType_Warning,
                           "This editor has no version.",
                           "It was built from sources without their tags, so it cannot be compared with a release.");
        return;
    }
    start_check(true);
}

void version_manager::start_check(bool is_manual)
{
    if(is_checking_)
    {
        is_manual_check_ = is_manual_check_ || is_manual;
        return;
    }
    is_checking_ = true;
    is_manual_check_ = is_manual;
    auto is_stopping = is_stopping_;
    tpp::async(
        [is_stopping]()
        {
            return update_check::fetch_update(
                [is_stopping]()
                {
                    return is_stopping->load();
                });
        })
        .then(tpp::this_thread::get_id(),
              [this, is_stopping](auto result)
              {
                  // Once the editor closes, the manager may be gone.
                  if(is_stopping->load())
                  {
                      return;
                  }
                  apply_outcome(result.get());
              });
}

void version_manager::apply_outcome(const update_check::check_outcome& outcome)
{
    const bool is_manual = is_manual_check_;
    is_checking_ = false;
    is_manual_check_ = false;
    log_update_outcome(outcome);
    change_state(
        [&outcome](update_check::update_state& state)
        {
            update_check::record_outcome(state, outcome, std::chrono::system_clock::now());
        });
    if(auto build = update_check::get_build_to_announce(state_, *own_version_, is_manual))
    {
        announce_update(*build);
        return;
    }
    if(is_manual)
    {
        show_manual_answer(outcome);
    }
}

void version_manager::announce_update(const update_check::release_build& build)
{
    const std::string version_text = build.version.to_string();
    const std::string title = "Unravel Engine " + version_text + " is available.";
    const std::string content = "This editor is " + get_update_display_version(*own_version_) + ".";
    const std::string page_url = update_check::get_release_page_url(build.tag);
    APPLOG_INFO("{} {}", title, page_url);
    ImGuiToast toast(ImGuiToastType_Info, UPDATE_NOTICE_DURATION_MS);
    toast.set_title("%s", title.c_str());
    toast.set_content("%s", content.c_str());
    toast.set_horizontal_pos(ImGuiToastHorizontalPos_Left);
    // The dismiss button is "Not now": this build is not announced again, a newer one is.
    toast.set_show_dismiss_button(true);
    toast.set_on_dismiss(
        [this, is_stopping = is_stopping_, version_text]()
        {
            if(!is_stopping->load())
            {
                dismiss_update(version_text);
            }
        });
    toast.set_draw_callback(
        [page_url](const ImGuiToast&, float, const ImVec4&)
        {
            if(ImGui::Button("Open the download page"))
            {
                ImGui::OpenInShell(page_url.c_str());
            }
        });
    ImGui::PushNotification(UPDATE_TOAST_ID, toast);
}

void version_manager::show_manual_answer(const update_check::check_outcome& outcome)
{
    if(!outcome.has_answer)
    {
        show_update_answer(ImGuiToastType_Warning,
                           "The update information could not be read.",
                           "GitHub could not be reached or answered with an error. Check the connection and try again.");
        return;
    }
    if(!outcome.build)
    {
        show_update_answer(ImGuiToastType_Info,
                           "No build for this platform is published.",
                           "This editor is built for " + std::string(update_check::get_own_platform()) + ".");
        return;
    }
    show_update_answer(ImGuiToastType_Success,
                       "This editor is up to date.",
                       "The newest published build is " + outcome.build->version.to_string() + ". This editor is " +
                           get_update_display_version(*own_version_) + ".");
}

void version_manager::dismiss_update(const std::string& version)
{
    change_state(
        [&version](update_check::update_state& state)
        {
            state.dismissed_version = version;
        });
}

void version_manager::change_state(const std::function<void(update_check::update_state&)>& change)
{
    // Other editors of this platform share the file: a change goes onto what it holds now, not onto what this
    // editor read at start, so their answers and dismissals stay.
    state_ = update_check::load_update_state(state_path_);
    change(state_);
    if(!update_check::save_update_state(state_path_, state_))
    {
        APPLOG_TRACE("Update check: {} could not be written.", state_path_.string());
    }
}

} // namespace unravel
