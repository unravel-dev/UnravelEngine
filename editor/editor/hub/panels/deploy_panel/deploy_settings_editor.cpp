#include "deploy_settings_editor.h"

#include <deploy/meta/deploy_settings.hpp>
#include <deploy/profile.h>
#include <editor/system/project_manager.h>
#include <serialization/sparse_document.h>

#include <imgui/imgui.h>

namespace unravel
{

void deploy_settings_editor::sync(rtti::context& ctx)
{
    // An edit in progress keeps its profile and values until it is finished and committed.
    if(ImGui::IsAnyItemActive())
    {
        return;
    }
    auto& pm = ctx.get_cached<project_manager>();
    profile_ = pm.get_deploy_profile();
    sync_shown(pm);
    before_ = shown_;
}

void deploy_settings_editor::sync_shown(project_manager& pm)
{
    if(is_profile_mode_)
    {
        if(auto resolved = pm.get_profile_deploy_settings(profile_))
        {
            shown_ = std::move(*resolved);
            return;
        }
    }
    shown_ = pm.get_deploy_request();
    if(!is_profile_mode_)
    {
        // The shared settings themselves, with the deploy profile's own fields for what the window shows
        // beside them (its platform, its folder).
        auto& shared = pm.get_deploy_settings();
        const auto profile_fields = shown_;
        shown_ = shared;
        shown_.profile = profile_fields.profile;
        shown_.deploy_target = profile_fields.deploy_target;
        shown_.android = profile_fields.android;
    }
}

auto deploy_settings_editor::get() -> deploy_settings&
{
    return shown_;
}

void deploy_settings_editor::commit(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    error_.clear();
    if(is_profile_mode_)
    {
        const auto changed = pm.override_profile_deploy(ctx, profile_, before_, shown_);
        error_ = changed.error;
        return;
    }
    // The profile's own fields and the device are no shared settings and stay out of their document.
    auto& shared = pm.get_deploy_settings();
    if(!serialization::apply_edit(before_, shown_, SETTINGS_DOCUMENT_ROOT, shared).empty())
    {
        pm.save_deploy_settings();
    }
}

auto deploy_settings_editor::is_profile_mode() const -> bool
{
    return is_profile_mode_;
}

void deploy_settings_editor::set_profile_mode(rtti::context& ctx, bool is_profile_mode)
{
    is_profile_mode_ = is_profile_mode;
    error_.clear();
    sync(ctx);
}

auto deploy_settings_editor::get_profile() const -> const std::string&
{
    return profile_;
}

void deploy_settings_editor::revert(rtti::context& ctx, hpp::string_view path)
{
    const auto reverted = ctx.get_cached<project_manager>().revert_profile_deploy(ctx, profile_, path);
    error_ = reverted.error;
}

auto deploy_settings_editor::get_error() const -> const std::string&
{
    return error_;
}

} // namespace unravel
