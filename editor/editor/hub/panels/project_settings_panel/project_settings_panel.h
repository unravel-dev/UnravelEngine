#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <editor/editing/editor_actions.h>
#include <editor/hub/panels/settings_view.h>
#include <engine/settings/settings.h>

#include <memory>
#include <string>

namespace unravel
{

class imgui_panels;

/// What the Project Settings window edits: the project settings, or a profile's settings, where an edit
/// becomes an override of that profile.
struct settings_edit_target
{
    /// The profile edited; empty for the project settings.
    std::string profile;
    /// The profile's settings as shown and edited.
    settings shown;
    /// The profile's settings the edit in progress started from: only what the edit changes is stored.
    settings before;
    /// What the last edit could not do; empty when it succeeded.
    std::string error;
    /// A profile that was edited and no longer exists (deleted or renamed elsewhere); empty for none.
    std::string missing_profile;
};

class project_settings_panel
{
public:
    project_settings_panel(imgui_panels* parent);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    /// Shows or hides the window. A hint that names a category opens it on that category.
    void show(bool s, const std::string& hint);
    /// Shows the window editing the settings of `profile`, or the project settings for an empty name.
    void show_profile(const std::string& profile);

private:
    imgui_panels* parent_{};
    bool visible_{};
    /// Owned here, at one address: the categories' draws refer to it.
    std::unique_ptr<settings_edit_target> edit_target_;
    settings_view view_;
};
} // namespace unravel
