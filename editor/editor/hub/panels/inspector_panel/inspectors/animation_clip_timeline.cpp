#include "animation_clip_timeline.h"
#include "inspectors.h"

#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/animation_panel/animation_controller_ui.h>
#include <editor/shortcuts.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/assets/asset_storage.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/play_mode.h>

#include <imgui/imgui.h>
#include <logging/logging.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <string>

namespace unravel
{
namespace
{
constexpr float TRACK_HEIGHT = 72.0f;
constexpr float RULER_HEIGHT = 18.0f;
constexpr float MARKER_HALF_SIZE = 6.0f;
/// Extra pixels around a marker that still pick it.
constexpr float MARKER_PICK_SLACK = 3.0f;
/// Major ruler ticks are at least this far apart, in pixels.
constexpr float MIN_TICK_SPACING = 64.0f;
/// Samples per curve drawn on the track.
constexpr int CURVE_SAMPLES = 96;
/// How long a marker the playhead crossed stays lit, in seconds.
constexpr double FLASH_SECONDS = 0.35;
/// Shift-dragging snaps markers to frames of this rate.
constexpr float SNAP_FRAME_RATE = 30.0f;
constexpr float MIN_DURATION = 1e-3f;
/// The smallest value range the curve overlay stretches over the lane height.
constexpr float MIN_CURVE_SPAN = 1e-3f;
/// Width of the preview entity picker, at most, and the share of the row it takes at most.
constexpr float PREVIEW_PICKER_WIDTH = 220.0f;
constexpr float PREVIEW_PICKER_MAX_SHARE = 0.5f;
/// Room the value and delete button of a curve row keep beside its name.
constexpr float CURVE_ROW_TRAILER = 150.0f;
constexpr float MIN_CURVE_NAME_WIDTH = 80.0f;
constexpr float MIN_TRACK_WIDTH = 120.0f;
constexpr float TRACK_ROUNDING = 3.0f;
/// Ruler tick heights and the offset of the time labels from their tick, in pixels.
constexpr float MAJOR_TICK_HEIGHT = 6.0f;
constexpr float MINOR_TICK_HEIGHT = 3.0f;
constexpr ImVec2 RULER_LABEL_OFFSET(4.0f, 1.0f);
/// Room kept between the curves and the top and bottom of the lane, in pixels.
constexpr float CURVE_LANE_PADDING = 4.0f;
constexpr float CURVE_THICKNESS = 1.5f;
/// Gap between the top of the lane and the marker row, and between a marker and its label, in pixels.
constexpr float MARKER_TOP_MARGIN = 6.0f;
constexpr float MARKER_LABEL_GAP = 2.0f;
constexpr float MARKER_OUTLINE_THICKNESS = 1.5f;
constexpr float PLAYHEAD_THICKNESS = 2.0f;
/// The playhead's handle on the ruler: half its width and its height, in pixels.
constexpr float PLAYHEAD_HANDLE_HALF_WIDTH = 5.0f;
constexpr float PLAYHEAD_HANDLE_HEIGHT = 7.0f;
/// Pixels the mouse moves with a marker pressed before the marker drags.
constexpr float MARKER_DRAG_THRESHOLD = 1.0f;
/// Alpha of the curve lines, of the markers on the read-only curve track and of a marker's line down the lane.
constexpr ImU32 CURVE_ALPHA = 150;
constexpr ImU32 MUTED_MARKER_ALPHA = 110;
constexpr ImU32 MARKER_LINE_ALPHA = 70;
const ImVec4 COL_NAME_PROBLEM(1.0f, 0.72f, 0.3f, 1.0f);
constexpr ImU32 COL_PLAYHEAD_LINE = IM_COL32(255, 140, 40, 255);
constexpr ImU32 COL_MARKER = IM_COL32(200, 200, 210, 255);
constexpr ImU32 COL_MARKER_SELECTED = IM_COL32(90, 170, 255, 255);
constexpr ImU32 COL_MARKER_FIRED = IM_COL32(255, 230, 90, 255);
constexpr ImU32 COL_MARKER_OUTLINE = IM_COL32(255, 255, 255, 255);
constexpr std::array<ImU32, 6> CURVE_COLORS = {IM_COL32(110, 210, 120, 255), IM_COL32(230, 120, 200, 255),
                                               IM_COL32(120, 190, 240, 255), IM_COL32(240, 200, 90, 255),
                                               IM_COL32(200, 140, 255, 255), IM_COL32(240, 120, 100, 255)};
constexpr std::array<float, 14> TICK_STEPS = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.25f, 0.5f,
                                              1.0f,  2.0f,  5.0f,  10.0f, 20.0f, 30.0f, 60.0f};

auto with_alpha(ImU32 color, ImU32 alpha) -> ImU32
{
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

/// A dimmed hint that wraps at the panel edge.
void draw_hint(const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

auto get_clip_duration(const asset_handle<animation_clip>& clip) -> float
{
    const auto clip_ptr = clip.try_get();
    if(clip_ptr && !asset_handle<animation_clip>::is_empty(clip_ptr) && clip_ptr->duration.count() > 0.0)
    {
        return std::max(float(clip_ptr->duration.count()), MIN_DURATION);
    }
    return 1.0f;
}

auto get_entity_label(entt::handle entity) -> std::string
{
    auto* tag = entity && entity.valid() ? entity.try_get<tag_component>() : nullptr;
    return tag && !tag->name.empty() ? tag->name : std::string("Entity");
}

/// The entity's name with its parent's, so entities that share a name can be told apart.
auto get_entity_label_with_parent(entt::handle entity) -> std::string
{
    const auto* transform = entity && entity.valid() ? entity.try_get<transform_component>() : nullptr;
    const auto parent = transform ? transform->get_parent() : entt::handle{};
    return parent ? get_entity_label(entity) + "  (" + get_entity_label(parent) + ")" : get_entity_label(entity);
}

/// The event's parameters that are set, for the event list; empty when none is.
auto describe_event_parameters(const animation_event& event) -> std::string
{
    std::string text = event.string_parameter;
    const auto append = [&](const std::string& part)
    {
        text += (text.empty() ? "" : ", ") + part;
    };
    if(event.float_parameter != 0.0f)
    {
        char value[32];
        ImFormatString(value, sizeof(value), "%.2f", event.float_parameter);
        append(value);
    }
    if(event.int_parameter != 0)
    {
        append(std::to_string(event.int_parameter));
    }
    return text;
}

/// Why a curve name does not work as a lookup key, or nullptr.
auto get_curve_name_problem(const std::vector<animation_curve>& curves, size_t index) -> const char*
{
    const auto& name = curves[index].name;
    if(name.empty())
    {
        return "A curve needs a name: parameters and IK weight curves find it by name.";
    }
    for(size_t other = 0; other < curves.size(); ++other)
    {
        if(other != index && curves[other].name == name)
        {
            return "Another curve has this name: only one of them is found by it.";
        }
    }
    return nullptr;
}

auto pick_tick_step(float duration, float width) -> float
{
    for(float step : TICK_STEPS)
    {
        if(step / duration * width >= MIN_TICK_SPACING)
        {
            return step;
        }
    }
    return TICK_STEPS.back();
}

/// The nearest frame, within the clip: the last frame of a clip that is not a whole number of
/// frames long lies past its end.
auto snap_to_frame(float progress, float duration) -> float
{
    const float frames = duration * SNAP_FRAME_RATE;
    return frames > 0.0f ? std::clamp(std::round(progress * frames) / frames, 0.0f, 1.0f) : progress;
}

/// The value range every curve of the clip spans, at least 0..1.
auto get_curve_range(const std::vector<animation_curve>& curves) -> ImVec2
{
    ImVec2 range(0.0f, 1.0f);
    for(const auto& curve : curves)
    {
        for(int i = 0; i <= CURVE_SAMPLES; ++i)
        {
            const float value = curve.curve.sample(float(i) / float(CURVE_SAMPLES));
            range.x = std::min(range.x, value);
            range.y = std::max(range.y, value);
        }
    }
    return range;
}

void draw_ruler(ImDrawList* draw_list, const ImRect& rect, float duration)
{
    const float width = rect.GetWidth();
    const float step = pick_tick_step(duration, width);
    const ImU32 tick_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    for(float time = 0.0f; time <= duration + step * 0.5f; time += step)
    {
        const float x = rect.Min.x + std::min(time / duration, 1.0f) * width;
        draw_list->AddLine(ImVec2(x, rect.Min.y + RULER_HEIGHT - MAJOR_TICK_HEIGHT),
                           ImVec2(x, rect.Min.y + RULER_HEIGHT),
                           tick_color);
        char label[32];
        ImFormatString(label, sizeof(label), "%.2fs", time);
        draw_list->AddText(ImVec2(x + RULER_LABEL_OFFSET.x, rect.Min.y + RULER_LABEL_OFFSET.y), tick_color, label);
        const float minor_x = rect.Min.x + std::min((time + step * 0.5f) / duration, 1.0f) * width;
        draw_list->AddLine(ImVec2(minor_x, rect.Min.y + RULER_HEIGHT - MINOR_TICK_HEIGHT),
                           ImVec2(minor_x, rect.Min.y + RULER_HEIGHT),
                           tick_color);
    }
    draw_list->AddLine(ImVec2(rect.Min.x, rect.Min.y + RULER_HEIGHT),
                       ImVec2(rect.Max.x, rect.Min.y + RULER_HEIGHT),
                       ImGui::GetColorU32(ImGuiCol_Border));
}

void draw_curves_overlay(ImDrawList* draw_list, const ImRect& lane, const std::vector<animation_curve>& curves)
{
    if(curves.empty())
    {
        return;
    }
    const ImVec2 range = get_curve_range(curves);
    const float span = std::max(range.y - range.x, MIN_CURVE_SPAN);
    const float drawn_height = lane.GetHeight() - CURVE_LANE_PADDING * 2.0f;
    std::vector<ImVec2> points(CURVE_SAMPLES + 1);
    for(size_t c = 0; c < curves.size(); ++c)
    {
        for(int i = 0; i <= CURVE_SAMPLES; ++i)
        {
            const float progress = float(i) / float(CURVE_SAMPLES);
            const float value = (curves[c].curve.sample(progress) - range.x) / span;
            points[size_t(i)] =
                ImVec2(lane.Min.x + progress * lane.GetWidth(), lane.Max.y - CURVE_LANE_PADDING - value * drawn_height);
        }
        const ImU32 color = with_alpha(CURVE_COLORS[c % CURVE_COLORS.size()], CURVE_ALPHA);
        draw_list->AddPolyline(points.data(), int(points.size()), color, ImDrawFlags_None, CURVE_THICKNESS);
    }
}

void draw_marker(ImDrawList* draw_list, const ImVec2& center, ImU32 color, bool outlined)
{
    const ImVec2 top(center.x, center.y - MARKER_HALF_SIZE);
    const ImVec2 right(center.x + MARKER_HALF_SIZE, center.y);
    const ImVec2 bottom(center.x, center.y + MARKER_HALF_SIZE);
    const ImVec2 left(center.x - MARKER_HALF_SIZE, center.y);
    draw_list->AddQuadFilled(top, right, bottom, left, color);
    if(outlined)
    {
        draw_list->AddQuad(top, right, bottom, left, COL_MARKER_OUTLINE, MARKER_OUTLINE_THICKNESS);
    }
}

/// Index of the event marker under `mouse_x`, or -1.
auto find_marker(const std::vector<animation_event>& events, const ImRect& lane, float mouse_x) -> int
{
    int best = -1;
    float best_distance = MARKER_HALF_SIZE + MARKER_PICK_SLACK;
    for(size_t i = 0; i < events.size(); ++i)
    {
        const float x = lane.Min.x + events[i].position * lane.GetWidth();
        const float distance = std::fabs(mouse_x - x);
        if(distance <= best_distance)
        {
            best_distance = distance;
            best = int(i);
        }
    }
    return best;
}
} // namespace

void animation_clip_timeline::select_clip(const asset_handle<animation_clip>& clip)
{
    if(clip_ == clip)
    {
        return;
    }
    clip_ = clip;
    preview_.stop();
    selected_event_ = -1;
    dragging_event_ = -1;
    context_event_ = -1;
    scrubbing_ = false;
    playing_ = false;
    playhead_ = 0.0f;
    flash_until_.clear();
}

void animation_clip_timeline::add_event(animation_importer_meta& importer, float position, track_result& result)
{
    animation_event added;
    added.name = "Event";
    added.position = std::clamp(position, 0.0f, 1.0f);
    importer.events.push_back(added);
    selected_event_ = int(importer.events.size()) - 1;
    result.changed = true;
    result.edit_finished = true;
}

void animation_clip_timeline::erase_event(animation_importer_meta& importer, int index, track_result& result)
{
    importer.events.erase(importer.events.begin() + index);
    if(size_t(index) < flash_until_.size())
    {
        flash_until_.erase(flash_until_.begin() + index);
    }
    // Every index into the list may name another event now.
    selected_event_ = -1;
    dragging_event_ = -1;
    context_event_ = -1;
    result.changed = true;
    result.edit_finished = true;
}

void animation_clip_timeline::set_playhead(float progress)
{
    playhead_ = std::clamp(progress, 0.0f, 1.0f);
    preview_.set_progress(playhead_);
}

void animation_clip_timeline::advance_playback(const animation_importer_meta& importer, float duration)
{
    if(!playing_)
    {
        return;
    }
    const float previous = playhead_;
    float next = playhead_ + ImGui::GetIO().DeltaTime / duration;
    const bool wrapped = next >= 1.0f;
    next = wrapped ? next - std::floor(next) : next;
    flash_until_.resize(importer.events.size(), 0.0);
    const double now = ImGui::GetTime();
    for(size_t i = 0; i < importer.events.size(); ++i)
    {
        const float position = importer.events[i].position;
        const bool crossed = wrapped ? (position > previous || position <= next) : (position > previous && position <= next);
        if(crossed)
        {
            flash_until_[i] = now + FLASH_SECONDS;
        }
    }
    set_playhead(next);
}

void animation_clip_timeline::draw_preview_bar(rtti::context& ctx, const asset_handle<animation_clip>& clip, float duration)
{
    const bool in_play_mode = ctx.get_cached<play_mode>().is_active();
    // The entities of the scene being edited: the prefab's in prefab mode.
    auto* active_scene = ctx.get_cached<editing_manager>().get_active_scene(ctx);
    const bool is_choice_usable = preview_choice_ && preview_choice_.valid() &&
                                  preview_choice_.all_of<animation_component>() && active_scene != nullptr &&
                                  preview_choice_.registry() == active_scene->registry.get();
    if(preview_choice_ && !is_choice_usable)
    {
        preview_choice_ = {};
        preview_.stop();
    }
    ImGui::BeginDisabled(in_play_mode);
    ImGui::SetNextItemWidth(
        std::min(PREVIEW_PICKER_WIDTH, ImGui::GetContentRegionAvail().x * PREVIEW_PICKER_MAX_SHARE));
    const std::string label = preview_choice_ ? get_entity_label(preview_choice_) : std::string("Preview entity...");
    if(ImGui::BeginCombo("##preview_entity", (ICON_MDI_HUMAN " " + label).c_str()))
    {
        if(ImGui::Selectable("None", !preview_choice_))
        {
            preview_choice_ = {};
            preview_.stop();
        }
        if(active_scene != nullptr)
        {
            auto& registry = *active_scene->registry;
            registry.view<animation_component>().each(
                [&](auto e, auto&&)
                {
                    entt::handle handle(registry, e);
                    const std::string item = get_entity_label_with_parent(handle) + "##" + std::to_string(uint32_t(e));
                    if(ImGui::Selectable(item.c_str(), preview_choice_ == handle))
                    {
                        preview_choice_ = handle;
                        preview_.stop();
                    }
                });
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltipEx("Scene entity the playhead poses with this clip. The scene gets its pose back when the "
                            "preview stops.");
    ImGui::SameLine();
    const bool previewing = preview_.is_active();
    ImGui::BeginDisabled(!preview_choice_);
    if(ImGui::Button(previewing ? ICON_MDI_STOP " Stop" : ICON_MDI_EYE " Preview"))
    {
        if(previewing)
        {
            preview_.stop();
            playing_ = false;
        }
        else if(preview_.start(ctx, preview_choice_, clip))
        {
            set_playhead(playhead_);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button(playing_ ? ICON_MDI_PAUSE : ICON_MDI_PLAY))
    {
        playing_ = !playing_;
    }
    ImGui::SetItemTooltipEx("Play the playhead; markers it crosses light up.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%.3fs / %.3fs", playhead_ * duration, duration);
    if(in_play_mode)
    {
        preview_.stop();
        playing_ = false;
    }
    preview_.keep_alive();
}

auto animation_clip_timeline::draw_track(const char* id, animation_importer_meta& importer, float duration, bool edit_events)
    -> track_result
{
    track_result result;
    auto& events = importer.events;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, MIN_TRACK_WIDTH);
    const ImRect rect(origin, ImVec2(origin.x + width, origin.y + TRACK_HEIGHT));
    const ImRect lane(ImVec2(rect.Min.x, rect.Min.y + RULER_HEIGHT), rect.Max);
    ImGui::InvisibleButton(id, rect.GetSize(), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float mouse_progress = std::clamp((mouse.x - lane.Min.x) / lane.GetWidth(), 0.0f, 1.0f);
    const int hovered_marker = hovered && lane.Contains(mouse) ? find_marker(events, lane, mouse.x) : -1;

    // An undo during a drag puts other settings in place: the drag ends there, without an edit.
    const bool is_drag_target_gone = dragging_event_ >= int(events.size()) ||
                                     (dragging_event_ >= 0 && events[size_t(dragging_event_)].position != drag_position_);
    if(is_drag_target_gone)
    {
        dragging_event_ = -1;
    }
    // Interaction: markers select and drag, empty space scrubs, a double click adds an event.
    if(ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        if(edit_events && hovered_marker >= 0)
        {
            selected_event_ = hovered_marker;
            dragging_event_ = hovered_marker;
            drag_start_position_ = events[size_t(hovered_marker)].position;
            drag_position_ = drag_start_position_;
        }
        else
        {
            scrubbing_ = true;
            playing_ = false;
        }
    }
    if(edit_events && hovered && hovered_marker < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        add_event(importer, mouse_progress, result);
        scrubbing_ = false;
    }
    if(active && dragging_event_ >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, MARKER_DRAG_THRESHOLD))
    {
        const float position = ImGui::GetIO().KeyShift ? snap_to_frame(mouse_progress, duration) : mouse_progress;
        if(drag_position_ != position)
        {
            events[size_t(dragging_event_)].position = position;
            drag_position_ = position;
            result.changed = true;
        }
    }
    if(scrubbing_ && active)
    {
        set_playhead(mouse_progress);
    }
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // A marker clicked and let go where it was is no edit.
        if(dragging_event_ >= 0 && drag_position_ != drag_start_position_)
        {
            result.edit_finished = true;
        }
        dragging_event_ = -1;
        scrubbing_ = false;
    }
    if(edit_events && ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        context_event_ = hovered_marker;
        context_position_ = mouse_progress;
        if(hovered_marker >= 0)
        {
            selected_event_ = hovered_marker;
        }
        ImGui::OpenPopup("event_marker_menu");
    }

    // Drawing.
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), TRACK_ROUNDING);
    draw_ruler(draw_list, rect, duration);
    draw_curves_overlay(draw_list, lane, importer.curves);
    const double now = ImGui::GetTime();
    flash_until_.resize(events.size(), 0.0);
    const float marker_y = lane.Min.y + MARKER_HALF_SIZE + MARKER_TOP_MARGIN;
    // Labels of markers near the end stop at the track's edge.
    const ImVec4 label_clip(rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
    for(size_t i = 0; i < events.size(); ++i)
    {
        const float x = lane.Min.x + events[i].position * lane.GetWidth();
        const bool selected = edit_events && int(i) == selected_event_;
        ImU32 color = flash_until_[i] > now ? COL_MARKER_FIRED : (selected ? COL_MARKER_SELECTED : COL_MARKER);
        if(!edit_events)
        {
            color = with_alpha(color, MUTED_MARKER_ALPHA);
        }
        draw_list->AddLine(ImVec2(x, marker_y), ImVec2(x, lane.Max.y), with_alpha(color, MARKER_LINE_ALPHA));
        draw_marker(draw_list, ImVec2(x, marker_y), color, selected);
        if(edit_events)
        {
            draw_list->AddText(ImGui::GetFont(),
                               ImGui::GetFontSize(),
                               ImVec2(x + MARKER_HALF_SIZE + MARKER_LABEL_GAP, marker_y - ImGui::GetTextLineHeight() * 0.5f),
                               color,
                               events[i].name.c_str(),
                               nullptr,
                               0.0f,
                               &label_clip);
        }
    }
    const float playhead_x = lane.Min.x + playhead_ * lane.GetWidth();
    draw_list->AddLine(ImVec2(playhead_x, rect.Min.y), ImVec2(playhead_x, rect.Max.y), COL_PLAYHEAD_LINE, PLAYHEAD_THICKNESS);
    draw_list->AddTriangleFilled(ImVec2(playhead_x - PLAYHEAD_HANDLE_HALF_WIDTH, rect.Min.y),
                                 ImVec2(playhead_x + PLAYHEAD_HANDLE_HALF_WIDTH, rect.Min.y),
                                 ImVec2(playhead_x, rect.Min.y + PLAYHEAD_HANDLE_HEIGHT),
                                 COL_PLAYHEAD_LINE);
    if(hovered_marker >= 0 && dragging_event_ < 0)
    {
        const auto& event = events[size_t(hovered_marker)];
        ImGui::SetTooltip("%s\n%.3fs  (%.1f%%)", event.name.c_str(), event.position * duration, event.position * 100.0f);
    }

    if(ImGui::BeginPopup("event_marker_menu"))
    {
        const bool valid = context_event_ >= 0 && context_event_ < int(events.size());
        if(!valid && ImGui::MenuItem(ICON_MDI_PLUS " Add Event Here"))
        {
            add_event(importer, context_position_, result);
        }
        if(valid && ImGui::MenuItem(ICON_MDI_CONTENT_DUPLICATE " Duplicate"))
        {
            animation_event copy = events[size_t(context_event_)];
            copy.position = std::min(copy.position + 1.0f / std::max(duration * SNAP_FRAME_RATE, 1.0f), 1.0f);
            events.push_back(copy);
            selected_event_ = int(events.size()) - 1;
            result.changed = true;
            result.edit_finished = true;
        }
        if(valid && ImGui::MenuItem(ICON_MDI_DELETE " Delete", "Del"))
        {
            erase_event(importer, context_event_, result);
        }
        ImGui::EndPopup();
    }
    // Delete belongs to the focused window: with the content browser focused it deletes assets there.
    const bool has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if(edit_events && has_focus && selected_event_ >= 0 && selected_event_ < int(events.size()) && hovered &&
       !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(shortcuts::delete_item, false))
    {
        erase_event(importer, selected_event_, result);
    }
    return result;
}

auto animation_clip_timeline::draw_event_list(animation_importer_meta& importer, float duration) -> track_result
{
    track_result result;
    auto& events = importer.events;
    if(ImGui::Button(ICON_MDI_PLUS " Add at Playhead"))
    {
        add_event(importer, playhead_, result);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selected_event_ < 0 || selected_event_ >= int(events.size()));
    if(ImGui::Button(ICON_MDI_DELETE " Remove"))
    {
        erase_event(importer, selected_event_, result);
    }
    ImGui::EndDisabled();
    if(events.empty())
    {
        ImGui::TextDisabled("No events. Double-click the track to add one.");
        return result;
    }
    // Listed in time order; the stored order is left alone so the selection stays put.
    std::vector<size_t> order(events.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(),
                     order.end(),
                     [&](size_t lhs, size_t rhs)
                     {
                         return events[lhs].position < events[rhs].position;
                     });
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if(ImGui::BeginTable("clip_events", 4, flags))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Min Weight", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableHeadersRow();
        for(size_t index : order)
        {
            const auto& event = events[index];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(int(index));
            if(ImGui::Selectable(event.name.c_str(), selected_event_ == int(index), ImGuiSelectableFlags_SpanAllColumns))
            {
                selected_event_ = int(index);
                set_playhead(event.position);
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::Text("%.3fs", event.position * duration);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", event.min_weight);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", describe_event_parameters(event).c_str());
        }
        ImGui::EndTable();
    }
    return result;
}

auto animation_clip_timeline::draw_selected_event(animation_event& event, float duration) -> track_result
{
    track_result result;
    const auto track = [&]()
    {
        result.changed |= ImGui::IsItemEdited();
        result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
    };
    input_text_string("Name", event.name);
    track();
    ImGui::SetItemTooltipEx("Scripts dispatch on it in OnAnimationEvent.");
    float time = event.position * duration;
    if(ImGui::DragFloat("Time (s)", &time, 0.001f * duration, 0.0f, duration, "%.3f"))
    {
        event.position = std::clamp(time / duration, 0.0f, 1.0f);
    }
    track();
    ImGui::SliderFloat("Min Weight", &event.min_weight, 0.0f, 1.0f, "%.2f");
    track();
    ImGui::SetItemTooltipEx("Smallest share of the pose the clip needs for the event to fire: one half makes a "
                            "crossfade report it from one side only.");
    input_text_string("String", event.string_parameter);
    track();
    ImGui::DragFloat("Float", &event.float_parameter, 0.01f);
    track();
    ImGui::DragInt("Int", &event.int_parameter);
    track();
    return result;
}

auto animation_clip_timeline::draw_events(rtti::context& ctx,
                                          const asset_handle<animation_clip>& clip,
                                          animation_importer_meta& importer) -> inspect_result
{
    select_clip(clip);
    inspect_result result;
    const float duration = get_clip_duration(clip);
    advance_playback(importer, duration);
    draw_preview_bar(ctx, clip, duration);
    const auto track = draw_track("##event_track", importer, duration, true);
    draw_hint("Double-click to add, drag to move (Shift snaps to 30 fps frames), right-click for more.");
    const auto list = draw_event_list(importer, duration);
    result.changed |= track.changed || list.changed;
    result.edit_finished |= track.edit_finished || list.edit_finished;
    if(selected_event_ >= 0 && selected_event_ < int(importer.events.size()))
    {
        ImGui::SeparatorText("Selected Event");
        ImGui::PushID(selected_event_);
        const auto selected = draw_selected_event(importer.events[size_t(selected_event_)], duration);
        ImGui::PopID();
        result.changed |= selected.changed;
        result.edit_finished |= selected.edit_finished;
    }
    return result;
}

auto animation_clip_timeline::draw_curves(rtti::context& ctx,
                                          const asset_handle<animation_clip>& clip,
                                          animation_importer_meta& importer) -> inspect_result
{
    select_clip(clip);
    inspect_result result;
    const float duration = get_clip_duration(clip);
    advance_playback(importer, duration);
    draw_preview_bar(ctx, clip, duration);
    draw_track("##curve_track", importer, duration, false);
    draw_hint("A controller float parameter with a curve's name follows it; IK components take a curve name as "
              "their weight curve.");
    if(ImGui::Button(ICON_MDI_PLUS " Add Curve"))
    {
        animation_curve added;
        added.name = make_unique_name(
            [&](const std::string& name)
            {
                return std::any_of(importer.curves.begin(),
                                   importer.curves.end(),
                                   [&](const animation_curve& curve)
                                   {
                                       return curve.name == name;
                                   });
            },
            "Curve");
        importer.curves.push_back(added);
        result.changed = true;
        result.edit_finished = true;
    }
    for(size_t i = 0; i < importer.curves.size(); ++i)
    {
        auto& curve = importer.curves[i];
        ImGui::PushID(int(i));
        ImGui::Spacing();
        ImGui::ColorButton("##color",
                           ImGui::ColorConvertU32ToFloat4(CURVE_COLORS[i % CURVE_COLORS.size()]),
                           ImGuiColorEditFlags_NoTooltip,
                           ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - CURVE_ROW_TRAILER, MIN_CURVE_NAME_WIDTH));
        if(input_text_string("##name", curve.name))
        {
            result.changed = true;
        }
        if(ImGui::IsItemDeactivatedAfterEdit())
        {
            result.edit_finished = true;
        }
        const char* name_problem = get_curve_name_problem(importer.curves, i);
        ImGui::SameLine();
        ImGui::TextDisabled("%.3f", curve.curve.sample(playhead_));
        ImGui::SameLine();
        if(ImGui::Button(ICON_MDI_DELETE))
        {
            importer.curves.erase(importer.curves.begin() + std::ptrdiff_t(i));
            result.changed = true;
            result.edit_finished = true;
            ImGui::PopID();
            break;
        }
        if(name_problem != nullptr)
        {
            ImGui::TextColored(COL_NAME_PROBLEM, ICON_MDI_ALERT " %s", name_problem);
        }
        result |= ::unravel::inspect(ctx, curve.curve);
        ImGui::PopID();
    }
    return result;
}

} // namespace unravel
