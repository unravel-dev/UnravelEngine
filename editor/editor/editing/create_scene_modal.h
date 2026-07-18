#pragma once

#include <engine/defaults/defaults.h>
#include <functional>

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

private:
    create_scene_modal() = default;
    static auto get_instance() -> create_scene_modal&;

    std::function<void(defaults::scene_preset)> callback_;
    defaults::scene_preset selected_preset_{defaults::scene_preset::medium};
    bool show_requested_{};
    bool open_requested_{};
};

} // namespace unravel
