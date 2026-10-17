#include "animation_panel.h"
#include "animation_controller_inspectors.h"
#include "../panels_defs.h"

#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/shortcuts.h>
#include <engine/animation/animation_controller_instance.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/meta/animation/animation_controller.hpp>
#include <engine/play_mode.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/mesh.h>

#include <filesystem/filesystem.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/splitter.h>
#include <imgui_widgets/tooltips.h>
#include <logging/logging.h>

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace
{
/// Undo snapshots kept per open controller.
constexpr size_t MAX_UNDO_STEPS = 100;
/// Own saves remembered until their reload comes back; a save whose reload never comes ages out.
constexpr size_t MAX_OWN_SAVES = 16;
constexpr float MIN_SIDE_WIDTH = 160.0f;
/// The columns next to a dragged splitter keep at least this much.
constexpr float MIN_SPLIT_REST = 200.0f;
constexpr float MIN_GRAPH_WIDTH = 100.0f;
constexpr float SPLITTER_THICKNESS = 4.0f;
constexpr float DEBUG_TARGET_WIDTH = 180.0f;
constexpr float MASK_TREE_HEIGHT = 260.0f;
constexpr float MASK_INDENT = 10.0f;
/// Half the width of the empty-window message, to centre it.
constexpr float EMPTY_STATE_HALF_WIDTH = 200.0f;
/// Room kept right of the validation status in the toolbar.
constexpr float STATUS_MARGIN = 30.0f;

const ImVec4 COL_WARNING(1.0f, 0.72f, 0.3f, 1.0f);
const ImVec4 COL_OK(0.45f, 0.85f, 0.5f, 1.0f);
const ImVec4 COL_LIVE(0.35f, 0.78f, 1.0f, 1.0f);
/// The graph's default-state accent and selection colours, as text colours.
const ImVec4 COL_DEFAULT_STATE(0.87f, 0.52f, 0.2f, 1.0f);
const ImVec4 COL_SELECTED_POINT(0.4f, 0.7f, 1.0f, 1.0f);
const ImVec4 COL_BONE_MASKED(0.85f, 0.88f, 0.92f, 1.0f);
const ImVec4 COL_BONE_UNMASKED(0.5f, 0.52f, 0.56f, 1.0f);

constexpr animation_parameter_type PARAMETER_TYPES[] = {
    animation_parameter_type::float_value,
    animation_parameter_type::int_value,
    animation_parameter_type::bool_value,
    animation_parameter_type::trigger,
};

void collect_mesh_skeleton(const mesh::armature_node& node,
                           int parent,
                           std::vector<std::string>& names,
                           std::vector<int>& parents)
{
    const int index = int(names.size());
    names.push_back(node.name);
    parents.push_back(parent);
    for(const auto& child : node.children)
    {
        collect_mesh_skeleton(*child, index, names, parents);
    }
}

/// How many conditions, blend axes and speed parameters name the parameter.
auto count_parameter_uses(const animation_controller& controller, const std::string& name) -> size_t
{
    size_t uses = 0;
    for(const auto& layer : controller.layers)
    {
        for(const auto& state : layer.states)
        {
            uses += size_t(state.parameter_x == name) + size_t(state.parameter_y == name) +
                    size_t(state.speed_parameter == name);
        }
        for(const auto& transition : layer.transitions)
        {
            uses += size_t(std::count_if(transition.conditions.begin(),
                                         transition.conditions.end(),
                                         [&](const animation_condition_def& condition)
                                         {
                                             return condition.parameter == name;
                                         }));
        }
    }
    return uses;
}

/// The script call that sets the parameter, for the inspector's hint.
auto get_script_setter(const animation_parameter_def& parameter) -> std::string
{
    const char* value = parameter.type == animation_parameter_type::trigger ? "" : ", value";
    return std::string("AnimationComponent.Set") + get_parameter_type_name(parameter.type) + "(\"" + parameter.name +
           "\"" + value + ")";
}
} // namespace

animation_panel::animation_panel(imgui_panels* parent) : parent_(parent)
{
}

void animation_panel::init(rtti::context& ctx)
{
}

void animation_panel::deinit(rtti::context& ctx)
{
    flush_changes();
    stop_preview();
}

void animation_panel::show(bool s)
{
    show_request_ = s;
}

void animation_panel::open(const asset_handle<animation_controller>& controller)
{
    if(controller_ != controller)
    {
        flush_changes();
        stop_preview();
        controller_ = controller;
        shown_ = nullptr;
        layer_ = 0;
        reset_selection();
        undo_stack_.clear();
        redo_stack_.clear();
        committed_snapshot_.clear();
        edit_open_ = false;
        dirty_ = false;
        own_saves_.clear();
        graph_.cancel_interaction();
        graph_.request_frame_all();
    }
    show(true);
}

auto animation_panel::get_controller() const -> const asset_handle<animation_controller>&
{
    return controller_;
}

auto animation_panel::is_focused() const -> bool
{
    return focused_;
}

void animation_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    focused_ = false;
    if(show_request_)
    {
        show_request_ = false;
        show_ = true;
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.6f, ImGuiCond_Once);
        ImGui::SetNextWindowFocus();
    }
    bool is_drawn = false;
    if(show_)
    {
        // Hidden (a docked tab behind another), the window draws nothing and its preview ends.
        if(ImGui::Begin(name, &show_, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            draw_ui(ctx);
            is_drawn = true;
        }
        ImGui::End();
    }
    else
    {
        stop_preview();
    }
    // Closed or hidden: no field of it reports the end of an edit any more.
    if(drawn_ && !is_drawn)
    {
        flush_changes();
    }
    drawn_ = is_drawn;
}

// ---------------------------------------------------------------------------------
// Editing, undo, saving
// ---------------------------------------------------------------------------------

void animation_panel::begin_edit(animation_controller& controller)
{
    if(edit_open_)
    {
        return;
    }
    // The snapshot from before the edit: widgets change the definition before they report it.
    undo_stack_.push_back(committed_snapshot_.empty() ? save_to_string(controller) : committed_snapshot_);
    if(undo_stack_.size() > MAX_UNDO_STEPS)
    {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
    edit_open_ = true;
    edit_item_ = ImGui::GetActiveID();
}

void animation_panel::apply_edit(animation_controller& controller, bool finished, bool runtime_change)
{
    // Running instances rebuild against the edited definition on their next update.
    if(runtime_change)
    {
        ++controller.revision;
    }
    dirty_ = true;
    if(finished)
    {
        edit_open_ = false;
        committed_snapshot_ = save_to_string(controller);
        save();
    }
}

void animation_panel::apply_result(animation_controller& controller, const animation_edit_result& result)
{
    if(result.begin_edit)
    {
        begin_edit(controller);
    }
    if(result.changed || result.layout_changed || result.finished)
    {
        apply_edit(controller, result.finished, result.changed);
    }
}

void animation_panel::commit_edit(animation_controller& controller)
{
    begin_edit(controller);
    apply_edit(controller, true);
}

void animation_panel::track_item_edit(animation_controller& controller)
{
    if(ImGui::IsItemEdited())
    {
        begin_edit(controller);
        apply_edit(controller, false);
    }
    // The changes were applied while it was edited; the end only closes the edit.
    if(ImGui::IsItemDeactivatedAfterEdit() && edit_open_)
    {
        apply_edit(controller, true, false);
    }
}

void animation_panel::finish_abandoned_edit(animation_controller& controller)
{
    // The item may lose the mouse or keyboard during a frame and report it in the next one.
    const ImGuiID previous = ImGui::GetCurrentContext()->ActiveIdPreviousFrame;
    const bool is_item_in_use = edit_item_ != 0 && (ImGui::GetActiveID() == edit_item_ || previous == edit_item_);
    if(edit_open_ && !is_item_in_use)
    {
        apply_edit(controller, true, false);
    }
}

void animation_panel::flush_changes()
{
    if(!shown_)
    {
        return;
    }
    if(edit_open_)
    {
        apply_edit(*shown_, true, false);
    }
    else if(dirty_)
    {
        save();
    }
}

void animation_panel::undo(animation_controller& controller)
{
    step_history(controller, undo_stack_, redo_stack_);
}

void animation_panel::redo(animation_controller& controller)
{
    step_history(controller, redo_stack_, undo_stack_);
}

void animation_panel::step_history(animation_controller& controller,
                                   std::vector<std::string>& from,
                                   std::vector<std::string>& to)
{
    if(from.empty())
    {
        return;
    }
    to.push_back(save_to_string(controller));
    animation_controller restored;
    if(load_from_string(from.back(), restored))
    {
        restored.revision = controller.revision + 1;
        controller = restored;
    }
    committed_snapshot_ = from.back();
    from.pop_back();
    edit_open_ = false;
    reset_selection();
    graph_.cancel_interaction();
    layer_ = std::min(layer_, controller.layers.empty() ? size_t(0) : controller.layers.size() - 1);
    dirty_ = true;
    save();
}

void animation_panel::reset_selection()
{
    selection_ = {};
    selected_point_ = -1;
    blend_view_.cancel_interaction();
}

void animation_panel::save()
{
    if(!controller_ || !shown_)
    {
        return;
    }
    // The definition on screen, not the asset's object: while a reload is on its way the asset
    // has only the reloaded copy to give, which a get() would wait for.
    if(!asset_writer::atomic_save_to_file(controller_.id(), shown_))
    {
        APPLOG_ERROR("Could not save the animation controller {}; it stays unsaved.", controller_.id());
        return;
    }
    dirty_ = false;
    own_saves_.push_back(save_to_string(*shown_));
    if(own_saves_.size() > MAX_OWN_SAVES)
    {
        own_saves_.erase(own_saves_.begin());
    }
}

auto animation_panel::get_problems(const animation_controller& controller) -> const std::vector<std::string>&
{
    if(validated_revision_ != controller.revision)
    {
        problems_ = validate_animation_controller(controller);
        validated_revision_ = controller.revision;
    }
    return problems_;
}

// ---------------------------------------------------------------------------------
// Live debugging and preview
// ---------------------------------------------------------------------------------

void animation_panel::resolve_debug_target(rtti::context& ctx)
{
    // A target that is gone, runs another controller (another one is open) or lies outside the
    // scene being edited (a prefab opened since) is dropped; with no target left to pin, the
    // selection is followed again.
    auto* active_scene = ctx.get_cached<editing_manager>().get_active_scene(ctx);
    if(debug_entity_ && (!debug_entity_.valid() || !debug_entity_.all_of<animation_component>() ||
                         debug_entity_.get<animation_component>().get_controller() != controller_ ||
                         active_scene == nullptr || debug_entity_.registry() != active_scene->registry.get()))
    {
        debug_entity_ = {};
        follow_selection_ = true;
    }
    if(!follow_selection_ && debug_entity_)
    {
        return;
    }
    auto& em = ctx.get_cached<editing_manager>();
    for(const auto& selected : em.get_selections())
    {
        auto* entity = selected.try_cast<entt::handle>();
        if(entity == nullptr || !entity->valid())
        {
            continue;
        }
        if(auto* animation = entity->try_get<animation_component>())
        {
            if(animation->get_controller() == controller_)
            {
                debug_entity_ = *entity;
                return;
            }
        }
    }
    if(debug_entity_)
    {
        return;
    }
    // Nothing selected uses the controller: in play mode follow the first entity that does.
    if(ctx.get_cached<play_mode>().is_active() && active_scene != nullptr)
    {
        auto& registry = *active_scene->registry;
        registry.view<animation_component>().each(
            [&](auto e, auto&& animation)
            {
                if(!debug_entity_ && animation.get_controller() == controller_)
                {
                    debug_entity_ = entt::handle(registry, e);
                }
            });
    }
}

auto animation_panel::get_debug_component() const -> animation_component*
{
    if(!debug_entity_ || !debug_entity_.valid())
    {
        return nullptr;
    }
    auto* animation = debug_entity_.try_get<animation_component>();
    if(animation == nullptr || animation->get_controller() != controller_)
    {
        return nullptr;
    }
    return animation;
}

auto animation_panel::get_live(size_t layer) const -> animation_graph_live
{
    animation_graph_live live;
    auto* animation = get_debug_component();
    if(animation == nullptr || !animation->get_player().is_playing())
    {
        return live;
    }
    auto* instance = animation->get_controller_instance();
    if(instance == nullptr || layer >= instance->get_layer_count())
    {
        return live;
    }
    const auto status = instance->get_layer_status(layer, animation->get_player());
    live.valid = true;
    live.current_state = status.current_state;
    live.next_state = status.next_state;
    live.transition = status.transition;
    live.transition_progress = status.transition_progress;
    live.state_time = status.state_time;
    live.next_state_time = status.next_state_time;
    return live;
}

auto animation_panel::get_live_blend_weights(size_t layer, size_t state, const animation_state_def& def) const
    -> std::vector<float>
{
    std::vector<float> weights;
    const auto live = get_live(layer);
    if(!live.valid || (live.current_state != int(state) && live.next_state != int(state)))
    {
        return weights;
    }
    auto* animation = get_debug_component();
    const auto& layers = animation->get_player().get_layers();
    if(layer >= layers.size())
    {
        return weights;
    }
    const auto& player_state = live.next_state == int(state) ? layers[layer].target_state.state
                                                              : layers[layer].current_state.state;
    // The running blend space has the definition's points that have a clip, in order.
    std::vector<size_t> point_of_runtime;
    for(size_t i = 0; i < def.points.size(); ++i)
    {
        if(def.points[i].clip)
        {
            point_of_runtime.push_back(i);
        }
    }
    weights.assign(def.points.size(), 0.0f);
    for(const auto& clip_weight : player_state.blend_clips)
    {
        if(clip_weight.point < point_of_runtime.size())
        {
            weights[point_of_runtime[clip_weight.point]] = clip_weight.weight;
        }
    }
    return weights;
}

void animation_panel::set_live_parameter(const std::string& name, float value)
{
    auto* animation = get_debug_component();
    if(animation == nullptr)
    {
        return;
    }
    if(auto* instance = animation->get_controller_instance())
    {
        const int index = instance->get_parameter_index(name);
        if(index >= 0)
        {
            instance->set_parameter_from_float(size_t(index), value);
        }
    }
}

void animation_panel::start_preview(rtti::context& ctx)
{
    if(get_debug_component() == nullptr || ctx.get_cached<play_mode>().is_active())
    {
        return;
    }
    auto* animation = preview_.start(ctx, debug_entity_);
    if(animation == nullptr)
    {
        return;
    }
    if(auto* instance = animation->get_controller_instance())
    {
        instance->reset();
    }
    animation->get_player().play();
}

void animation_panel::stop_preview()
{
    preview_.stop();
}

// ---------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------

auto animation_panel::resolve_controller() -> std::shared_ptr<animation_controller>
{
    // try_get, not is_ready: it also starts the load of a controller nothing has used yet.
    auto loaded = controller_ ? controller_.try_get() : nullptr;
    reloading_ = false;
    if(loaded && !asset_handle<animation_controller>::is_empty(loaded))
    {
        if(loaded != shown_)
        {
            adopt_loaded(loaded);
        }
        return loaded;
    }
    if(controller_ && !loaded && shown_)
    {
        reloading_ = true;
        return shown_;
    }
    shown_ = nullptr;
    return nullptr;
}

void animation_panel::adopt_loaded(const std::shared_ptr<animation_controller>& loaded)
{
    const std::string text = save_to_string(*loaded);
    const auto echo = std::find(own_saves_.begin(), own_saves_.end(), text);
    if(shown_ && echo != own_saves_.end())
    {
        // One of the window's own saves coming back: the definition on screen, with what changed
        // since, moves into the new object, and the edit and drags in progress go on. The revision
        // bump rebuilds instances that already run the reloaded copy.
        own_saves_.erase(own_saves_.begin(), echo + 1);
        const uint64_t revision = std::max(loaded->revision, shown_->revision) + 1;
        *loaded = *shown_;
        loaded->revision = revision;
        shown_ = loaded;
        return;
    }
    // A first load or a change made elsewhere: what is on file now is the base of the next undo step.
    own_saves_.clear();
    shown_ = loaded;
    committed_snapshot_ = text;
    edit_open_ = false;
    validated_revision_ = ~uint64_t(0);
    graph_.cancel_interaction();
    blend_view_.cancel_interaction();
}

void animation_panel::draw_empty_state(rtti::context& ctx)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + avail.x * 0.5f - EMPTY_STATE_HALF_WIDTH,
                               ImGui::GetCursorPosY() + avail.y * 0.4f));
    ImGui::BeginGroup();
    ImGui::PushFont(ImGui::Font::Bold);
    if(controller_ && controller_.try_get() == nullptr)
    {
        ImGui::Text(ICON_MDI_STATE_MACHINE " Loading %s...", controller_.name().c_str());
    }
    else if(controller_)
    {
        ImGui::Text(ICON_MDI_ALERT " %s could not be loaded", controller_.name().c_str());
    }
    else
    {
        ImGui::TextUnformatted(ICON_MDI_STATE_MACHINE " No animation controller open");
    }
    ImGui::PopFont();
    ImGui::TextDisabled("Double-click an Animation Controller in the Content browser,");
    ImGui::TextDisabled("or create one with Create > Animation Controller.");
    ImGui::EndGroup();
}

void animation_panel::draw_ui(rtti::context& ctx)
{
    if(ctx.get_cached<play_mode>().is_active())
    {
        stop_preview();
    }
    auto controller_ptr = resolve_controller();
    if(!controller_ptr)
    {
        draw_empty_state(ctx);
        return;
    }
    auto& controller = *controller_ptr;
    if(controller.layers.empty())
    {
        // The editor needs a layer to show; the file gets it with the first edit.
        controller.layers.emplace_back().name = "Base";
    }
    layer_ = std::min(layer_, controller.layers.size() - 1);
    resolve_debug_target(ctx);
    if(preview_.is_active())
    {
        if(preview_.get_entity() == debug_entity_)
        {
            preview_.keep_alive();
        }
        else
        {
            stop_preview();
        }
    }
    // While the saved controller reloads it shows read-only for a frame or two, so no edit is lost.
    ImGui::BeginDisabled(reloading_);
    if(!reloading_)
    {
        handle_shortcuts(controller);
    }
    draw_toolbar(ctx, controller);
    ImGui::Separator();
    draw_layout(ctx, controller);
    finish_abandoned_edit(controller);
    ImGui::EndDisabled();
}

void animation_panel::handle_shortcuts(animation_controller& controller)
{
    // Like the header's scene shortcuts, none while an item is in use or a popup is open: both can
    // hold an index into the definition that an undo or a delete would move.
    if(!focused_ || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(ImGuiID(0), ImGuiPopupFlags_AnyPopup))
    {
        return;
    }
    // The header saves the scene as on Ctrl+Shift+S, which Ctrl+S matches too.
    if(!ImGui::IsCombinationKeyPressed(shortcuts::save_scene_as) && ImGui::IsCombinationKeyPressed(shortcuts::save_scene))
    {
        save();
    }
    // Ctrl+Shift+Z matches Ctrl+Z too: redo goes first.
    if(ImGui::IsCombinationKeyPressed(shortcuts::redo, true) || ImGui::IsCombinationKeyPressed(shortcuts::redo_alt, true))
    {
        redo(controller);
    }
    else if(ImGui::IsCombinationKeyPressed(shortcuts::undo, true))
    {
        undo(controller);
    }
    if(ImGui::IsKeyPressed(shortcuts::rename_item, false) && selection_.what != animation_graph_selection::kind::none)
    {
        animation_controller_inspect_scope::request_name_focus();
    }
}

void animation_panel::draw_layout(rtti::context& ctx, animation_controller& controller)
{
    const float total_width = ImGui::GetContentRegionAvail().x;
    const float graph_width =
        std::max(total_width - sidebar_width_ - inspector_width_ - SPLITTER_THICKNESS * 2.0f, MIN_GRAPH_WIDTH);
    const float height = ImGui::GetContentRegionAvail().y;
    // The side panes keep the window padding, like the inspector panel; the graph runs to its edges.
    ImGui::BeginChild("##sidebar", ImVec2(sidebar_width_, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    draw_sidebar(ctx, controller);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    float rest_of_sidebar = graph_width + inspector_width_;
    ImGui::Splitter(true, SPLITTER_THICKNESS, &sidebar_width_, &rest_of_sidebar, MIN_SIDE_WIDTH, MIN_SPLIT_REST, height);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##graph",
                      ImVec2(graph_width, height),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    draw_layer_tabs(controller);
    const auto clicked_selection = draw_graph(ctx, controller);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    float rest_of_inspector = sidebar_width_ + graph_width;
    float inspector = inspector_width_;
    if(ImGui::Splitter(true, SPLITTER_THICKNESS, &rest_of_inspector, &inspector, MIN_SPLIT_REST, MIN_SIDE_WIDTH, height))
    {
        inspector_width_ = inspector;
    }
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##inspector", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    draw_inspector(ctx, controller);
    ImGui::EndChild();
    if(clicked_selection)
    {
        selection_ = *clicked_selection;
    }
}

void animation_panel::draw_layer_tabs(const animation_controller& controller)
{
    if(!ImGui::BeginTabBar("##layer_tabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        return;
    }
    // layer_ changed elsewhere (the Layers list, Add Layer, undo): the tab bar follows it. The
    // switch shows a frame later, so a tab showing meanwhile is not a click.
    const bool is_switching = shown_tab_ != layer_;
    for(size_t i = 0; i < controller.layers.size(); ++i)
    {
        // The id ignores the name: renaming a layer keeps its tab.
        const std::string label = (controller.layers[i].name.empty() ? std::string("Layer") : controller.layers[i].name) +
                                  "###layer_tab" + std::to_string(i);
        const ImGuiTabItemFlags flags = is_switching && i == layer_ ? ImGuiTabItemFlags_SetSelected : 0;
        if(ImGui::BeginTabItem(label.c_str(), nullptr, flags))
        {
            if(!is_switching && i != layer_)
            {
                layer_ = i;
                selection_ = {};
            }
            shown_tab_ = i;
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

auto animation_panel::draw_graph(rtti::context& ctx, animation_controller& controller)
    -> std::optional<animation_graph_selection>
{
    animation_graph_selection selection = selection_;
    const auto graph_result = graph_.draw(controller, layer_, get_live(layer_), selection);
    apply_result(controller, graph_result);
    // An edit's selection (a new or deleted item) shows at once. A click's shows in the inspector
    // after it drew this frame: the field the click took the keyboard from is drawn once more, so
    // it reports the end of its edit and keeps it.
    std::optional<animation_graph_selection> clicked_selection;
    if(graph_result.changed)
    {
        selection_ = selection;
    }
    else if(selection != selection_)
    {
        clicked_selection = selection;
    }
    if(graph_result.rename_requested)
    {
        animation_controller_inspect_scope::request_name_focus();
    }
    if(!graph_result.dropped_clip_key.empty())
    {
        add_dropped_clip(ctx, controller, graph_result.dropped_clip_key, graph_result.drop_position);
    }
    return clicked_selection;
}

void animation_panel::add_dropped_clip(rtti::context& ctx,
                                       animation_controller& controller,
                                       const std::string& key,
                                       const math::vec2& position)
{
    auto clip = ctx.get_cached<asset_manager>().get_asset<animation_clip>(key);
    if(!clip)
    {
        return;
    }
    begin_edit(controller);
    auto& layer = controller.layers[layer_];
    animation_state_def state;
    state.name = make_unique_state_name(layer, fs::path(key).stem().string());
    state.clip = clip;
    state.editor_position = position;
    layer.states.push_back(state);
    if(layer.states.size() == 1)
    {
        layer.default_state = state.name;
    }
    selection_ = {animation_graph_selection::kind::state, int(layer.states.size() - 1)};
    apply_edit(controller, true);
}

void animation_panel::draw_toolbar(rtti::context& ctx, animation_controller& controller)
{
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(ICON_MDI_STATE_MACHINE);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted(fs::path(controller_.id()).filename().string().c_str());
    if(dirty_)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("*");
    }
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_CONTENT_SAVE))
    {
        save();
    }
    ImGui::SetItemTooltipEx("Save (Ctrl+S). Every finished edit saves on its own.");
    ImGui::SameLine();
    ImGui::BeginDisabled(undo_stack_.empty());
    if(ImGui::Button(ICON_MDI_UNDO))
    {
        undo(controller);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Undo (Ctrl+Z)");
    ImGui::SameLine();
    ImGui::BeginDisabled(redo_stack_.empty());
    if(ImGui::Button(ICON_MDI_REDO))
    {
        redo(controller);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Redo (Ctrl+Y)");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    draw_debug_target(ctx);
    ImGui::SameLine();
    draw_preview_button(ctx);
    if(get_live(layer_).valid)
    {
        ImGui::SameLine();
        ImGui::TextColored(COL_LIVE, ICON_MDI_ACCESS_POINT " LIVE");
    }
    draw_validation(controller);
}

void animation_panel::draw_debug_target(rtti::context& ctx)
{
    std::string target_label = "No entity";
    if(debug_entity_ && debug_entity_.valid())
    {
        if(auto* tag = debug_entity_.try_get<tag_component>())
        {
            target_label = tag->name;
        }
    }
    ImGui::SetNextItemWidth(DEBUG_TARGET_WIDTH);
    if(ImGui::BeginCombo("##debug_target", (ICON_MDI_BUG " " + target_label).c_str()))
    {
        if(ImGui::Selectable("Follow Selection", follow_selection_))
        {
            follow_selection_ = true;
            debug_entity_ = {};
        }
        ImGui::Separator();
        // The entities of the scene being edited: the prefab's in prefab mode.
        auto* active_scene = ctx.get_cached<editing_manager>().get_active_scene(ctx);
        if(active_scene != nullptr)
        {
            auto& registry = *active_scene->registry;
            registry.view<animation_component>().each(
                [&](auto e, auto&& animation)
                {
                    if(animation.get_controller() != controller_)
                    {
                        return;
                    }
                    entt::handle handle(registry, e);
                    auto* tag = handle.try_get<tag_component>();
                    const std::string label = (tag ? tag->name : std::string("Entity")) + "##" + std::to_string(uint32_t(e));
                    if(ImGui::Selectable(label.c_str(), debug_entity_ == handle))
                    {
                        debug_entity_ = handle;
                        follow_selection_ = false;
                    }
                });
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltipEx("Entity whose running controller is shown live: the selected one using this controller, "
                            "or in play mode the first one.");
}

void animation_panel::draw_preview_button(rtti::context& ctx)
{
    const bool playing_mode = ctx.get_cached<play_mode>().is_active();
    ImGui::BeginDisabled(playing_mode || get_debug_component() == nullptr);
    if(preview_.is_active())
    {
        if(ImGui::Button(ICON_MDI_STOP " Stop Preview"))
        {
            stop_preview();
        }
    }
    else if(ImGui::Button(ICON_MDI_PLAY " Preview"))
    {
        start_preview(ctx);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Runs the controller on the debug entity in edit mode. Parameters can be changed in the "
                            "Parameters list and by dragging the marker of a blend state. Stopping, closing the "
                            "window or entering play mode restores the pose.");
}

void animation_panel::draw_validation(const animation_controller& controller)
{
    const auto& problems = get_problems(controller);
    const char* status = problems.empty() ? ICON_MDI_CHECK_CIRCLE " Valid" : ICON_MDI_ALERT " Problems";
    const float status_width = ImGui::CalcTextSize(status).x + ImGui::GetStyle().FramePadding.x * 2.0f + STATUS_MARGIN;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - status_width));
    ImGui::TextColored(problems.empty() ? COL_OK : COL_WARNING, "%s", status);
    if(!problems.empty() && ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        for(const auto& problem : problems)
        {
            ImGui::BulletText("%s", problem.c_str());
        }
        ImGui::EndTooltip();
    }
}

void animation_panel::draw_sidebar(rtti::context& ctx, animation_controller& controller)
{
    if(ImGui::BeginTabBar("##sidebar_tabs"))
    {
        if(ImGui::BeginTabItem(ICON_MDI_LAYERS " Layers"))
        {
            draw_layers(controller);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(ICON_MDI_TUNE " Parameters"))
        {
            draw_parameters(controller);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void animation_panel::draw_layer_row(const animation_controller& controller, size_t index)
{
    const auto& layer = controller.layers[index];
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const float row_height = ImGui::GetFrameHeight();
    if(ImGui::Selectable("##layer_row", layer_ == index, ImGuiSelectableFlags_None, ImVec2(0.0f, row_height)))
    {
        layer_ = index;
        selection_ = {animation_graph_selection::kind::layer, int(index)};
    }
    const float row_right = ImGui::GetItemRectMax().x;
    const std::string name = layer.name.empty() ? std::string("Layer") : layer.name;
    std::string details = index == 0 ? "base" : (layer.blend_mode == animation_layer_blend_mode::additive ? "additive" : "override");
    if(index > 0)
    {
        char weight[16];
        ImFormatString(weight, sizeof(weight), "  %.2f", layer.weight);
        details += weight;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float text_x = row_min.x + style.FramePadding.x;
    const float text_y = row_min.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f;
    const float details_width = ImGui::CalcTextSize(details.c_str()).x;
    const float name_room = std::max(0.0f, row_right - text_x - style.FramePadding.x - style.ItemSpacing.x - details_width);
    const std::string fitted_name = fit_text(ImGui::GetFont(), ImGui::GetFontSize(), name, name_room);
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddText(ImVec2(text_x, text_y), ImGui::GetColorU32(ImGuiCol_Text), fitted_name.c_str());
    const float details_x = text_x + ImGui::CalcTextSize(fitted_name.c_str()).x + style.ItemSpacing.x;
    draw_list->AddText(ImVec2(details_x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), details.c_str());
    ImGui::SetItemTooltipEx("%s\nRight-click to move or delete it; Delete removes the selected layer.", name.c_str());
}

void animation_panel::remove_layer(animation_controller& controller, size_t index)
{
    if(index >= controller.layers.size() || controller.layers.size() <= 1)
    {
        return;
    }
    // The selection and a link or drag in the graph are indices into the layers before the removal.
    begin_edit(controller);
    controller.layers.erase(controller.layers.begin() + std::ptrdiff_t(index));
    layer_ = index > 0 ? index - 1 : 0;
    reset_selection();
    graph_.cancel_interaction();
    apply_edit(controller, true);
}

void animation_panel::draw_layers(animation_controller& controller)
{
    std::optional<size_t> removed;
    for(size_t i = 0; i < controller.layers.size(); ++i)
    {
        ImGui::PushID(int(i));
        draw_layer_row(controller, i);
        if(ImGui::BeginPopupContextItem("##layer_menu"))
        {
            // The moved layer is shown; the selection and a link or drag in the graph, indices into
            // the layer shown before, go.
            if(ImGui::MenuItem("Move Up", nullptr, false, i > 0))
            {
                begin_edit(controller);
                std::swap(controller.layers[i], controller.layers[i - 1]);
                layer_ = i - 1;
                reset_selection();
                graph_.cancel_interaction();
                apply_edit(controller, true);
            }
            if(ImGui::MenuItem("Move Down", nullptr, false, i + 1 < controller.layers.size()))
            {
                begin_edit(controller);
                std::swap(controller.layers[i], controller.layers[i + 1]);
                layer_ = i + 1;
                reset_selection();
                graph_.cancel_interaction();
                apply_edit(controller, true);
            }
            if(ImGui::MenuItem(ICON_MDI_DELETE " Delete", "Del", false, controller.layers.size() > 1))
            {
                removed = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if(removed)
    {
        remove_layer(controller, *removed);
    }
    // Delete removes the selected layer while this list has focus, not while the inspector's
    // fields of it do.
    if(ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(shortcuts::delete_item, false) &&
       selection_.what == animation_graph_selection::kind::layer && selection_.index >= 0)
    {
        remove_layer(controller, size_t(selection_.index));
    }
    ImGui::Spacing();
    if(ImGui::Button(ICON_MDI_PLUS " Add Layer", ImVec2(-1.0f, 0.0f)))
    {
        begin_edit(controller);
        auto& layer = controller.layers.emplace_back();
        layer.name = make_unique_name(
            [&](const std::string& name)
            {
                return controller.find_layer(name) >= 0;
            },
            "New Layer");
        auto& state = layer.states.emplace_back();
        state.name = "Empty";
        layer.default_state = state.name;
        layer_ = controller.layers.size() - 1;
        selection_ = {animation_graph_selection::kind::layer, int(layer_)};
        apply_edit(controller, true);
    }
    ImGui::TextWrapped("Layers above the base override (or add to) the layers below, per bone of their mask. "
                       "Right-click a layer to move or delete it.");
}

void animation_panel::draw_parameters(animation_controller& controller)
{
    auto* animation = get_debug_component();
    auto* instance = animation && animation->get_player().is_playing() ? animation->get_controller_instance() : nullptr;
    if(instance)
    {
        ImGui::TextColored(COL_LIVE, ICON_MDI_ACCESS_POINT " Live values");
    }
    if(ImGui::BeginTable("##parameters", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        int remove = -1;
        for(size_t i = 0; i < controller.parameters.size(); ++i)
        {
            auto& parameter = controller.parameters[i];
            ImGui::PushID(int(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", get_parameter_icon(parameter.type));
            ImGui::SameLine();
            if(ImGui::Selectable(parameter.name.c_str(), selection_.is(animation_graph_selection::kind::parameter, int(i))))
            {
                selection_ = {animation_graph_selection::kind::parameter, int(i)};
            }
            if(ImGui::BeginPopupContextItem("##parameter_menu"))
            {
                if(ImGui::MenuItem(ICON_MDI_RENAME_BOX " Rename", "F2"))
                {
                    selection_ = {animation_graph_selection::kind::parameter, int(i)};
                    animation_controller_inspect_scope::request_name_focus();
                }
                if(ImGui::MenuItem(ICON_MDI_DELETE " Delete", "Del"))
                {
                    remove = int(i);
                }
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            if(instance)
            {
                draw_live_value(parameter, *instance);
            }
            else
            {
                draw_parameter_default(controller, parameter);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if(remove >= 0)
        {
            remove_parameter(controller, size_t(remove));
        }
    }
    // Delete removes the selected parameter while this list has focus, not while the inspector's
    // fields of it do.
    if(ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(shortcuts::delete_item, false) &&
       selection_.what == animation_graph_selection::kind::parameter && selection_.index >= 0 &&
       size_t(selection_.index) < controller.parameters.size())
    {
        remove_parameter(controller, size_t(selection_.index));
    }
    ImGui::Spacing();
    draw_add_parameter(controller);
}

void animation_panel::draw_live_value(animation_parameter_def& parameter, animation_controller_instance& instance)
{
    const int index = instance.get_parameter_index(parameter.name);
    if(index < 0)
    {
        return;
    }
    auto value = instance.get_parameter_values()[size_t(index)];
    switch(parameter.type)
    {
        case animation_parameter_type::float_value:
            if(ImGui::DragFloat("##live", &value.float_value, 0.01f))
            {
                instance.set_parameter_from_float(size_t(index), value.float_value);
            }
            break;
        case animation_parameter_type::int_value:
            if(ImGui::DragInt("##live", &value.int_value))
            {
                instance.set_parameter_from_float(size_t(index), float(value.int_value));
            }
            break;
        case animation_parameter_type::bool_value:
            if(ImGui::Checkbox("##live", &value.bool_value))
            {
                instance.set_parameter_from_float(size_t(index), value.bool_value ? 1.0f : 0.0f);
            }
            break;
        case animation_parameter_type::trigger:
            if(ImGui::RadioButton("##live", value.bool_value))
            {
                instance.set_parameter_from_float(size_t(index), 1.0f);
            }
            ImGui::SetItemTooltipEx("Fire the trigger");
            break;
    }
}

void animation_panel::draw_parameter_default(animation_controller& controller, animation_parameter_def& parameter)
{
    switch(parameter.type)
    {
        case animation_parameter_type::float_value:
            ImGui::DragFloat("##default", &parameter.default_float, 0.01f);
            track_item_edit(controller);
            break;
        case animation_parameter_type::int_value:
            ImGui::DragInt("##default", &parameter.default_int);
            track_item_edit(controller);
            break;
        case animation_parameter_type::bool_value:
            if(ImGui::Checkbox("##default", &parameter.default_bool))
            {
                commit_edit(controller);
            }
            break;
        case animation_parameter_type::trigger:
            ImGui::TextDisabled("starts unset");
            break;
    }
}

void animation_panel::draw_add_parameter(animation_controller& controller)
{
    if(ImGui::Button(ICON_MDI_PLUS " Add Parameter", ImVec2(-1.0f, 0.0f)))
    {
        ImGui::OpenPopup("##add_parameter");
    }
    if(!ImGui::BeginPopup("##add_parameter"))
    {
        return;
    }
    for(auto type : PARAMETER_TYPES)
    {
        const std::string label = std::string(get_parameter_icon(type)) + " " + get_parameter_type_name(type);
        if(ImGui::MenuItem(label.c_str()))
        {
            begin_edit(controller);
            animation_parameter_def parameter;
            parameter.type = type;
            parameter.name = make_unique_name(
                [&](const std::string& name)
                {
                    return controller.find_parameter(name) >= 0;
                },
                std::string("New ") + get_parameter_type_name(type));
            controller.parameters.push_back(parameter);
            selection_ = {animation_graph_selection::kind::parameter, int(controller.parameters.size() - 1)};
            animation_controller_inspect_scope::request_name_focus();
            apply_edit(controller, true);
        }
    }
    ImGui::EndPopup();
}

void animation_panel::remove_parameter(animation_controller& controller, size_t index)
{
    begin_edit(controller);
    // Conditions and blend axes naming it stay, flagged as unknown, so nothing is lost silently.
    controller.parameters.erase(controller.parameters.begin() + std::ptrdiff_t(index));
    selection_ = {};
    apply_edit(controller, true);
}

// ---------------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------------

template<typename Definition>
void animation_panel::inspect_definition(rtti::context& ctx, animation_controller& controller, Definition& definition)
{
    animation_controller_inspect_scope scope({&controller,
                                              layer_,
                                              [&](animation_state_def& state)
                                              {
                                                  return draw_blend_plot(controller, state);
                                              },
                                              [&](animation_layer_def& layer)
                                              {
                                                  return draw_mask_bones(controller, layer);
                                              }});
    // The window keeps the controller's history: an edit is one of its undo steps, not the scene's.
    inspect_without_undo_scope no_undo;
    const auto result = inspect(ctx, definition);
    // Only a change begins an edit; an end alone (a name refused, a field left as it was) closes
    // the edit that is open, if any.
    if(result.changed)
    {
        begin_edit(controller);
    }
    if(result.changed || (result.edit_finished && edit_open_))
    {
        apply_edit(controller, result.edit_finished, result.changed);
    }
}

void animation_panel::draw_inspector_title(const char* title)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Separator();
}

void animation_panel::draw_inspector(rtti::context& ctx, animation_controller& controller)
{
    draw_selection_inspector(ctx, controller);
    // A rename request the selection had no name field for ends here.
    animation_controller_inspect_scope::cancel_name_focus();
}

void animation_panel::draw_selection_inspector(rtti::context& ctx, animation_controller& controller)
{
    auto& layer = controller.layers[layer_];
    switch(selection_.what)
    {
        case animation_graph_selection::kind::state:
            if(selection_.index >= 0 && size_t(selection_.index) < layer.states.size())
            {
                draw_state_inspector(ctx, controller, layer, size_t(selection_.index));
                return;
            }
            break;
        case animation_graph_selection::kind::transition:
            if(selection_.index >= 0 && size_t(selection_.index) < layer.transitions.size())
            {
                draw_transition_inspector(ctx, controller, layer, size_t(selection_.index));
                return;
            }
            break;
        case animation_graph_selection::kind::parameter:
            if(selection_.index >= 0 && size_t(selection_.index) < controller.parameters.size())
            {
                draw_parameter_inspector(ctx, controller, size_t(selection_.index));
                return;
            }
            break;
        case animation_graph_selection::kind::entry:
            draw_inspector_title(ICON_MDI_PLAY " Entry");
            ImGui::TextWrapped("The layer starts in its default state (orange). Right-click a state and choose "
                               "Set as Layer Default State to change it.");
            return;
        case animation_graph_selection::kind::any_state:
            draw_inspector_title(ICON_MDI_ASTERISK " Any State");
            ImGui::TextWrapped("Transitions from Any State fire from whatever state the layer is in, before that "
                               "state's own transitions: hit reactions, deaths, jumps. Right-click it and choose "
                               "Make Transition.");
            return;
        default:
            break;
    }
    draw_layer_inspector(ctx, controller, layer_);
}

void animation_panel::draw_parameter_inspector(rtti::context& ctx, animation_controller& controller, size_t index)
{
    ImGui::PushID("parameter");
    ImGui::PushID(int(index));
    draw_inspector_title((std::string(get_parameter_icon(controller.parameters[index].type)) + " Parameter").c_str());
    inspect_definition(ctx, controller, controller.parameters[index]);
    const auto& parameter = controller.parameters[index];
    ImGui::Spacing();
    ImGui::TextWrapped("Scripts: %s", get_script_setter(parameter).c_str());
    if(parameter.type == animation_parameter_type::float_value)
    {
        ImGui::TextWrapped("A curve named \"%s\" on a playing clip drives this parameter.", parameter.name.c_str());
    }
    const size_t uses = count_parameter_uses(controller, parameter.name);
    if(uses == 1)
    {
        ImGui::TextDisabled("Used once in this controller.");
    }
    else
    {
        ImGui::TextDisabled("Used %d times in this controller.", int(uses));
    }
    ImGui::Spacing();
    ImGui::Separator();
    if(ImGui::Button(ICON_MDI_DELETE " Delete Parameter", ImVec2(-1.0f, 0.0f)))
    {
        remove_parameter(controller, index);
    }
    if(uses > 0)
    {
        ImGui::SetItemTooltipEx("Conditions and blend axes using it stay and show as unknown.");
    }
    ImGui::PopID();
    ImGui::PopID();
}

void animation_panel::draw_state_inspector(rtti::context& ctx,
                                           animation_controller& controller,
                                           animation_layer_def& layer,
                                           size_t index)
{
    ImGui::PushID("state");
    ImGui::PushID(int(index));
    draw_inspector_title(ICON_MDI_CHECKBOX_BLANK_OUTLINE " State");
    if(get_default_state(layer) == int(index))
    {
        ImGui::TextColored(COL_DEFAULT_STATE, ICON_MDI_STAR " Layer default state");
    }
    else if(ImGui::SmallButton(ICON_MDI_STAR_OUTLINE " Set as default state"))
    {
        begin_edit(controller);
        layer.default_state = layer.states[index].name;
        apply_edit(controller, true);
    }
    inspect_definition(ctx, controller, layer.states[index]);
    draw_outgoing_transitions(controller, layer, layer.states[index]);
    ImGui::PopID();
    ImGui::PopID();
}

auto animation_panel::draw_blend_plot(animation_controller& controller, animation_state_def& state) -> inspect_result
{
    inspect_result result;
    auto& layer = controller.layers[layer_];
    const auto index = size_t(&state - layer.states.data());
    // The selected point, and a drag in the plot, belong to one state.
    if(point_owner_ != std::make_pair(layer_, index))
    {
        point_owner_ = {layer_, index};
        selected_point_ = -1;
        blend_view_.cancel_interaction();
    }
    const bool is_2d = state.motion != animation_motion_type::blend_1d;
    const auto weights = get_live_blend_weights(layer_, index, state);
    math::vec2 live_parameter{};
    const math::vec2* parameter_ptr = nullptr;
    auto* animation = weights.empty() ? nullptr : get_debug_component();
    if(auto* instance = animation ? animation->get_controller_instance() : nullptr)
    {
        live_parameter.x = instance->get_float(state.parameter_x);
        live_parameter.y = is_2d ? instance->get_float(state.parameter_y) : 0.0f;
        parameter_ptr = &live_parameter;
    }
    const auto blend_result = blend_view_.draw(state, weights, parameter_ptr, selected_point_);
    if(blend_result.clicked_point >= 0)
    {
        selected_point_ = blend_result.clicked_point;
    }
    result.changed = blend_result.changed;
    result.edit_finished = blend_result.finished;
    if(blend_result.preview_moved)
    {
        set_live_parameter(state.parameter_x, blend_result.preview_parameter.x);
        if(is_2d)
        {
            set_live_parameter(state.parameter_y, blend_result.preview_parameter.y);
        }
    }
    if(parameter_ptr)
    {
        ImGui::TextColored(COL_LIVE, ICON_MDI_ACCESS_POINT " Drag in the plot to preview parameter values.");
    }
    return result;
}

void animation_panel::draw_outgoing_transitions(animation_controller& controller,
                                                animation_layer_def& layer,
                                                const animation_state_def& state)
{
    ImGui::SeparatorText("Transitions");
    for(size_t i = 0; i < layer.transitions.size(); ++i)
    {
        const auto& transition = layer.transitions[i];
        if(transition.source != state.name)
        {
            continue;
        }
        const std::string label = std::string(ICON_MDI_ARROW_RIGHT " ") + transition.destination + "##outgoing" + std::to_string(i);
        if(ImGui::Selectable(label.c_str()))
        {
            selection_ = {animation_graph_selection::kind::transition, int(i)};
        }
    }
    if(!ImGui::BeginCombo("##add_transition", ICON_MDI_PLUS " Add Transition To..."))
    {
        return;
    }
    // The new transition goes in after the loop: adding one moves the list being drawn.
    int destination = -1;
    for(size_t i = 0; i < layer.states.size(); ++i)
    {
        if(ImGui::Selectable(layer.states[i].name.c_str()))
        {
            destination = int(i);
        }
    }
    ImGui::EndCombo();
    if(destination >= 0)
    {
        begin_edit(controller);
        animation_transition_def transition;
        transition.source = state.name;
        transition.destination = layer.states[size_t(destination)].name;
        transition.has_exit_time = true;
        layer.transitions.push_back(transition);
        selection_ = {animation_graph_selection::kind::transition, int(layer.transitions.size() - 1)};
        apply_edit(controller, true);
    }
}

void animation_panel::draw_transition_inspector(rtti::context& ctx,
                                                animation_controller& controller,
                                                animation_layer_def& layer,
                                                size_t index)
{
    ImGui::PushID("transition");
    ImGui::PushID(int(index));
    draw_inspector_title(ICON_MDI_ARROW_RIGHT_BOLD " Transition");
    const auto& shown = layer.transitions[index];
    ImGui::TextWrapped("%s  " ICON_MDI_ARROW_RIGHT "  %s",
                       shown.source.empty() ? "Any State" : shown.source.c_str(),
                       shown.destination.c_str());
    // Sibling transitions of the same pair, which the graph stacks on one arrow.
    const auto is_sibling = [&](const animation_transition_def& other)
    {
        return other.source == shown.source && other.destination == shown.destination;
    };
    if(std::count_if(layer.transitions.begin(), layer.transitions.end(), is_sibling) > 1)
    {
        int ordinal = 0;
        for(size_t i = 0; i < layer.transitions.size(); ++i)
        {
            if(!is_sibling(layer.transitions[i]))
            {
                continue;
            }
            const std::string label = "#" + std::to_string(++ordinal) + "##sibling" + std::to_string(i);
            ImGui::SameLine();
            if(ImGui::RadioButton(label.c_str(), i == index))
            {
                selection_ = {animation_graph_selection::kind::transition, int(i)};
            }
        }
    }
    inspect_definition(ctx, controller, layer.transitions[index]);
    const auto& transition = layer.transitions[index];
    if(transition.conditions.empty())
    {
        ImGui::TextDisabled(transition.has_exit_time ? "No conditions: fires at the exit time."
                                                     : "No conditions and no exit time: it never fires.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    if(ImGui::Button(ICON_MDI_DELETE " Delete Transition", ImVec2(-1.0f, 0.0f)))
    {
        begin_edit(controller);
        layer.transitions.erase(layer.transitions.begin() + std::ptrdiff_t(index));
        selection_ = {};
        apply_edit(controller, true);
    }
    ImGui::PopID();
    ImGui::PopID();
}

void animation_panel::draw_layer_inspector(rtti::context& ctx, animation_controller& controller, size_t index)
{
    ImGui::PushID("layer");
    ImGui::PushID(int(index));
    draw_inspector_title(ICON_MDI_LAYERS " Layer");
    inspect_definition(ctx, controller, controller.layers[index]);
    ImGui::Spacing();
    ImGui::BeginDisabled(controller.layers.size() <= 1);
    if(ImGui::Button(ICON_MDI_DELETE " Delete Layer", ImVec2(-1.0f, 0.0f)))
    {
        remove_layer(controller, index);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Removes the layer and its states; undo brings it back. A controller keeps at least one layer.");
    ImGui::PopID();
    ImGui::PopID();
}

auto animation_panel::get_mask_skeleton(const asset_handle<mesh>& mesh_asset) -> const mask_skeleton*
{
    const auto source = mesh_asset ? mesh_asset.try_get() : nullptr;
    if(!source)
    {
        return nullptr;
    }
    const bool is_cached = !mask_skeleton_.source.owner_before(source) && !source.owner_before(mask_skeleton_.source) &&
                           !mask_skeleton_.source.expired();
    if(!is_cached)
    {
        mask_skeleton_ = {};
        mask_skeleton_.source = source;
        if(const auto& root = source->get_armature())
        {
            collect_mesh_skeleton(*root, -1, mask_skeleton_.names, mask_skeleton_.parents);
        }
        mask_skeleton_.depths.resize(mask_skeleton_.names.size());
        for(size_t i = 0; i < mask_skeleton_.names.size(); ++i)
        {
            const int parent = mask_skeleton_.parents[i];
            mask_skeleton_.depths[i] = parent >= 0 ? mask_skeleton_.depths[size_t(parent)] + 1 : 0;
        }
    }
    return mask_skeleton_.names.empty() ? nullptr : &mask_skeleton_;
}

auto animation_panel::draw_mask_bones(animation_controller& controller, animation_layer_def& layer) -> inspect_result
{
    inspect_result result;
    // The skeleton: the preview model's armature, else the debug entity's.
    asset_handle<mesh> skeleton_mesh = controller.preview_model;
    if(!skeleton_mesh && get_debug_component() != nullptr)
    {
        if(auto* model = debug_entity_.try_get<model_component>())
        {
            skeleton_mesh = model->get_model().get_lod(0);
        }
    }
    if(layer.mask.empty())
    {
        ImGui::TextDisabled("Empty: the layer affects every bone.");
    }
    if(const auto* skeleton = get_mask_skeleton(skeleton_mesh))
    {
        if(draw_mask_tree(layer, *skeleton))
        {
            result.changed = true;
            result.edit_finished = true;
        }
    }
    else
    {
        ImGui::TextWrapped("Set a Preview Model, or select an entity using this controller, to pick bones from its "
                           "skeleton.");
    }
    return result;
}

auto animation_panel::draw_mask_tree(animation_layer_def& layer, const mask_skeleton& skeleton) -> bool
{
    {
        property_layout layout("Bones", "Tick a bone to put it in the mask; its children follow unless the entry's "
                                        "Include Children is off.");
        if(ImGui::InputTextWithHint("##mask_filter",
                                    ICON_MDI_MAGNIFY " Filter bones",
                                    mask_filter_.InputBuf,
                                    IM_ARRAYSIZE(mask_filter_.InputBuf)))
        {
            mask_filter_.Build();
        }
    }
    ImGui::BeginChild("##mask_tree", ImVec2(0.0f, MASK_TREE_HEIGHT), ImGuiChildFlags_Borders);
    // The weight each bone gets at runtime. A toggle changes the mask and ends the loop, so the
    // rows drawn all match these weights.
    const auto weights = compute_animation_mask_weights(layer.mask, skeleton.names, skeleton.parents);
    bool toggled = false;
    for(size_t i = 0; i < skeleton.names.size() && !toggled; ++i)
    {
        const auto& name = skeleton.names[i];
        if(!mask_filter_.PassFilter(name.c_str()))
        {
            continue;
        }
        ImGui::PushID(int(i));
        const auto it = std::find_if(layer.mask.begin(),
                                     layer.mask.end(),
                                     [&](const animation_mask_entry& entry)
                                     {
                                         return entry.bone == name;
                                     });
        bool included = it != layer.mask.end();
        const float effective = weights[i];
        // Indent(0) would indent by the style's default spacing.
        const float indent = float(skeleton.depths[i]) * MASK_INDENT;
        if(indent > 0.0f)
        {
            ImGui::Indent(indent);
        }
        toggled = ImGui::Checkbox("##in_mask", &included);
        ImGui::SameLine();
        ImGui::TextColored(effective > 0.0f ? COL_BONE_MASKED : COL_BONE_UNMASKED, "%s", name.c_str());
        if(effective > 0.0f && effective < 1.0f)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f", effective);
        }
        if(indent > 0.0f)
        {
            ImGui::Unindent(indent);
        }
        ImGui::PopID();
        if(toggled && included)
        {
            layer.mask.push_back({name, 1.0f, true});
        }
        else if(toggled)
        {
            layer.mask.erase(it);
        }
    }
    ImGui::EndChild();
    return toggled;
}

} // namespace unravel
