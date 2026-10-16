#pragma once
#include "inspector.h"

#include <editor/editing/animation_preview.h>
#include <engine/animation/animation.h>
#include <engine/assets/asset_handle.h>

#include <vector>

namespace unravel
{
struct animation_importer_meta;

/**
 * @brief Authoring view of a clip's events and curves, which live in the clip's import settings.
 *
 * A timeline shows the event markers over the curves; markers drag along it, a double click adds
 * one, and the playhead scrubs. With a preview entity picked, the playhead poses that scene entity
 * with the clip (paused and seeked, so nothing else drives it) and playback flashes the markers it
 * crosses. Below the timeline sit the editors of the selected event and of every curve.
 */
class animation_clip_timeline
{
public:
    auto draw_events(rtti::context& ctx, const asset_handle<animation_clip>& clip, animation_importer_meta& importer)
        -> inspect_result;
    auto draw_curves(rtti::context& ctx, const asset_handle<animation_clip>& clip, animation_importer_meta& importer)
        -> inspect_result;

private:
    struct track_result
    {
        bool changed{};
        bool edit_finished{};
    };

    void draw_preview_bar(rtti::context& ctx, const asset_handle<animation_clip>& clip, float duration);
    auto draw_track(const char* id, animation_importer_meta& importer, float duration, bool edit_events) -> track_result;
    auto draw_event_list(animation_importer_meta& importer, float duration) -> track_result;
    auto draw_selected_event(animation_event& event, float duration) -> track_result;
    void add_event(animation_importer_meta& importer, float position, track_result& result);
    /// Removes an event; the selection, a drag or a context menu on any event ends with it.
    void erase_event(animation_importer_meta& importer, int index, track_result& result);
    void advance_playback(const animation_importer_meta& importer, float duration);
    void set_playhead(float progress);
    void select_clip(const asset_handle<animation_clip>& clip);

    animation_clip_preview preview_;
    entt::handle preview_choice_;
    asset_handle<animation_clip> clip_;
    int selected_event_{-1};
    int dragging_event_{-1};
    /// Where the dragged marker was when pressed, and where the drag last put it.
    float drag_start_position_{};
    float drag_position_{};
    int context_event_{-1};
    /// Where the track was right-clicked, for Add Event Here.
    float context_position_{};
    bool scrubbing_{};
    float playhead_{};
    bool playing_{};
    /// ImGui time until which each event marker shows as just fired.
    std::vector<double> flash_until_;
};

} // namespace unravel
