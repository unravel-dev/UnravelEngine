#pragma once

#include <context/context.hpp>
#include <deploy/profile.h>

#include <array>
#include <string>

namespace unravel
{

/**
 * @brief The New Profile dialog of the Deploy and Project Settings windows: the platform as cards, the profile's
 * name, and what it starts from - the platform's starting values or a copy of a profile. A modal over the window that
 * opens it; the project manager creates the profile, and the window shows it.
 */
class new_profile_dialog
{
public:
    /// Opens the dialog at its next draw, with `platform` chosen.
    void open(target_platform platform);

    /**
     * @brief Draws the dialog while it is open. Call it every frame from the window that opens it, inside that
     * window: the dialog is a popup of it.
     * @return The name of the profile created this frame; empty otherwise.
     */
    auto draw(rtti::context& ctx) -> std::string;

private:
    /// The longest name the dialog takes, with its terminating zero.
    static constexpr std::size_t NAME_CAPACITY = 128;

    void draw_platform_cards(rtti::context& ctx);
    /// @return Whether Enter was pressed in the field.
    auto draw_name_field(rtti::context& ctx) -> bool;
    void draw_source_field(rtti::context& ctx);
    /// Cancel and Create Profile; @return whether Create Profile was pressed.
    auto draw_footer(bool can_create) -> bool;
    void choose_platform(rtti::context& ctx, target_platform platform);
    auto create(rtti::context& ctx) -> std::string;

    std::array<char, NAME_CAPACITY> name_{};
    /// Whether the name was typed: until it is, the name follows the platform (Android, Android 2, ...).
    bool is_name_typed_{};
    target_platform platform_{target_platform::desktop};
    /// The profile the new one copies; empty: the platform's starting values.
    std::string source_;
    /// Why the profile could not be created; empty while nothing failed.
    std::string error_;
    bool is_open_requested_{};
    /// Whether a picker of the dialog was open at the end of the last frame: an Escape then closes only the picker.
    bool had_open_picker_{};
};

} // namespace unravel
