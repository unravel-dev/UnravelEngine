#pragma once

#include <context/context.hpp>
#include <deploy/deploy_settings.h>

#include <hpp/string_view.hpp>

#include <string>

namespace unravel
{
class project_manager;

/**
 * @brief The deploy settings the Deploy window shows and edits: the ones every profile shares, or the
 * deploy profile's, where an edit becomes an override of that profile.
 *
 * Call sync at the start of every frame the window draws: it takes the project's current settings and deploy
 * profile unless an item is being edited. After an edit is finished, commit writes what the edit changed
 * where edits go, over the settings as they are then.
 */
class deploy_settings_editor
{
public:
    void sync(rtti::context& ctx);
    /// The settings shown, to inspect and edit.
    auto get() -> deploy_settings&;
    /// Writes the shown settings: the shared ones, or the deploy profile's overrides.
    void commit(rtti::context& ctx);

    /// Whether edits go to the deploy profile instead of the shared settings.
    auto is_profile_mode() const -> bool;
    void set_profile_mode(rtti::context& ctx, bool is_profile_mode);
    /// The deploy profile.
    auto get_profile() const -> const std::string&;

    /// Stops the deploy profile overriding a deploy settings property.
    void revert(rtti::context& ctx, hpp::string_view path);

    /// What the last commit could not do; empty when it succeeded.
    auto get_error() const -> const std::string&;

private:
    /// Takes the settings the window shows from the project.
    void sync_shown(project_manager& pm);

    deploy_settings shown_;
    /// The settings the edit in progress started from.
    deploy_settings before_;
    std::string profile_;
    std::string error_;
    bool is_profile_mode_{};
};

} // namespace unravel
