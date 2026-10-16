#pragma once

#include "animation_controller_ui.h"

#include <editor/imgui/integration/imgui.h>
#include <engine/animation/animation_controller.h>

#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief What the controller editor has selected in a layer's graph.
 */
struct animation_graph_selection
{
    enum class kind : uint8_t
    {
        none,
        state,
        transition,
        entry,
        any_state,
        layer,
        parameter,
    };

    kind what{kind::none};
    /// State, transition or parameter index, depending on `what`.
    int index{-1};

    auto is(kind k, int i) const -> bool
    {
        return what == k && index == i;
    }

    auto operator==(const animation_graph_selection& other) const -> bool = default;
};

/**
 * @brief Runtime of the layer being shown, for live highlighting.
 */
struct animation_graph_live
{
    bool valid{};
    int current_state{-1};
    int next_state{-1};
    int transition{-1};
    float transition_progress{};
    float state_time{};
    float next_state_time{};
};

/**
 * @brief Pan / zoom canvas drawing one layer's state machine: states, Entry, Any State and the
 * transitions between them, with move, link, create, delete and context menus.
 *
 * Edits are made directly on the layer definition; draw() reports them so the owner can snapshot
 * for undo before the first change of an interaction and save once it finishes.
 */
class animation_graph_view
{
public:
    struct result : animation_edit_result
    {
        /// A clip dropped on the canvas: the owner creates a state for it at drop_position.
        std::string dropped_clip_key;
        math::vec2 drop_position{};
        /// A state was double-clicked: the owner starts renaming the selection.
        bool rename_requested{};
    };

    /**
     * @brief Draws the canvas filling the available region.
     */
    auto draw(animation_controller& controller,
              size_t layer_index,
              const animation_graph_live& live,
              animation_graph_selection& selection) -> result;

    /// Fits every node in view on the next draw; a layer whose states were never placed is laid out.
    void request_frame_all();

    /// Ends a link or node drag in progress: the definition changed under it (undo, a reload, a
    /// state or layer removed).
    void cancel_interaction();

private:
    enum class node_kind : uint8_t
    {
        state,
        entry,
        any_state,
    };

    struct node_ref
    {
        node_kind kind{node_kind::state};
        int index{-1};

        auto operator==(const node_ref& other) const -> bool = default;
    };

    struct node_rect
    {
        ImVec2 min;
        ImVec2 max;
        auto center() const -> ImVec2
        {
            return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        }
    };

    auto to_screen(const math::vec2& graph) const -> ImVec2;
    auto to_graph(const ImVec2& screen) const -> math::vec2;
    auto get_node_rect(const animation_layer_def& layer, const node_ref& node) const -> node_rect;
    auto get_node_position(const animation_layer_def& layer, const node_ref& node) const -> math::vec2;
    void set_node_position(animation_layer_def& layer, const node_ref& node, const math::vec2& position) const;
    auto is_valid_node(const animation_layer_def& layer, const node_ref& node) const -> bool;
    auto hit_node(const animation_layer_def& layer, const ImVec2& mouse) const -> node_ref;
    auto hit_transition(const animation_layer_def& layer, const ImVec2& mouse) const -> int;
    void frame_all(const animation_layer_def& layer);
    /// The state drawn last and hit first: the selected one, so a dragged node stays on top.
    auto get_top_state(const animation_graph_selection& selection) const -> int;

    void draw_grid(ImDrawList* draw_list) const;
    void draw_transitions(ImDrawList* draw_list,
                          const animation_layer_def& layer,
                          const animation_graph_live& live,
                          const animation_graph_selection& selection) const;
    void draw_nodes(ImDrawList* draw_list,
                    const animation_layer_def& layer,
                    const animation_graph_live& live,
                    const animation_graph_selection& selection) const;
    void draw_node(ImDrawList* draw_list,
                   const node_rect& rect,
                   const char* label,
                   const char* sublabel,
                   ImU32 accent,
                   bool selected,
                   bool hovered,
                   float progress,
                   ImU32 progress_color) const;
    void draw_arrow(ImDrawList* draw_list,
                    const ImVec2& from,
                    const ImVec2& to,
                    ImU32 color,
                    float thickness,
                    int heads) const;
    void draw_tooltip(const animation_controller& controller, const animation_layer_def& layer) const;

    void handle_input(animation_controller& controller,
                      size_t layer_index,
                      animation_graph_selection& selection,
                      result& out);
    void handle_zoom_and_pan();
    void handle_click(animation_layer_def& layer, size_t layer_index, animation_graph_selection& selection, result& out);
    void link_to(animation_layer_def& layer, const node_ref& node, animation_graph_selection& selection, result& out);
    void handle_node_drag(animation_layer_def& layer, result& out);
    void handle_context_click(animation_layer_def& layer, animation_graph_selection& selection);
    void handle_keys(animation_layer_def& layer, animation_graph_selection& selection, result& out);
    /// Removes a state with its transitions, ending the link or drag in progress.
    void remove_state(animation_layer_def& layer, size_t index);
    void draw_context_menus(animation_controller& controller,
                            size_t layer_index,
                            animation_graph_selection& selection,
                            result& out);
    void handle_drop(result& out);

    ImVec2 origin_{};
    ImVec2 size_{};
    math::vec2 pan_{300.0f, 160.0f};
    float zoom_{1.0f};
    bool frame_request_{true};
    size_t framed_layer_{~size_t(0)};

    node_ref dragging_{node_kind::state, -1};
    bool dragged_{};
    math::vec2 drag_grab_{};
    bool linking_{};
    node_ref link_source_{};
    math::vec2 context_position_{};
    node_ref context_node_{node_kind::state, -1};
    /// The state drawn on top this frame, or -1.
    int top_state_{-1};
};

} // namespace unravel
