#pragma once
#include <editor/editing/animation_preview.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector.h>
#include <editor/imgui/integration/imgui.h>

#include "animation_blend_space_view.h"
#include "animation_graph_view.h"

#include <engine/animation/animation_controller.h>
#include <engine/assets/asset_handle.h>

#include <entt/entt.hpp>
#include <math/math.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace unravel
{
class imgui_panels;
class animation_component;
class animation_controller_instance;

/**
 * @brief The Animation Controller editor: a graph of every layer's states and transitions, the
 * controller's parameters and layers, an inspector for the selection, live debugging of the
 * entity using the controller and an edit-mode preview.
 *
 * The selection is inspected with the inspector itself (inspect(), with the controller types'
 * inspectors in animation_controller_inspectors.h), so it reads like any other inspector. Every
 * finished edit saves the controller and can be undone with Ctrl+Z while the window has focus;
 * the scene's own undo, redo and save shortcuts stay out of it meanwhile.
 */
class animation_panel
{
public:
    animation_panel(imgui_panels* parent);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s);

    /**
     * @brief Shows the window editing `controller`.
     */
    void open(const asset_handle<animation_controller>& controller);

    auto get_controller() const -> const asset_handle<animation_controller>&;

    /// The window has keyboard focus: it takes Ctrl+Z / Y / S for the controller.
    auto is_focused() const -> bool;

private:
    /// The skeleton a layer mask lists bones from, cached per mesh.
    struct mask_skeleton
    {
        std::weak_ptr<const mesh> source;
        std::vector<std::string> names;
        std::vector<int> parents;
        std::vector<int> depths;
    };

    auto resolve_controller() -> std::shared_ptr<animation_controller>;
    /// Shows a freshly loaded object of the controller: a reload of one of the window's own saves
    /// takes the definition on screen, anything else replaces it.
    void adopt_loaded(const std::shared_ptr<animation_controller>& loaded);
    void draw_ui(rtti::context& ctx);
    void handle_shortcuts(animation_controller& controller);
    void draw_layout(rtti::context& ctx, animation_controller& controller);
    void draw_layer_tabs(const animation_controller& controller);
    /// Returns the selection a click in the graph made, for the inspector to show once it drew.
    auto draw_graph(rtti::context& ctx, animation_controller& controller) -> std::optional<animation_graph_selection>;
    void add_dropped_clip(rtti::context& ctx,
                          animation_controller& controller,
                          const std::string& key,
                          const math::vec2& position);
    void draw_toolbar(rtti::context& ctx, animation_controller& controller);
    void draw_debug_target(rtti::context& ctx);
    void draw_preview_button(rtti::context& ctx);
    void draw_validation(const animation_controller& controller);
    void draw_sidebar(rtti::context& ctx, animation_controller& controller);
    void draw_layers(animation_controller& controller);
    void draw_parameters(animation_controller& controller);
    void draw_live_value(animation_parameter_def& parameter, animation_controller_instance& instance);
    void draw_parameter_default(animation_controller& controller, animation_parameter_def& parameter);
    void draw_add_parameter(animation_controller& controller);
    void remove_parameter(animation_controller& controller, size_t index);
    void draw_inspector(rtti::context& ctx, animation_controller& controller);
    void draw_selection_inspector(rtti::context& ctx, animation_controller& controller);
    void draw_inspector_title(const char* title);
    /// Inspects a definition of the controller with the inspector, as one undo step of the window.
    template<typename Definition>
    void inspect_definition(rtti::context& ctx, animation_controller& controller, Definition& definition);
    void draw_parameter_inspector(rtti::context& ctx, animation_controller& controller, size_t index);
    void draw_state_inspector(rtti::context& ctx, animation_controller& controller, animation_layer_def& layer, size_t index);
    auto draw_blend_plot(animation_controller& controller, animation_state_def& state) -> inspect_result;
    void draw_outgoing_transitions(animation_controller& controller, animation_layer_def& layer, const animation_state_def& state);
    void draw_transition_inspector(rtti::context& ctx,
                                   animation_controller& controller,
                                   animation_layer_def& layer,
                                   size_t index);
    void draw_layer_inspector(rtti::context& ctx, animation_controller& controller, size_t index);
    auto draw_mask_bones(animation_controller& controller, animation_layer_def& layer) -> inspect_result;
    auto draw_mask_tree(animation_layer_def& layer, const mask_skeleton& skeleton) -> bool;
    void draw_empty_state(rtti::context& ctx);

    void resolve_debug_target(rtti::context& ctx);
    auto get_debug_component() const -> animation_component*;
    auto get_live(size_t layer) const -> animation_graph_live;
    auto get_live_blend_weights(size_t layer, size_t state, const animation_state_def& def) const -> std::vector<float>;
    void set_live_parameter(const std::string& name, float value);

    void begin_edit(animation_controller& controller);
    /// `runtime_change` false for editor layout only (node positions): running controllers keep going.
    void apply_edit(animation_controller& controller, bool finished, bool runtime_change = true);
    void apply_result(animation_controller& controller, const animation_edit_result& result);
    void commit_edit(animation_controller& controller);
    void track_item_edit(animation_controller& controller);
    /// Finishes the open edit once the item that began it is no longer in use: an item that stops
    /// being drawn (the selection changed under it) never reports the end of its edit.
    void finish_abandoned_edit(animation_controller& controller);
    /// Finishes the open edit and saves what is unsaved: the window stops showing the controller.
    void flush_changes();
    void undo(animation_controller& controller);
    void redo(animation_controller& controller);
    void step_history(animation_controller& controller, std::vector<std::string>& from, std::vector<std::string>& to);
    /// Clears the selection with the selected blend point and a drag in the blend plot.
    void reset_selection();
    void save();

    void start_preview(rtti::context& ctx);
    void stop_preview();

    auto get_problems(const animation_controller& controller) -> const std::vector<std::string>&;
    auto get_mask_skeleton(const asset_handle<mesh>& mesh_asset) -> const mask_skeleton*;

    imgui_panels* parent_{};
    bool show_request_{};
    bool show_{false};
    /// The window drew its content last frame: open, and not a docked tab behind another.
    bool drawn_{};
    bool focused_{};

    asset_handle<animation_controller> controller_;
    /// The definition on screen. A save reloads the asset as a new object, which takes this
    /// definition over when it arrives; until then this one stays on screen, read-only.
    std::shared_ptr<animation_controller> shown_;
    bool reloading_{};
    size_t layer_{};
    /// The layer whose tab the tab bar showed last frame: differs from layer_ while a switch is on.
    size_t shown_tab_{~size_t(0)};
    animation_graph_selection selection_;
    animation_graph_view graph_;
    animation_blend_space_view blend_view_;
    int selected_point_{-1};
    /// The state (layer, index) selected_point_ belongs to.
    std::pair<size_t, size_t> point_owner_{~size_t(0), ~size_t(0)};
    float sidebar_width_{240.0f};
    float inspector_width_{520.0f};

    std::vector<std::string> undo_stack_;
    std::vector<std::string> redo_stack_;
    /// The definition as of the last finished edit: what the next undo step goes back to.
    std::string committed_snapshot_;
    bool edit_open_{};
    /// The item active when the open edit began (0 for none).
    ImGuiID edit_item_{};
    bool dirty_{};
    /// The text of each of the window's own saves whose reload has not come back yet, oldest first.
    std::vector<std::string> own_saves_;

    /// Validation of the shown definition at validated_revision_.
    std::vector<std::string> problems_;
    uint64_t validated_revision_{~uint64_t(0)};
    mask_skeleton mask_skeleton_;

    entt::handle debug_entity_;
    bool follow_selection_{true};

    animation_entity_preview preview_;

    ImGuiTextFilter mask_filter_;
};
} // namespace unravel
