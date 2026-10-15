#pragma once

#include <engine/defaults/defaults.h>
#include <functional>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief Modal for selecting a scene preset when creating a new scene.
 * Call ShowCreateSceneModal() to open; RenderCreateSceneModal() draws it each frame.
 */
class create_scene_modal
{
public:
    /**
     * @brief Request the create scene modal to open.
     * @param on_preset_selected Called when the user selects a preset (or Cancel to dismiss).
     */
    static void show(std::function<void(defaults::scene_preset)> on_preset_selected);

    /**
     * @brief Draw the modal. Call every frame from the main UI render loop.
     */
    static void render();

    /// True when Show was requested and the user has not yet confirmed/cancelled.
    static auto is_pending() -> bool;

    /// Headless path (MCP/CLI): if a create-scene modal is pending, invoke its
    /// callback with `preset` and dismiss it. Returns true when a pending modal
    /// was completed.
    static auto complete_if_pending(defaults::scene_preset preset) -> bool;

    /// Dismiss a pending create-scene modal without invoking the callback.
    static void cancel_if_pending();

    /// Headless path (MCP): answer a pending modal as a click on the button or preset card labelled
    /// @p label would ("Create", "Cancel", "Standard", ..., case-insensitive). False when nothing is
    /// pending or no button or card has that label.
    static auto answer_if_pending(const std::string& label) -> bool;

    /// The labels answer_if_pending takes: the two buttons, then the preset cards.
    static auto get_answer_labels() -> std::vector<std::string>;

    static auto get_title() -> const char*;
    static auto get_message() -> const char*;

private:
    create_scene_modal() = default;
    static auto get_instance() -> create_scene_modal&;

    std::function<void(defaults::scene_preset)> callback_;
    defaults::scene_preset selected_preset_{defaults::scene_preset::medium};
    bool show_requested_{};
    bool open_requested_{};
};

} // namespace unravel
