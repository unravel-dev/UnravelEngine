#pragma once

#include "material_graph_layout.h"
#include "material_graph_previews.h"

#include <editor/imgui/integration/imgui.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>
#include <imgui/imgui_internal.h>
#include <math/math.h>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief What an editing widget of the material graph editor did this frame, so its owner can take an undo snapshot
 * at the first change of an interaction and save once it finishes.
 */
struct material_graph_edit_result
{
    /// The graph is about to change (first change of an interaction).
    bool begin_edit{};
    /// The graph changed in a way its code sees.
    bool changed{};
    /// Only positions changed (nodes, comments): saved, but the code stays.
    bool layout_changed{};
    /// The interaction finished; save.
    bool finished{};

    /// Reports a one-step edit: begun, changed and finished at once.
    void set_committed();
    /// Takes in what @p other reported too.
    void merge(const material_graph_edit_result& other);
};

/// How a pin of a node shows: whether it does, and its label. A material function call's pins are its function's
/// inputs and outputs, named as the function names them; its other pins and every hidden pin do not show.
struct material_pin_presentation
{
    bool is_shown{};
    std::string label;
};

auto get_material_pin_presentation(const material_graph_node& node, const material_node_def& def, bool is_output, size_t pin)
    -> material_pin_presentation;

/// The material function a call names, as its pins show it; empty when it names none that loads.
auto find_material_call_interface(const material_graph_node& node) -> hpp::optional<material_function_interface>;

/**
 * @brief What is selected on the canvas.
 */
struct material_graph_selection
{
    /// Ids of the selected nodes, the last one clicked last.
    std::vector<std::string> nodes;
    /// Index of the selected comment, -1 for none.
    int comment{-1};
    /// The input entry whose link is selected, "node.pin" (or "node.pin.c"); empty for none.
    std::string link;

    auto operator==(const material_graph_selection& other) const -> bool = default;

    auto has_node(hpp::string_view id) const -> bool;
    auto is_empty() const -> bool;
    void clear();
};

/**
 * @brief Pan / zoom canvas of a material graph: nodes with their pins, values and texture previews, the links
 * between them, comments, and every way to edit them.
 *
 * - Links: drag from a pin to a pin. Dropping on a node links its first fitting pin; dropping on empty canvas opens
 *   the node search for a node to link. A link that would add a problem (a vector into a float, a loop) is refused,
 *   its preview drawn red with the reason. Dragging from a linked input picks its link up; Alt+click breaks a pin's
 *   links; double-click a link for a reroute.
 * - Components: a vector pin splits into a pin per component (double-click it, or its context menu): an output then
 *   offers each component, an input is built from components linked one by one.
 * - Nodes: right-click, Space or Tab opens the node search; a node type's key held during a click places it (1-4
 *   constants, M multiply, A add, L lerp, T texture...). Box-select by dragging the empty canvas.
 * - Values: drag a number sideways to change it (Shift faster, Ctrl finer), click any value to type it.
 * - Keys: Delete, Ctrl+D, Ctrl+C / X / V (the graph's JSON, so text from anywhere pastes), Ctrl+A, F frames the
 *   selection or everything, C comments the selection.
 *
 * Nodes with a value of their own show a swatch of it under their pins (material_graph_previews), unless the graph's
 * swatches are off or the node's preview is hidden (its context menu); texture nodes show it in place of their
 * texture.
 *
 * Edits are made directly on the graph; draw() reports them so the owner can snapshot for undo before the first
 * change of an interaction and save once it finishes.
 */
class material_graph_view
{
public:
    struct result : material_graph_edit_result
    {
        /// A texture dropped from the Content Browser: the owner gives it to drop_node, or to a new Texture Sample
        /// at drop_position when drop_node is empty.
        std::string dropped_texture_key;
        std::string drop_node;
        math::vec2 drop_position{};
    };

    /**
     * @brief Draws the canvas filling the available region.
     * @param compiled The graph compiled before this frame: resolved types, problems, unused nodes.
     */
    auto draw(material_graph& graph, const material_graph_compile_result& compiled, material_graph_selection& selection) -> result;

    /// Fits every node in view on the next draw; a graph whose nodes were never placed is laid out first.
    void request_frame_all();

    /// Ends a link, drag or box selection in progress: the graph changed under it (undo, open, a reload).
    void cancel_interaction();

    /// How big nodes are and where their pins sit, as the canvas draws them.
    auto get_layout_metrics() const -> material_graph_layout_metrics;

    /// Adds a node of @p type in the middle of the view and selects it; returns its index.
    auto add_node_at_center(material_graph& graph, const std::string& type, material_graph_selection& selection) -> int;

    /// Adds a node of @p type with its header at @p position (graph units) and returns its index.
    auto create_node(material_graph& graph, const std::string& type, const math::vec2& position) -> int;

    /// The swatches the nodes show; null for none. @p are_shown false hides every one, and the room they take.
    void set_previews(const material_graph_previews* previews, bool are_shown);

private:
    enum class pin_side : uint8_t
    {
        input,
        output,
    };

    /// A pin of a node: its index in the node type's inputs or outputs, and the component of a split pin (-1: the
    /// whole pin).
    struct pin_ref
    {
        int node{-1};
        pin_side side{pin_side::input};
        int pin{-1};
        int component{-1};

        auto operator==(const pin_ref& other) const -> bool = default;
        auto is_valid() const -> bool
        {
            return node >= 0 && pin >= 0;
        }
    };

    /// One row of a node: a pin (or a component of a split one) on its side.
    struct pin_row
    {
        pin_side side{pin_side::input};
        int pin{-1};
        int component{-1};
        std::string label;
    };

    /// Where a pin of a node is on screen this frame.
    struct pin_slot
    {
        pin_ref ref;
        ImVec2 center{};
        /// Inputs: the value field, shown while the input is unlinked.
        ImRect chip;
        std::string label;
        bool is_linked{};
        /// Value-only inputs show a field and no pin.
        bool has_pin{true};
        /// A component row of a split output, drawn indented.
        bool is_component{};
    };

    /// Where a node and its parts are on screen this frame.
    struct node_geometry
    {
        ImRect rect;
        ImRect header;
        const material_node_def* def{};
        std::vector<pin_slot> slots;
        /// Texture nodes and nodes with a swatch: where their texture or swatch shows.
        ImRect preview;
        bool has_preview{};
        bool has_swatch{};
        bool is_reroute{};
    };

    /// A link on screen this frame, from an output slot to an input slot.
    struct link_geometry
    {
        pin_ref from;
        pin_ref to;
        ImVec2 start{};
        ImVec2 end{};
        material_value_type type{material_value_type::float1};
        /// The components the link picks, shown on it when it starts at the whole output ("rg").
        std::string components;
        /// The input entry it is written on, "node.pin" or "node.pin.c".
        std::string entry;
    };

    enum class interaction : uint8_t
    {
        none,
        moving_nodes,
        box_selecting,
        linking,
        scrubbing,
        moving_comment,
        resizing_comment,
    };

    /// What part of a comment is under the pointer.
    enum class comment_part : uint8_t
    {
        none,
        title,
        corner,
    };

    // Coordinates and measuring.
    auto to_screen(const math::vec2& graph) const -> ImVec2;
    auto to_graph(const ImVec2& screen) const -> math::vec2;
    auto get_base_font_size() const -> float;
    auto get_text_width(const std::string& text) const -> float;
    /// The rows a node shows, inputs and outputs, split pins as a row per component.
    auto make_rows(const material_graph_node& node, const material_node_def& def) const -> std::vector<pin_row>;
    auto measure_node(const material_graph_node& node) const -> math::vec2;
    /// Width of a node's input labels, where its value fields start.
    auto get_input_label_column(const std::vector<pin_row>& rows) const -> float;
    auto get_pin_offset(const material_graph_node& node, const std::string& pin, bool is_output) const -> float;
    auto get_comment_size(const material_graph_comment& comment) const -> math::vec2;
    /// Whether @p node shows a swatch of its value.
    auto has_swatch(const material_graph_node& node, const material_node_def& def) const -> bool;
    /// Whether the node shows the picture of its own texture in place of a swatch: a 2D texture it holds and reads,
    /// nothing linked into its Texture input.
    static auto shows_texture_preview(const material_graph_node& node) -> bool;
    void update_geometry(const material_graph& graph, const material_graph_selection& selection);
    void update_links(const material_graph& graph);
    /// True when the compile result describes the graph drawn: no node was added or removed since.
    auto is_compiled_current(int node) const -> bool;
    auto get_input_type(int node, int pin) const -> material_value_type;
    auto get_output_type(int node, int pin) const -> material_value_type;
    /// The type a pin, or one component of it, gives or takes.
    auto get_pin_type(const pin_ref& pin) const -> material_value_type;
    /// False for a node whose value never reaches the surface output.
    auto is_node_used(int node) const -> bool;
    auto find_slot(const pin_ref& pin) const -> const pin_slot*;
    void frame_rect(const math::vec2& min, const math::vec2& max);
    void frame_all(const material_graph& graph);
    void frame_selection(const material_graph& graph, const material_graph_selection& selection);

    // Names.
    /// The input entry a pin is written on: "base_color", or "base_color.r" for a component.
    auto get_entry_name(const material_graph& graph, const pin_ref& input) const -> std::string;
    /// The output a link reads from a pin: "rgba", or "rgba.r" for a component.
    auto get_output_name(const material_graph& graph, const pin_ref& output) const -> std::string;

    // Hit testing, topmost first.
    auto hit_node(const ImVec2& mouse) const -> int;
    auto hit_pin(const ImVec2& mouse) const -> pin_ref;
    auto hit_chip(const ImVec2& mouse) const -> pin_ref;
    /// The index of the link under the pointer in links_, or -1.
    auto hit_link(const ImVec2& mouse) const -> int;
    auto hit_comment(const material_graph& graph, const ImVec2& mouse, comment_part& part) const -> int;
    /// The pin a link being dragged would connect to under the pointer: a pin, or of a node's pins that take the
    /// link, the one nearest the pointer vertically.
    auto find_link_target(const material_graph& graph, const ImVec2& mouse) -> pin_ref;
    /// Why the link from @p from to @p to would be refused; empty when it would not. Remembered per pair while a
    /// link is dragged: the check compiles the graph.
    auto get_link_error(const material_graph& graph, const pin_ref& from, const pin_ref& to) -> std::string;
    /// The bezier control points of a link from an output at @p from to an input at @p to.
    void get_link_points(const ImVec2& from, const ImVec2& to, ImVec2& control_from, ImVec2& control_to) const;

    // Drawing.
    void draw_grid(ImDrawList* draw_list) const;
    void draw_comments(ImDrawList* draw_list, const material_graph& graph, const material_graph_selection& selection) const;
    void draw_links(ImDrawList* draw_list, const material_graph& graph, const material_graph_selection& selection) const;
    void draw_link_curve(ImDrawList* draw_list, const ImVec2& from, const ImVec2& to, ImU32 color, float thickness) const;
    void draw_nodes(ImDrawList* draw_list, const material_graph& graph, const material_graph_selection& selection) const;
    void draw_node(ImDrawList* draw_list, const material_graph& graph, int index, bool selected) const;
    void draw_node_header(ImDrawList* draw_list, const material_graph& graph, int index, bool is_used) const;
    void draw_node_badge(ImDrawList* draw_list, const material_graph& graph, int index) const;
    void draw_node_slots(ImDrawList* draw_list, const material_graph& graph, int index, ImU32 text_color) const;
    void draw_texture_preview(ImDrawList* draw_list, const material_graph& graph, int index) const;
    void draw_swatch(ImDrawList* draw_list, const material_graph& graph, int index) const;
    /// Draws the swatch of node @p index over @p frame; false while it has none.
    auto draw_swatch_image(ImDrawList* draw_list, const material_graph& graph, int index, const ImRect& frame) const -> bool;
    void draw_reroute(ImDrawList* draw_list, int index, bool selected) const;
    void draw_pin(ImDrawList* draw_list, const ImVec2& center, ImU32 color, bool is_linked, bool is_hovered) const;
    void draw_chip(ImDrawList* draw_list, const material_graph& graph, int node, const pin_slot& slot) const;
    void draw_text(ImDrawList* draw_list, const ImVec2& position, ImU32 color, const char* text, float scale = 1.0f) const;
    void draw_interaction(ImDrawList* draw_list, const material_graph& graph);
    void draw_empty_hint(ImDrawList* draw_list, const material_graph& graph) const;
    void draw_tooltip(const material_graph& graph) const;

    // Input.
    void handle_input(material_graph& graph, material_graph_selection& selection, result& out);
    void handle_zoom_and_pan();
    void handle_press(material_graph& graph, material_graph_selection& selection, result& out);
    void press_pin(material_graph& graph, const pin_ref& pin, result& out);
    void press_chip(material_graph& graph, const pin_ref& chip);
    void press_node(material_graph& graph, int node, material_graph_selection& selection);
    void press_comment(material_graph& graph, int comment, comment_part part, material_graph_selection& selection);
    void press_empty(material_graph& graph, material_graph_selection& selection, result& out);
    void handle_drag(material_graph& graph, result& out);
    void scrub_value(material_graph& graph, result& out);
    void handle_release(material_graph& graph, material_graph_selection& selection, result& out);
    void finish_link(material_graph& graph, result& out);
    void handle_double_click(material_graph& graph, material_graph_selection& selection, result& out);
    void handle_context_click(material_graph& graph, material_graph_selection& selection);
    void handle_keys(material_graph& graph, material_graph_selection& selection, result& out);
    void handle_drop(material_graph& graph, material_graph_selection& selection, result& out);
    /// Places the node type whose key is held during a click on the empty canvas; false when none is held.
    auto place_shortcut_node(material_graph& graph, material_graph_selection& selection, result& out) -> bool;

    // Popups.
    void open_search(const math::vec2& position);
    /// The node types the search lists for its text, best matches first, and only those that can take the pending
    /// link.
    auto get_search_candidates() const -> std::vector<const material_node_def*>;
    void draw_search_popup(material_graph& graph, material_graph_selection& selection, result& out);
    void draw_value_popup(material_graph& graph, result& out);
    void draw_context_menus(material_graph& graph, material_graph_selection& selection, result& out);
    void draw_pin_menu(material_graph& graph, result& out);

    // Edits.
    /// Links output @p from into input @p to (either order); false when the link is refused.
    auto connect(material_graph& graph, const pin_ref& from, const pin_ref& to) -> bool;
    /// Links the new node @p node to the pin the search was opened from.
    void connect_pending(material_graph& graph, int node);
    void break_links(material_graph& graph, const pin_ref& pin);
    /// Shows a vector pin as a pin per component, or as one pin again.
    void toggle_split(material_graph& graph, const pin_ref& pin);
    auto is_split(const material_graph& graph, const pin_ref& pin) const -> bool;
    void insert_reroute(material_graph& graph, const link_geometry& link, const math::vec2& position, material_graph_selection& selection);
    void delete_selection(material_graph& graph, material_graph_selection& selection, result& out);
    void duplicate_selection(material_graph& graph, material_graph_selection& selection, result& out);
    void copy_selection(const material_graph& graph, const material_graph_selection& selection) const;
    void paste(material_graph& graph, material_graph_selection& selection, result& out);
    /// How far pasted nodes move from where the clipboard has them.
    auto get_paste_offset(const material_graph& graph, const material_graph& fragment) const -> math::vec2;
    void comment_selection(material_graph& graph, material_graph_selection& selection, result& out);
    /// Turns a constant into the parameter of its kind and back, keeping its links.
    void convert_node(material_graph& graph, int node, const char* type, result& out);
    void select_nodes_in_box(const material_graph& graph, material_graph_selection& selection) const;

    ImVec2 origin_{};
    ImVec2 size_{};
    math::vec2 pan_{400.0f, 200.0f};
    float zoom_{1.0f};
    bool frame_request_{true};
    std::vector<node_geometry> geometry_;
    std::vector<link_geometry> links_;
    /// Node indices in the order they are drawn: the selected ones last.
    std::vector<int> draw_order_;
    /// The graph changed while input was handled: the geometry no longer matches it until the next frame.
    bool is_geometry_stale_{};
    const material_graph_compile_result* compiled_{};
    const material_graph_previews* previews_{};
    bool are_swatches_shown_{};

    interaction interaction_{interaction::none};
    ImVec2 press_position_{};
    bool has_dragged_{};
    /// The node a press landed on, -1 for none.
    int pressed_node_{-1};
    /// The pin a link is being dragged from.
    pin_ref link_source_{};
    /// The link was picked up from an input: dropped anywhere but on a pin, it is gone.
    bool link_picked_up_{};
    /// Why the target under the pointer refuses the dragged link; empty when it takes it.
    std::string link_error_;
    /// Link checks of the drag in progress, by source and target.
    std::map<std::string, std::string> link_checks_;
    /// Nodes moved by the drag in progress, and where each started.
    std::vector<std::pair<int, math::vec2>> moving_nodes_;
    /// The comment moved or resized and where it started.
    int moving_comment_{-1};
    math::vec2 comment_start_position_{};
    math::vec2 comment_start_size_{};
    /// The value being scrubbed and where it started.
    pin_ref scrubbed_{};
    float scrub_start_value_{};
    bool has_scrub_edit_{};

    /// The node search: where it creates, what it lists, and the pin it links the new node to.
    math::vec2 search_position_{};
    std::array<char, 128> search_text_{};
    int search_highlight_{};
    bool search_focus_{};
    pin_ref search_link_{};
    std::string search_link_node_;
    /// The value edited in the value popup: node, input and component (-1: the whole value).
    std::string value_node_;
    std::string value_pin_;
    int value_component_{-1};
    /// What a context menu was opened on.
    std::string context_node_id_;
    pin_ref context_pin_{};
    int context_link_{-1};
    math::vec2 context_position_{};
};

} // namespace unravel
