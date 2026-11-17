#pragma once

#include "material_graph_previews.h"
#include "material_graph_scene_preview.h"
#include "material_graph_view.h"

#include <context/context.hpp>
#include <editor/imgui/integration/imgui.h>
#include <engine/assets/asset_handle.h>
#include <engine/frame_times.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>

#include <hpp/optional.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace unravel
{
class imgui_panels;
class material;

/**
 * @brief The Material Graph editor: a palette of node types, the graph's canvas, the selection's details and the
 * G-buffer shader the graph generates, kept up to date with every edit.
 *
 * A graph is a graph material (.mat, graph_material): the window edits the loaded asset, so the scene shows every
 * value at once, and saves each finished edit to its file, whose compile writes the shader the scene draws it with.
 * Graph files (.mgraph JSON, engine/meta/rendering/material_graph.hpp), the shipped examples among them, open as
 * untitled graphs that Save makes a material of. Ctrl+Z undoes while the window has focus; the scene's own undo, redo
 * and save shortcuts stay out of it meanwhile.
 *
 * Every node shows a swatch of its value and the side pane a 3D preview of the material, both drawn with shaders the
 * window compiles itself (material_graph_previews), so an untitled graph previews as well as a saved one.
 */
class material_graph_panel
{
public:
    material_graph_panel(imgui_panels* parent);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    /// Draws the previews of a window drawn the frame before: the node swatches and the 3D preview.
    void on_frame_render(rtti::context& ctx, const frame_times& times);
    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s);

    /// Opens graph material @p asset and shows the window. False, opening nothing, for a material that is not a graph,
    /// or when the user keeps the untitled graph the window holds.
    auto open(const asset_handle<material>& asset) -> bool;

    /// The window holds an untitled graph with changes: opening another asks the user before it discards them.
    auto has_unsaved_untitled_graph() const -> bool;

    /// Opens the graph file (.mgraph) or graph material (.mat) at @p absolute_path and shows the window: a graph file of
    /// the project, such as a material function, is edited in place; one outside it opens untitled.
    auto open_path(const std::string& absolute_path) -> bool;

    /// The window has keyboard focus: it takes Ctrl+Z / Y / S for the graph.
    auto is_focused() const -> bool;

private:
    /// Lets go of the project's assets: the window shows a new untitled graph.
    void on_project_closed(rtti::context& ctx);

    /// A graph file the New menu offers.
    struct example
    {
        std::string name;
        std::string path;
    };

    // Documents.
    /// Shows @p graph, named @p name, saved to @p path (empty: untitled). The undo history starts over.
    void set_graph(material_graph graph, const std::string& path, const std::string& name);
    /// Shows @p graph as an untitled graph named @p name.
    void set_untitled_graph(material_graph graph, const std::string& name);
    void new_graph();
    void open_dialog();
    /// Opens a graph material (.mat) or, untitled, a graph file (.mgraph).
    void open_file(const std::string& absolute_path);
    /// Opens the graph file at @p absolute_path as an untitled graph named @p name.
    void open_graph_file(const std::string& absolute_path, const std::string& name);
    void save();
    void save_as();
    /// Gives the graph on screen to the loaded material, so the scene draws it, while its generated code is the code
    /// the material's shader was compiled from (live_code_): a value edit. A code edit reaches the scene with the
    /// recompile its save starts, graph and shader together, never the new graph's parameters over the old shader.
    void push_values_to_asset();
    /// What the window's compiles name the graph by: its asset key, else its name.
    auto get_source_name() const -> std::string;
    /// The code a compile generates for @p graph (both stages), as compile_if_needed names it.
    auto make_generated_code(const material_graph& graph) const -> std::string;
    /// Follows a reload of the material: one of the window's own saves coming back keeps the graph on screen, a change
    /// made elsewhere replaces it.
    void sync_with_asset();
    /// True when nothing unsaved is lost, or the user agrees to lose it.
    auto confirm_discard() -> bool;
    /// Opens the function a Material Function node calls, keeping the graph it was opened from to go back to.
    void open_called_function(const std::string& absolute_path);
    /// Goes back to the graph at @p index of back_trail_, forgetting the ones after it.
    void go_back(size_t index);
    void find_examples();

    // Editing, undo, compiling.
    void begin_edit();
    /// @p code_change false for positions only: the shader stays.
    void apply_edit(bool finished, bool code_change = true);
    void apply_result(const material_graph_edit_result& result);
    void commit_edit();
    /// Follows the edit of the last item drawn: an edit begins with its first change and ends when it is let go.
    void track_item_edit();
    /// Follows an edit as an inspector widget reports it: one that edits in a popup of its own (a color swatch) is not
    /// the last item drawn.
    void track_value_edit(bool is_changed, bool is_finished);
    /// Finishes the open edit once the item that began it is no longer in use.
    void finish_abandoned_edit();
    void undo();
    void redo();
    void step_history(std::vector<std::string>& from, std::vector<std::string>& to);
    void compile_if_needed();
    void lay_out();

    // Drawing.
    void draw_ui(rtti::context& ctx);
    void handle_shortcuts();
    void draw_toolbar();
    /// A toolbar toggle showing or hiding a part of the window.
    void draw_pane_toggle(const char* icon, bool& shown, const char* tooltip);
    /// The way back to the graphs the window came from: a Back button and the trail of their names.
    void draw_back_trail();
    void draw_new_menu();
    void draw_domain_combo();
    /// A Post Process material's button that runs it over the view (add_to_scene).
    void draw_add_to_scene_button();
    /// Runs the material on the selected volume or camera, or on the scene's global volume.
    void add_to_scene();
    void draw_status();
    void draw_layout(rtti::context& ctx);
    /// The edge of a side pane a handle moves: its width, kept below max_width, whether it shows, and which way it
    /// grows with the mouse (1 for a pane on the left, -1 for one on the right).
    struct pane_edge
    {
        float& width;
        bool& is_shown;
        float direction{};
        float max_width{};
    };
    /// The handle between a side pane and the canvas, at the cursor: dragging it resizes the pane, a double-click
    /// collapses it.
    void draw_pane_handle(const char* id, const pane_edge& pane, float height, const char* pane_name);
    /// A collapsed side pane's tab, at the cursor: its icon and its name written down the tab. A click opens the pane.
    void draw_pane_tab(const char* id, const char* icon, const char* name, bool& is_shown, float height);
    void draw_palette();
    void draw_palette_entry(const material_node_def& def);
    void draw_side(rtti::context& ctx);
    /// The 3D preview at the top of the side pane, with its shape picker and compile state.
    void draw_scene_preview();
    /// A line telling a preview compile in progress or failed; nothing when the previews are current.
    void draw_preview_state();
    void draw_details(rtti::context& ctx);
    void draw_node_details(rtti::context& ctx, size_t index);
    void draw_node_title(size_t index);
    void draw_setting(material_graph_node& node, const material_setting_def& setting);
    /// A Custom node's code: a multi-line editor under its label.
    void draw_code_setting(material_graph_node& node, const material_setting_def& setting, const std::string& value);
    /// A Named Reroute Usage's name: a pick of the graph's Named Reroutes.
    void draw_reroute_name_setting(material_graph_node& node, const material_setting_def& setting, const std::string& value);
    /// A Material Function's function: a pick of the project's functions, and a button opening it.
    void draw_function_setting(material_graph_node& node, const material_setting_def& setting, const std::string& value);
    /// The window edits a graph file of the project (a material function), saved in place.
    auto is_graph_file_document() const -> bool;
    /// The document has a file, which keeps every finished edit, undo and redo included: a graph material or a graph
    /// file of the project. An untitled graph waits for Save.
    auto is_saved_on_edit() const -> bool;
    /// The graph material's source is gone (deleted in the Content Browser): the window keeps the graph as an untitled
    /// one, unsaved, rather than writing the material back. True when it did.
    auto detach_deleted_material() -> bool;
    void save_graph_file();
    /// Saves an untitled function as a graph file of the project.
    void save_function_as();
    /// A texture node's texture, edited with the inspector's asset field and picker.
    void draw_texture_field(rtti::context& ctx, size_t index);
    /// A Color Ramp's stops: the ramp as a bar, a row per stop, and buttons that add and remove stops.
    void draw_color_ramp(size_t index);
    /// Gives a texture dropped on the canvas to the node it landed on, or to a new Texture Sample.
    void apply_texture_drop(rtti::context& ctx, const material_graph_view::result& result);
    void draw_inputs(size_t index);
    void draw_input_value(size_t index, size_t pin);
    /// The components of a split input: each one's link, or its value.
    void draw_input_components(size_t index, size_t pin);
    void draw_outputs(size_t index);
    /// A button splitting a vector pin into components, or joining them again.
    void draw_split_toggle(size_t index, const material_pin_def& pin, bool is_output);
    void draw_diagnostics(const std::vector<const material_graph_diagnostic*>& diagnostics);
    void draw_comment_details();
    void draw_graph_summary();
    void draw_problems();
    void draw_code();
    /// The lines of the shader the selected nodes wrote, first and count; first 0 for none.
    auto get_selected_lines() const -> std::pair<uint32_t, uint32_t>;
    void select_node(const std::string& id);

    imgui_panels* parent_{};
    rtti::context* ctx_{};
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    bool show_request_{};
    bool show_{false};
    bool drawn_{};
    bool focused_{};

    material_graph graph_;
    /// The graph material being edited; empty while untitled.
    asset_handle<material> material_;
    /// The loaded object of material_ the graph on screen was last given to or taken from.
    std::weak_ptr<material> shown_material_;
    /// The window's saves not yet back from the material's reload, oldest first.
    std::vector<std::string> own_saves_;
    /// The generated code of the graph the loaded material holds: what its shader was compiled from.
    std::string live_code_;
    /// Absolute path of the material; empty while untitled.
    std::string path_;
    std::string name_;
    material_graph_selection selection_;
    material_graph_view view_;
    material_graph_compile_result compiled_;
    material_graph_previews previews_;
    material_graph_scene_preview scene_preview_;
    /// Bumped by every change of the graph's code; the compile runs when it moved.
    uint64_t revision_{};
    uint64_t compiled_revision_{~uint64_t(0)};
    /// Bumped by every change at all, layout too: the JSON view follows it.
    uint64_t text_revision_{};
    uint64_t json_revision_{~uint64_t(0)};
    std::string json_;

    std::vector<std::string> undo_stack_;
    std::vector<std::string> redo_stack_;
    /// The graph as of the last finished edit: what the next undo step goes back to.
    std::string committed_snapshot_;
    bool edit_open_{};
    ImGuiID edit_item_{};
    bool dirty_{};

    /// A graph to open once this frame's drawing is done (a Material Function's function).
    std::string pending_open_;
    /// A graph the window came from, to go back to: the file of a graph material or a material function.
    struct document_ref
    {
        std::string path;
        std::string name;
    };
    /// The graphs the window came from by opening the functions they call, the first one first.
    std::vector<document_ref> back_trail_;
    /// The index in back_trail_ to go back to once this frame's drawing is done.
    hpp::optional<size_t> pending_back_;
    std::vector<example> examples_;
    std::array<char, 128> palette_filter_{};
    /// The widths the side panes open at, until their handles are dragged.
    static constexpr float DEFAULT_PALETTE_WIDTH = 220.0f;
    static constexpr float DEFAULT_SIDE_WIDTH = 460.0f;
    float palette_width_{DEFAULT_PALETTE_WIDTH};
    float side_width_{DEFAULT_SIDE_WIDTH};
    bool show_palette_{true};
    bool show_side_{true};
    bool show_json_{};
    bool show_swatches_{true};
    bool show_scene_preview_{true};
    /// The selection the code view last scrolled to.
    material_graph_selection code_scrolled_selection_;
    /// The node whose id field is being typed, and its text.
    std::string renaming_node_;
    std::array<char, 128> rename_text_{};
};

} // namespace unravel
