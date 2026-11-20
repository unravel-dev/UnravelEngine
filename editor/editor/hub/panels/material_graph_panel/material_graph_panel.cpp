#include "material_graph_panel.h"
#include "material_graph_style.h"

#include <editor/assets/asset_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/events.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/shortcuts.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/engine.h>
#include <engine/meta/rendering/graph_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/material_graph.hpp>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/volume_component.h>

#include <filedialog/filedialog.h>
#include <filesystem/filesystem.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <logging/logging.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace unravel
{

namespace
{
/// Undo snapshots kept per open graph.
constexpr size_t MAX_GRAPH_UNDO_STEPS = 100;
/// Saves of the window kept to recognise when the material's reload brings them back.
constexpr size_t MAX_OWN_GRAPH_SAVES = 16;
constexpr float MIN_GRAPH_SIDE_WIDTH = 160.0f;
/// The canvas keeps at least this much when a pane's handle is dragged.
constexpr float MIN_GRAPH_CANVAS_WIDTH = 240.0f;
/// The handle between a side pane and the canvas: the width it catches the mouse in, and the line it draws.
constexpr float PANE_HANDLE_WIDTH = 6.0f;
constexpr float PANE_HANDLE_LINE_WIDTH = 2.0f;
/// A collapsed side pane's tab: the room above its icon and between its icon and its name, in font heights.
constexpr float PANE_TAB_PADDING_EM = 0.5f;
/// The 3D preview is square, up to this part of the side pane's height.
constexpr float SCENE_PREVIEW_MAX_HEIGHT_RATIO = 0.45f;
constexpr float SCENE_PREVIEW_SHAPE_WIDTH = 96.0f;
constexpr const char* SCENE_PREVIEW_SHAPES[] = {"Sphere", "Cube", "Cylinder", "Plane"};
/// Room kept right of the status in the toolbar.
constexpr float GRAPH_STATUS_MARGIN = 12.0f;
constexpr float SETTING_DRAG_SPEED = 0.01f;
constexpr const char* GRAPH_EXTENSION = ".mgraph";
constexpr const char* GRAPH_FILTER = "*.mgraph";
constexpr const char* GRAPH_MATERIAL_FILTER = "*.mat";
constexpr const char* NEW_GRAPH_MATERIAL_NAME = "New Material Graph";
constexpr const char* EXAMPLES_FOLDER = "editor:/data/material_graphs/examples";
constexpr const char* UNTITLED_NAME = "Untitled";
constexpr const char* TEXTURE_SAMPLE_TYPE = "texture_sample";
constexpr const char* NEW_FUNCTION_NAME = "New Material Function";
constexpr const char* PROJECT_DATA_FOLDER = "app:/data";
constexpr const char* REROUTE_NAME_SETTING = "name";
/// A Custom node's code editor: lines shown, and the room its buffer keeps for typing.
constexpr float CODE_EDITOR_LINES = 10.0f;
constexpr size_t MIN_CODE_BUFFER_SIZE = 4096;
constexpr size_t CODE_BUFFER_ROOM = 2048;

const ImVec4 COL_ERROR_TEXT(0.95f, 0.38f, 0.33f, 1.0f);
const ImVec4 COL_WARNING_TEXT(1.0f, 0.72f, 0.3f, 1.0f);
const ImVec4 COL_OK_TEXT(0.45f, 0.85f, 0.5f, 1.0f);
const ImU32 COL_CODE_HIGHLIGHT = IM_COL32(80, 170, 255, 48);
const ImU32 COL_LINE_NUMBER = IM_COL32(110, 116, 126, 255);

auto make_graph_with_output() -> material_graph
{
    material_graph graph;
    ensure_material_graph_output(graph);
    return graph;
}

auto count_severity(const material_graph_compile_result& result, material_graph_severity severity) -> size_t
{
    return size_t(std::count_if(result.diagnostics.begin(),
                                result.diagnostics.end(),
                                [&](const material_graph_diagnostic& diagnostic)
                                {
                                    return diagnostic.severity == severity;
                                }));
}

/// @p text written down from @p top_center, turned a quarter clockwise: the name on a collapsed pane's tab.
void add_vertical_text(ImDrawList* draw_list, const ImVec2& top_center, ImU32 color, const char* text)
{
    const ImVec2 size = ImGui::CalcTextSize(text);
    const float half_height = size.y * 0.5f;
    // Drawn level, centred on the line through the pivot, then turned about it so it runs down from there. AddText
    // drops the glyphs outside the clip rectangle before they are turned, and the draw clips them where they end up:
    // the rectangle holds the level text, and the turned text where the window shows it.
    const ImVec2 level_min(top_center.x, top_center.y - half_height);
    const ImVec2 level_max(top_center.x + size.x, top_center.y + half_height);
    const ImVec2 window_max = draw_list->GetClipRectMax();
    const ImVec2 turned_min(top_center.x - half_height, top_center.y);
    const ImVec2 turned_max(top_center.x + half_height, std::min(top_center.y + size.x, window_max.y));
    draw_list->PushClipRect(ImMin(level_min, turned_min), ImMax(level_max, turned_max), false);
    const int first_vertex = draw_list->VtxBuffer.Size;
    draw_list->AddText(level_min, color, text);
    for(int i = first_vertex; i < draw_list->VtxBuffer.Size; ++i)
    {
        ImVec2& position = draw_list->VtxBuffer[i].pos;
        const ImVec2 offset(position.x - top_center.x, position.y - top_center.y);
        position = ImVec2(top_center.x - offset.y, top_center.y + offset.x);
    }
    draw_list->PopClipRect();
}

auto get_severity_color(material_graph_severity severity) -> ImVec4
{
    switch(severity)
    {
        case material_graph_severity::error:
            return COL_ERROR_TEXT;
        case material_graph_severity::warning:
            return COL_WARNING_TEXT;
        case material_graph_severity::info:
            break;
    }
    // A note, not a problem.
    return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
}

auto get_severity_icon(material_graph_severity severity) -> const char*
{
    switch(severity)
    {
        case material_graph_severity::error:
            return ICON_MDI_CLOSE_CIRCLE;
        case material_graph_severity::warning:
            return ICON_MDI_ALERT;
        case material_graph_severity::info:
            break;
    }
    return ICON_MDI_INFORMATION_OUTLINE;
}

/// The type an input of a compiled node reads; the declared type while the compile does not know it.
auto get_resolved_input_type(const material_graph_compile_result& compiled, size_t node, const material_pin_def& pin, size_t index)
    -> material_value_type
{
    if(node < compiled.nodes.size() && index < compiled.nodes[node].input_types.size())
    {
        return compiled.nodes[node].input_types[index];
    }
    return pin.type == material_value_type::dynamic ? material_value_type::float1 : pin.type;
}

/// A text field editing @p value through a buffer. True when the text changed this frame.
auto input_text(const char* label, std::string& value, ImGuiInputTextFlags flags = 0, const char* hint = nullptr) -> bool
{
    std::array<char, 512> buffer{};
    value.copy(buffer.data(), buffer.size() - 1);
    const bool edited = hint ? ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size(), flags)
                             : ImGui::InputText(label, buffer.data(), buffer.size(), flags);
    if(edited)
    {
        value = buffer.data();
    }
    return edited;
}
} // namespace

material_graph_panel::material_graph_panel(imgui_panels* parent) : parent_(parent)
{
}

void material_graph_panel::init(rtti::context& ctx)
{
    ctx_ = &ctx;
    previews_.init(ctx);
    view_.set_previews(&previews_, show_swatches_);
    set_untitled_graph(make_graph_with_output(), UNTITLED_NAME);
    find_examples();
    ctx.get_cached<ui_events>().on_close_project.connect(sentinel_, this, &material_graph_panel::on_project_closed);
}

void material_graph_panel::on_project_closed(rtti::context& ctx)
{
    (void)ctx;
    edit_open_ = false;
    set_untitled_graph(make_graph_with_output(), UNTITLED_NAME);
    previews_.reset();
}

void material_graph_panel::deinit(rtti::context& ctx)
{
    if(edit_open_)
    {
        apply_edit(true, false);
    }
    view_.set_previews(nullptr, false);
    scene_preview_.deinit(ctx);
    previews_.deinit();
}

void material_graph_panel::show(bool s)
{
    show_request_ = s;
}

auto material_graph_panel::is_focused() const -> bool
{
    return focused_;
}

void material_graph_panel::on_frame_render(rtti::context& ctx, const frame_times& times)
{
    if(!drawn_)
    {
        return;
    }
    compile_if_needed();
    previews_.update(ctx, graph_, compiled_, revision_, get_source_name());
    if(show_swatches_)
    {
        previews_.render_swatches(graph_);
    }
    scene_preview_.render(ctx, times, previews_.get_material());
}

void material_graph_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    (void)ctx;
    focused_ = false;
    if(show_request_)
    {
        show_request_ = false;
        show_ = true;
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.7f, ImGuiCond_Once);
        ImGui::SetNextWindowFocus();
    }
    bool is_drawn = false;
    if(show_)
    {
        if(ImGui::Begin(name, &show_, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            draw_ui(ctx);
            is_drawn = true;
        }
        ImGui::End();
    }
    // Closed or hidden: no field of it reports the end of an edit any more.
    if(drawn_ && !is_drawn && edit_open_)
    {
        apply_edit(true, false);
    }
    drawn_ = is_drawn;
}

// ---------------------------------------------------------------------------------
// Documents
// ---------------------------------------------------------------------------------

void material_graph_panel::set_graph(material_graph graph, const std::string& path, const std::string& name)
{
    // Another document leaves the way back behind; a reload of this one (sync_with_asset) keeps it.
    if(path.empty() || path != path_)
    {
        back_trail_.clear();
    }
    ensure_material_graph_output(graph);
    // Component links written by hand or by a tool show on component pins.
    split_material_graph_component_pins(graph);
    graph_ = std::move(graph);
    path_ = path;
    name_ = name;
    selection_.clear();
    undo_stack_.clear();
    redo_stack_.clear();
    edit_open_ = false;
    dirty_ = false;
    ++revision_;
    ++text_revision_;
    view_.cancel_interaction();
    view_.request_frame_all();
    renaming_node_.clear();
    // A graph written without positions is laid out before it is shown, so undo never goes back to the pile.
    if(is_material_graph_never_laid_out(graph_) && ImGui::GetCurrentContext())
    {
        layout_material_graph(graph_, view_.get_layout_metrics());
    }
    committed_snapshot_ = save_to_string(graph_);
}

void material_graph_panel::set_untitled_graph(material_graph graph, const std::string& name)
{
    set_graph(std::move(graph), {}, name);
    material_ = {};
    shown_material_.reset();
    live_code_.clear();
    own_saves_.clear();
}

auto material_graph_panel::open(const asset_handle<material>& asset) -> bool
{
    const auto loaded = asset ? asset.get() : nullptr;
    const auto* graph = loaded && !asset_handle<material>::is_empty(loaded) ? loaded->safe_cast<graph_material>() : nullptr;
    if(graph == nullptr)
    {
        return false;
    }
    if(asset != material_)
    {
        // The open edit goes to the graph it was made in.
        if(edit_open_)
        {
            apply_edit(true, false);
        }
        if(!confirm_discard())
        {
            return false;
        }
        set_graph(graph->get_graph(), fs::resolve_protocol(asset.id()).string(), asset.name());
        material_ = asset;
        shown_material_ = loaded;
        live_code_ = make_generated_code(graph->get_graph());
        own_saves_.clear();
    }
    show(true);
    return true;
}

void material_graph_panel::new_graph()
{
    if(confirm_discard())
    {
        set_untitled_graph(make_graph_with_output(), UNTITLED_NAME);
    }
}

void material_graph_panel::open_dialog()
{
    if(!confirm_discard())
    {
        return;
    }
    std::string picked;
    if(native::open_file_dialog(picked,
                                {GRAPH_MATERIAL_FILTER, GRAPH_FILTER},
                                "Material graphs",
                                "Open material graph",
                                fs::resolve_protocol("app:/data/").string()))
    {
        open_file(picked);
    }
}

void material_graph_panel::open_file(const std::string& absolute_path)
{
    if(fs::path(absolute_path).extension() == GRAPH_EXTENSION)
    {
        // A graph file of the project (a material function) is edited in place; the others, examples among them,
        // open untitled.
        const auto key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
        material_graph graph;
        if(key.rfind(PROJECT_DATA_FOLDER, 0) == 0 && load_from_file(absolute_path, graph))
        {
            set_graph(std::move(graph), absolute_path, fs::path(absolute_path).stem().string());
            material_ = {};
            shown_material_.reset();
            live_code_.clear();
            own_saves_.clear();
            return;
        }
        open_graph_file(absolute_path, fs::path(absolute_path).stem().string());
        return;
    }
    const auto key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
    const auto asset = ctx_ ? ctx_->get_cached<asset_manager>().get_asset<material>(key) : asset_handle<material>{};
    if(!open(asset))
    {
        native::message_box(fs::path(absolute_path).filename().string() + " is not a material graph.",
                            native::dialog_type::ok,
                            native::icon_type::warning,
                            "Material Graph");
    }
}

void material_graph_panel::open_graph_file(const std::string& absolute_path, const std::string& name)
{
    material_graph graph;
    if(!load_from_file(absolute_path, graph))
    {
        APPLOG_ERROR("Could not read the material graph {}.", absolute_path);
        return;
    }
    set_untitled_graph(std::move(graph), name);
}

auto material_graph_panel::open_path(const std::string& absolute_path) -> bool
{
    if(!confirm_discard())
    {
        return false;
    }
    open_file(absolute_path);
    show(true);
    return true;
}

auto material_graph_panel::is_graph_file_document() const -> bool
{
    return !material_ && !path_.empty() && fs::path(path_).extension() == GRAPH_EXTENSION;
}

auto material_graph_panel::is_saved_on_edit() const -> bool
{
    return material_ || is_graph_file_document();
}

auto material_graph_panel::detach_deleted_material() -> bool
{
    fs::error_code ec;
    if(!material_ || fs::exists(fs::resolve_protocol(material_.id()), ec))
    {
        return false;
    }
    APPLOG_WARNING("The material {} was deleted; its graph stays in the Material Graph window, unsaved, until Save As.",
                   material_.id());
    material_ = {};
    shown_material_.reset();
    live_code_.clear();
    path_.clear();
    dirty_ = true;
    return true;
}

void material_graph_panel::save_graph_file()
{
    if(!save_to_file(path_, graph_))
    {
        APPLOG_ERROR("Could not save the material graph {}; it stays unsaved.", path_);
        return;
    }
    dirty_ = false;
}

void material_graph_panel::save_function_as()
{
    const fs::path start = fs::resolve_protocol(std::string(PROJECT_DATA_FOLDER) + "/") / NEW_FUNCTION_NAME;
    std::string picked;
    if(!native::save_file_dialog(picked, {GRAPH_FILTER}, "Material functions", "Save material function as", start.string()))
    {
        return;
    }
    fs::path path(picked);
    if(path.extension() != GRAPH_EXTENSION)
    {
        path += GRAPH_EXTENSION;
    }
    if(fs::convert_to_protocol(path).generic_string().rfind(PROJECT_DATA_FOLDER, 0) != 0)
    {
        native::message_box("A material function is saved in the project, under its data folder.",
                            native::dialog_type::ok,
                            native::icon_type::warning,
                            "Material Graph");
        return;
    }
    path_ = path.string();
    name_ = path.stem().string();
    save_graph_file();
}

void material_graph_panel::save()
{
    if(is_graph_file_document())
    {
        save_graph_file();
        return;
    }
    if(detach_deleted_material())
    {
        return;
    }
    if(!material_)
    {
        if(is_material_function_graph(graph_))
        {
            save_function_as();
            return;
        }
        save_as();
        return;
    }
    const auto written = ctx_ ? asset_actions::save_material_graph(*ctx_, material_, graph_) : nullptr;
    if(!written)
    {
        APPLOG_ERROR("Could not save the material graph {}; it stays unsaved.", material_.id());
        return;
    }
    own_saves_.push_back(save_to_string(graph_));
    if(own_saves_.size() > MAX_OWN_GRAPH_SAVES)
    {
        own_saves_.erase(own_saves_.begin());
    }
    dirty_ = false;
}

void material_graph_panel::save_as()
{
    if(!ctx_)
    {
        return;
    }
    const fs::path start = path_.empty() ? fs::resolve_protocol("app:/data/") / NEW_GRAPH_MATERIAL_NAME
                                         : fs::path(path_).replace_extension();
    std::string picked;
    if(!native::save_file_dialog(picked, {GRAPH_MATERIAL_FILTER}, "Materials", "Save material graph as", start.string()))
    {
        return;
    }
    fs::path path(picked);
    if(path.extension() != ex::get_format<material>())
    {
        path += ex::get_format<material>();
    }
    if(!fs::has_known_protocol(fs::convert_to_protocol(path)))
    {
        native::message_box("A material graph is saved in the project, under its data folder.",
                            native::dialog_type::ok,
                            native::icon_type::warning,
                            "Material Graph");
        return;
    }
    const auto asset = asset_actions::create_material_graph(*ctx_, path, graph_);
    if(!asset)
    {
        APPLOG_ERROR("Could not save the material graph {}; it stays unsaved.", path.string());
        return;
    }
    material_ = asset;
    shown_material_ = asset.try_get();
    own_saves_ = {save_to_string(graph_)};
    path_ = path.string();
    name_ = path.stem().string();
    live_code_ = make_generated_code(graph_);
    dirty_ = false;
}

void material_graph_panel::push_values_to_asset()
{
    const auto loaded = material_.try_get();
    auto* graph = loaded && !asset_handle<material>::is_empty(loaded) ? loaded->safe_cast<graph_material>() : nullptr;
    compile_if_needed();
    if(graph == nullptr || compiled_.shader + compiled_.vertex_shader != live_code_)
    {
        return;
    }
    graph->set_graph(graph_);
    shown_material_ = loaded;
}

auto material_graph_panel::get_source_name() const -> std::string
{
    return path_.empty() ? name_ : fs::convert_to_protocol(fs::path(path_)).generic_string();
}

auto material_graph_panel::make_generated_code(const material_graph& graph) const -> std::string
{
    material_graph_compile_options options;
    options.source_name = get_source_name();
    const auto compiled = compile_material_graph(graph, options);
    return compiled.shader + compiled.vertex_shader;
}

void material_graph_panel::sync_with_asset()
{
    if(!material_)
    {
        return;
    }
    const auto loaded = material_.try_get();
    if(!loaded || asset_handle<material>::is_empty(loaded) || loaded == shown_material_.lock())
    {
        return;
    }
    const auto* graph = loaded->safe_cast<graph_material>();
    if(graph == nullptr)
    {
        return;
    }
    shown_material_ = loaded;
    live_code_ = make_generated_code(graph->get_graph());
    const std::string text = save_to_string(graph->get_graph());
    const auto echo = std::find(own_saves_.begin(), own_saves_.end(), text);
    if(echo != own_saves_.end())
    {
        // One of the window's saves coming back: the graph on screen, with what changed since, goes on.
        own_saves_.erase(own_saves_.begin(), echo + 1);
        if(save_to_string(graph_) != text)
        {
            push_values_to_asset();
        }
        return;
    }
    // A change made elsewhere, by hand or by a tool: what is on file now is shown, and the history starts over.
    own_saves_.clear();
    set_graph(graph->get_graph(), path_, name_);
}

auto material_graph_panel::has_unsaved_untitled_graph() const -> bool
{
    return dirty_ && path_.empty();
}

auto material_graph_panel::confirm_discard() -> bool
{
    if(!has_unsaved_untitled_graph())
    {
        return true;
    }
    return native::message_box("The graph \"" + name_ + "\" has changes that were never saved. Discard them?",
                               native::dialog_type::ok_cancel,
                               native::icon_type::question,
                               "Material Graph") == native::action_type::ok_or_yes;
}

void material_graph_panel::open_called_function(const std::string& absolute_path)
{
    auto trail = std::move(back_trail_);
    document_ref from{path_, name_};
    if(!open_path(absolute_path))
    {
        back_trail_ = std::move(trail);
        return;
    }
    // An untitled graph has no file to go back to.
    if(!from.path.empty() && from.path != path_)
    {
        trail.push_back(std::move(from));
    }
    back_trail_ = std::move(trail);
}

void material_graph_panel::go_back(size_t index)
{
    if(index >= back_trail_.size())
    {
        return;
    }
    auto trail = std::move(back_trail_);
    const document_ref target = trail[index];
    if(!open_path(target.path))
    {
        back_trail_ = std::move(trail);
        return;
    }
    trail.resize(index);
    back_trail_ = std::move(trail);
}

void material_graph_panel::find_examples()
{
    examples_.clear();
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(fs::resolve_protocol(EXAMPLES_FOLDER), ec))
    {
        if(entry.path().extension() == GRAPH_EXTENSION)
        {
            examples_.push_back({entry.path().stem().string(), entry.path().string()});
        }
    }
    std::sort(examples_.begin(),
              examples_.end(),
              [](const example& a, const example& b)
              {
                  return a.name < b.name;
              });
}

// ---------------------------------------------------------------------------------
// Editing, undo, compiling
// ---------------------------------------------------------------------------------

void material_graph_panel::begin_edit()
{
    if(edit_open_)
    {
        return;
    }
    // The snapshot from before the edit: widgets change the graph before they report it.
    undo_stack_.push_back(committed_snapshot_.empty() ? save_to_string(graph_) : committed_snapshot_);
    if(undo_stack_.size() > MAX_GRAPH_UNDO_STEPS)
    {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
    edit_open_ = true;
    edit_item_ = ImGui::GetActiveID();
}

void material_graph_panel::apply_edit(bool finished, bool code_change)
{
    if(code_change)
    {
        ++revision_;
    }
    ++text_revision_;
    dirty_ = true;
    if(!finished)
    {
        // The scene follows a value while it is dragged; the file takes it when the drag ends.
        if(code_change && material_)
        {
            push_values_to_asset();
        }
        return;
    }
    edit_open_ = false;
    committed_snapshot_ = save_to_string(graph_);
    if(is_saved_on_edit())
    {
        save();
    }
}

void material_graph_panel::apply_result(const material_graph_edit_result& result)
{
    if(result.begin_edit)
    {
        begin_edit();
    }
    if(result.changed || result.layout_changed || result.finished)
    {
        apply_edit(result.finished, result.changed);
    }
}

void material_graph_panel::commit_edit()
{
    begin_edit();
    apply_edit(true);
}

void material_graph_panel::track_item_edit()
{
    track_value_edit(ImGui::IsItemEdited(), ImGui::IsItemDeactivatedAfterEdit());
}

void material_graph_panel::track_value_edit(bool is_changed, bool is_finished)
{
    if(is_changed)
    {
        begin_edit();
        apply_edit(false);
    }
    if(is_finished && edit_open_)
    {
        apply_edit(true, false);
    }
}

void material_graph_panel::finish_abandoned_edit()
{
    // The item may lose the mouse or keyboard during a frame and report it in the next one.
    const ImGuiID previous = ImGui::GetCurrentContext()->ActiveIdPreviousFrame;
    const bool is_item_in_use = edit_item_ != 0 && (ImGui::GetActiveID() == edit_item_ || previous == edit_item_);
    if(edit_open_ && !is_item_in_use && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        apply_edit(true, false);
    }
}

void material_graph_panel::undo()
{
    step_history(undo_stack_, redo_stack_);
}

void material_graph_panel::redo()
{
    step_history(redo_stack_, undo_stack_);
}

void material_graph_panel::step_history(std::vector<std::string>& from, std::vector<std::string>& to)
{
    if(from.empty())
    {
        return;
    }
    to.push_back(save_to_string(graph_));
    material_graph restored;
    if(load_from_string(from.back(), restored))
    {
        graph_ = std::move(restored);
    }
    committed_snapshot_ = from.back();
    from.pop_back();
    edit_open_ = false;
    selection_.clear();
    view_.cancel_interaction();
    ++revision_;
    ++text_revision_;
    dirty_ = true;
    if(is_saved_on_edit())
    {
        save();
    }
}

void material_graph_panel::compile_if_needed()
{
    if(compiled_revision_ == revision_)
    {
        return;
    }
    material_graph_compile_options options;
    options.source_name = get_source_name();
    compiled_ = compile_material_graph(graph_, options);
    compiled_revision_ = revision_;
}

void material_graph_panel::lay_out()
{
    begin_edit();
    layout_material_graph(graph_, view_.get_layout_metrics());
    apply_edit(true, false);
    view_.request_frame_all();
}

void material_graph_panel::select_node(const std::string& id)
{
    selection_.clear();
    selection_.nodes = {id};
}

// ---------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------

void material_graph_panel::draw_ui(rtti::context& ctx)
{
    sync_with_asset();
    handle_shortcuts();
    compile_if_needed();
    draw_toolbar();
    ImGui::Separator();
    draw_layout(ctx);
    finish_abandoned_edit();
    if(!pending_open_.empty())
    {
        const std::string path = std::move(pending_open_);
        pending_open_.clear();
        open_called_function(path);
    }
    if(pending_back_)
    {
        const size_t index = *pending_back_;
        pending_back_.reset();
        go_back(index);
    }
}

void material_graph_panel::handle_shortcuts()
{
    // None while an item is in use or a popup is open: both can hold a node of the graph an undo would replace.
    if(!focused_ || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(ImGuiID(0), ImGuiPopupFlags_AnyPopup))
    {
        return;
    }
    if(!ImGui::IsCombinationKeyPressed(shortcuts::save_scene_as) && ImGui::IsCombinationKeyPressed(shortcuts::save_scene))
    {
        save();
    }
    if(!back_trail_.empty() && ImGui::IsKeyPressed(shortcuts::navigate_back, false))
    {
        pending_back_ = back_trail_.size() - 1;
    }
    // Ctrl+Shift+Z matches Ctrl+Z too: redo goes first.
    if(ImGui::IsCombinationKeyPressed(shortcuts::redo, true) || ImGui::IsCombinationKeyPressed(shortcuts::redo_alt, true))
    {
        redo();
    }
    else if(ImGui::IsCombinationKeyPressed(shortcuts::undo, true))
    {
        undo();
    }
}

void material_graph_panel::draw_pane_toggle(const char* icon, bool& shown, const char* tooltip)
{
    // The colour pushed is popped by the state it was pushed for, not by the one the click leaves.
    const bool was_shown = shown;
    if(was_shown)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if(ImGui::Button(icon))
    {
        shown = !shown;
    }
    if(was_shown)
    {
        ImGui::PopStyleColor();
    }
    set_wrapped_item_tooltip("%s", tooltip);
}

void material_graph_panel::draw_back_trail()
{
    if(back_trail_.empty())
    {
        return;
    }
    if(ImGui::Button(ICON_MDI_ARROW_LEFT))
    {
        pending_back_ = back_trail_.size() - 1;
    }
    set_wrapped_item_tooltip("Back to %s (Backspace).", back_trail_.back().name.c_str());
    for(size_t i = 0; i < back_trail_.size(); ++i)
    {
        ImGui::SameLine();
        ImGui::PushID(int(i));
        if(ImGui::SmallButton(back_trail_[i].name.c_str()))
        {
            pending_back_ = i;
        }
        ImGui::PopID();
        set_wrapped_item_tooltip("Back to %s, which the graphs after it were opened from.", back_trail_[i].name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_MDI_CHEVRON_RIGHT);
    }
    ImGui::SameLine();
}

void material_graph_panel::draw_toolbar()
{
    draw_pane_toggle(ICON_MDI_ROTATE_3D_VARIANT, show_scene_preview_, "Show or hide the 3D preview of the material, at the top of the side pane.");
    ImGui::SameLine();
    draw_pane_toggle(ICON_MDI_PALETTE_SWATCH, show_swatches_, "Show or hide the preview of every node's value. A node's own preview hides from its context menu.");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    draw_back_trail();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(ICON_MDI_GRAPH);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted(name_.c_str());
    if(dirty_)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("*");
    }
    ImGui::SameLine();
    draw_domain_combo();
    draw_add_to_scene_button();
    ImGui::SameLine();
    draw_new_menu();
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_FOLDER_OPEN))
    {
        open_dialog();
    }
    set_wrapped_item_tooltip("Open a material graph: a graph material (.mat), or a graph file (.mgraph) as an untitled graph.");
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_CONTENT_SAVE))
    {
        save();
    }
    set_wrapped_item_tooltip("Save (Ctrl+S). A graph material saves every finished edit on its own; an untitled graph "
                            "becomes one.");
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_CONTENT_SAVE_EDIT))
    {
        save_as();
    }
    set_wrapped_item_tooltip("Save as a new graph material (.mat) in the project.");
    ImGui::SameLine();
    ImGui::BeginDisabled(undo_stack_.empty());
    if(ImGui::Button(ICON_MDI_UNDO))
    {
        undo();
    }
    ImGui::EndDisabled();
    set_wrapped_item_tooltip("Undo (Ctrl+Z)");
    ImGui::SameLine();
    ImGui::BeginDisabled(redo_stack_.empty());
    if(ImGui::Button(ICON_MDI_REDO))
    {
        redo();
    }
    ImGui::EndDisabled();
    set_wrapped_item_tooltip("Redo (Ctrl+Y)");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_SITEMAP " Arrange"))
    {
        lay_out();
    }
    set_wrapped_item_tooltip("Arranges every node in columns flowing into the output node.");
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_FIT_TO_PAGE_OUTLINE))
    {
        view_.request_frame_all();
    }
    set_wrapped_item_tooltip("Frame the whole graph (F frames the selection).");
    draw_status();
}

void material_graph_panel::draw_domain_combo()
{
    if(is_material_function_graph(graph_))
    {
        return;
    }
    const auto domain = get_material_graph_domain(graph_);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize(get_material_domain_label(material_domain::post_process)).x + ImGui::GetFrameHeight() * 2.0f);
    if(ImGui::BeginCombo("##material_domain", get_material_domain_label(domain)))
    {
        for(uint32_t i = 0; i < MATERIAL_DOMAIN_COUNT; ++i)
        {
            const auto option = material_domain(i);
            if(ImGui::Selectable(get_material_domain_label(option), option == domain) && option != domain)
            {
                begin_edit();
                set_material_graph_domain(graph_, option);
                apply_edit(true, true);
            }
        }
        ImGui::EndCombo();
    }
    set_wrapped_item_tooltip("What the material draws. Surface: the surface of a mesh. Decal: what a Decal component's box "
                             "lays onto the surfaces it projects onto. Post Process: the colour of the view, from a volume or "
                             "a camera. Changing it keeps every link the new output node has a pin for.");
}

void material_graph_panel::draw_add_to_scene_button()
{
    if(is_material_function_graph(graph_) || get_material_graph_domain(graph_) != material_domain::post_process)
    {
        return;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!material_);
    if(ImGui::Button(ICON_MDI_PLUS_BOX_OUTLINE " Add to Scene"))
    {
        add_to_scene();
    }
    ImGui::EndDisabled();
    set_wrapped_item_tooltip("Runs this material over the view: on the selected post process volume or camera, or on the "
                             "scene's global volume, which is made when the scene has none. Save an untitled graph first.");
}

void material_graph_panel::add_to_scene()
{
    auto& em = ctx_->get_cached<editing_manager>();
    auto* scn = em.get_active_scene(*ctx_);
    if(scn == nullptr || !material_)
    {
        return;
    }
    entt::handle target;
    if(auto* selected = em.try_get_active_selection_as<entt::handle>();
       selected != nullptr && *selected && selected->any_of<volume_component, camera_component>())
    {
        target = *selected;
    }
    // Added to show: an entry of the material switched off is switched on.
    const auto placement = editor_actions::set_post_process_material(*ctx_, *scn, target, material_, {.enabled = true});
    if(!placement.entity)
    {
        ImGui::PushNotification(ImGuiToast(ImGuiToastType_Warning, 4000, "%s", placement.error.c_str()));
        return;
    }
    em.select(placement.entity);
}

void material_graph_panel::draw_new_menu()
{
    if(ImGui::Button(ICON_MDI_FILE_PLUS " New"))
    {
        ImGui::OpenPopup("##material_graph_new");
    }
    set_wrapped_item_tooltip("Start an empty graph or one of the examples.");
    if(!ImGui::BeginPopup("##material_graph_new"))
    {
        return;
    }
    if(ImGui::MenuItem(ICON_MDI_FILE_DOCUMENT_OUTLINE " Empty Graph"))
    {
        new_graph();
    }
    if(ImGui::MenuItem(ICON_MDI_STICKER_OUTLINE " Decal Material") && confirm_discard())
    {
        set_untitled_graph(asset_actions::make_starter_material_graph(material_domain::decal), "Untitled Decal");
    }
    set_wrapped_item_tooltip("A decal: a colour map tinted, its alpha the opacity. A Decal component projects it onto the "
                             "surfaces in its box.");
    if(ImGui::MenuItem(ICON_MDI_IMAGE_FILTER_VINTAGE " Post Process Material") && confirm_discard())
    {
        set_untitled_graph(asset_actions::make_starter_material_graph(material_domain::post_process), "Untitled Post Process");
    }
    set_wrapped_item_tooltip("A post-process material: the scene's colour tinted. A volume's or a camera's Post Process "
                             "Materials run it.");
    if(ImGui::MenuItem(ICON_MDI_FUNCTION_VARIANT " Empty Material Function") && confirm_discard())
    {
        set_untitled_graph(asset_actions::make_starter_material_function(), "Untitled Function");
    }
    set_wrapped_item_tooltip("A graph of Function Inputs and Outputs that materials use through Material Function nodes. "
                            "Save puts it in the project as a .mgraph file.");
    ImGui::SeparatorText("Examples");
    for(const auto& example : examples_)
    {
        if(ImGui::MenuItem(example.name.c_str()) && confirm_discard())
        {
            open_graph_file(example.path, example.name + " (example)");
        }
    }
    if(examples_.empty())
    {
        ImGui::TextDisabled("No examples found in %s.", EXAMPLES_FOLDER);
    }
    ImGui::EndPopup();
}

void material_graph_panel::draw_status()
{
    const size_t errors = count_severity(compiled_, material_graph_severity::error);
    const size_t warnings = count_severity(compiled_, material_graph_severity::warning);
    std::string status;
    ImVec4 color = COL_OK_TEXT;
    if(errors > 0)
    {
        status = std::string(ICON_MDI_CLOSE_CIRCLE " ") + std::to_string(errors) + (errors == 1 ? " error" : " errors");
        color = COL_ERROR_TEXT;
    }
    else if(warnings > 0)
    {
        status = std::string(ICON_MDI_ALERT " ") + std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings");
        color = COL_WARNING_TEXT;
    }
    else
    {
        status = ICON_MDI_CHECK_CIRCLE " Compiles";
    }
    const float width = ImGui::CalcTextSize(status.c_str()).x + GRAPH_STATUS_MARGIN;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width));
    ImGui::TextColored(color, "%s", status.c_str());
    if(!compiled_.diagnostics.empty())
    {
        ImGui::ItemTooltipEx(
            [&]()
            {
                for(const auto& diagnostic : compiled_.diagnostics)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, get_severity_color(diagnostic.severity));
                    ImGui::TextWrapped("%s%s",
                                       diagnostic.node_id.empty() ? "" : (diagnostic.node_id + ": ").c_str(),
                                       diagnostic.message.c_str());
                    ImGui::PopStyleColor();
                }
            });
    }
}

void material_graph_panel::draw_layout(rtti::context& ctx)
{
    const float total_width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetContentRegionAvail().y;
    // A collapsed pane leaves its tab; an open one its width and its handle. The canvas takes the rest.
    const float tab_width = ImGui::GetFrameHeight();
    const float palette_space = show_palette_ ? palette_width_ + PANE_HANDLE_WIDTH : tab_width;
    const float side_space = show_side_ ? side_width_ + PANE_HANDLE_WIDTH : tab_width;
    const float canvas_width = std::max(total_width - palette_space - side_space, MIN_GRAPH_CANVAS_WIDTH);
    if(show_palette_)
    {
        ImGui::BeginChild("##palette", ImVec2(palette_width_, height), ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_palette();
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        const float max_width = std::max(total_width - side_space - PANE_HANDLE_WIDTH - MIN_GRAPH_CANVAS_WIDTH, MIN_GRAPH_SIDE_WIDTH);
        draw_pane_handle("##palette_handle", {palette_width_, show_palette_, 1.0f, max_width}, height, "palette");
    }
    else
    {
        draw_pane_tab("##palette_tab", ICON_MDI_SHAPE_PLUS, "Palette", show_palette_, height);
    }
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##canvas",
                      ImVec2(canvas_width, height),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    view_.set_previews(&previews_, show_swatches_);
    const auto result = view_.draw(graph_, compiled_, selection_);
    apply_result(result);
    apply_texture_drop(ctx, result);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    if(!show_side_)
    {
        draw_pane_tab("##side_tab", ICON_MDI_INFORMATION_OUTLINE, "Details", show_side_, height);
        return;
    }
    const float max_width = std::max(total_width - palette_space - PANE_HANDLE_WIDTH - MIN_GRAPH_CANVAS_WIDTH, MIN_GRAPH_SIDE_WIDTH);
    draw_pane_handle("##side_handle", {side_width_, show_side_, -1.0f, max_width}, height, "side pane");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##side", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    draw_side(ctx);
    ImGui::EndChild();
}

void material_graph_panel::draw_pane_handle(const char* id, const pane_edge& pane, float height, const char* pane_name)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(PANE_HANDLE_WIDTH, height));
    const bool is_active = ImGui::IsItemActive();
    const bool is_hovered = ImGui::IsItemHovered();
    if(is_active || is_hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if(is_active)
    {
        // The edge follows the mouse: a pane on the right grows as the mouse moves left.
        pane.width = std::clamp(pane.width + pane.direction * ImGui::GetIO().MouseDelta.x, MIN_GRAPH_SIDE_WIDTH, pane.max_width);
    }
    if(is_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        pane.is_shown = false;
    }
    set_wrapped_item_tooltip("Drag to resize the %s. Double-click to collapse it.", pane_name);
    const ImGuiCol color = is_active ? ImGuiCol_SeparatorActive : (is_hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
    const float center = min.x + PANE_HANDLE_WIDTH * 0.5f;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(center - PANE_HANDLE_LINE_WIDTH * 0.5f, min.y),
                                              ImVec2(center + PANE_HANDLE_LINE_WIDTH * 0.5f, min.y + height),
                                              ImGui::GetColorU32(color));
}

void material_graph_panel::draw_pane_tab(const char* id, const char* icon, const char* name, bool& is_shown, float height)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetFrameHeight();
    if(ImGui::InvisibleButton(id, ImVec2(width, height)))
    {
        is_shown = true;
    }
    set_wrapped_item_tooltip("Show the %s.", name);
    auto* draw_list = ImGui::GetWindowDrawList();
    const ImGuiCol background = ImGui::IsItemActive() ? ImGuiCol_ButtonActive : (ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    draw_list->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(background));
    const float padding = ImGui::GetFontSize() * PANE_TAB_PADDING_EM;
    const float center = min.x + width * 0.5f;
    const ImVec2 icon_size = ImGui::CalcTextSize(icon);
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    draw_list->AddText(ImVec2(center - icon_size.x * 0.5f, min.y + padding), text_color, icon);
    add_vertical_text(draw_list, ImVec2(center, min.y + padding * 2.0f + icon_size.y), text_color, name);
}

void material_graph_panel::apply_texture_drop(rtti::context& ctx, const material_graph_view::result& result)
{
    if(result.dropped_texture_key.empty())
    {
        return;
    }
    auto texture = ctx.get_cached<asset_manager>().get_asset<gfx::texture>(result.dropped_texture_key, load_flags::standard, load_mode::deferred);
    if(!texture)
    {
        return;
    }
    begin_edit();
    int index = find_material_graph_node(graph_, result.drop_node);
    if(index < 0)
    {
        index = view_.create_node(graph_, TEXTURE_SAMPLE_TYPE, result.drop_position);
        const std::string id = rename_material_graph_node(graph_, graph_.nodes[size_t(index)].id, fs::path(result.dropped_texture_key).stem().string());
        index = find_material_graph_node(graph_, id);
    }
    graph_.nodes[size_t(index)].texture = texture;
    select_node(graph_.nodes[size_t(index)].id);
    apply_edit(true);
}

void material_graph_panel::draw_palette()
{
    const float collapse_width = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - collapse_width - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##palette_filter", ICON_MDI_MAGNIFY " Search nodes...", palette_filter_.data(), palette_filter_.size());
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_CHEVRON_DOUBLE_LEFT, ImVec2(collapse_width, 0.0f)))
    {
        show_palette_ = false;
    }
    set_wrapped_item_tooltip("Collapse the palette; its tab at the window's edge opens it again.");
    const std::string filter = make_lowercase(palette_filter_.data());
    ImGui::BeginChild("##palette_list");
    int category = -1;
    bool is_open = true;
    const auto domain = get_material_graph_domain(graph_);
    for(const auto& def : get_material_node_library())
    {
        if(def.code == material_node_code::surface_output || !is_material_node_in_domain(def, domain))
        {
            continue;
        }
        const std::string words = make_lowercase(def.label + " " + def.type + " " + def.keywords);
        if(!filter.empty() && words.find(filter) == std::string::npos)
        {
            continue;
        }
        if(int(def.category) != category)
        {
            if(category >= 0 && is_open)
            {
                ImGui::TreePop();
            }
            category = int(def.category);
            const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            is_open = ImGui::TreeNodeEx(get_material_node_category_name(def.category), filter.empty() ? flags : flags | ImGuiTreeNodeFlags_Leaf);
            if(!filter.empty() && !is_open)
            {
                is_open = true;
            }
        }
        if(is_open)
        {
            draw_palette_entry(def);
        }
    }
    if(category >= 0 && is_open)
    {
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

void material_graph_panel::draw_palette_entry(const material_node_def& def)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(get_material_category_color(def.category) | IM_COL32_A_MASK));
    ImGui::TextUnformatted(get_material_category_icon(def.category));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Selectable((def.label + "##palette_" + def.type).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        view_.add_node_at_center(graph_, def.type, selection_);
        commit_edit();
    }
    if(ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(MATERIAL_GRAPH_NODE_PAYLOAD, def.type.data(), def.type.size());
        ImGui::TextUnformatted((std::string(get_material_category_icon(def.category)) + " " + def.label).c_str());
        ImGui::EndDragDropSource();
    }
    if(def.shortcut != 0)
    {
        set_wrapped_item_tooltip("%s\n\nDrag onto the canvas, double-click, or hold %c and click the canvas.", def.description.c_str(), def.shortcut);
    }
    else
    {
        set_wrapped_item_tooltip("%s\n\nDrag onto the canvas or double-click.", def.description.c_str());
    }
}

void material_graph_panel::draw_side(rtti::context& ctx)
{
    if(show_scene_preview_)
    {
        draw_scene_preview();
    }
    if(!ImGui::BeginTabBar("##side_tabs"))
    {
        return;
    }
    if(ImGui::TabItemButton(ICON_MDI_CHEVRON_DOUBLE_RIGHT, ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
    {
        show_side_ = false;
    }
    set_wrapped_item_tooltip("Collapse the side pane; its tab at the window's edge opens it again.");
    if(ImGui::BeginTabItem(ICON_MDI_INFORMATION_OUTLINE " Details"))
    {
        draw_details(ctx);
        ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(ICON_MDI_CODE_BRACES " Generated Code"))
    {
        draw_code();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void material_graph_panel::draw_scene_preview()
{
    const float width = ImGui::GetContentRegionAvail().x;
    const float side = std::max(std::min(width, ImGui::GetContentRegionAvail().y * SCENE_PREVIEW_MAX_HEIGHT_RATIO), 1.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    scene_preview_.draw(ImVec2(width, side));
    set_wrapped_item_tooltip("Drag to turn the camera around the shape, scroll to zoom, double-click to go back to the first view.");
    const ImVec2 after = ImGui::GetCursorScreenPos();
    // The shape picker sits over the preview's top left corner.
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    ImGui::SetCursorScreenPos(origin + padding);
    int shape = int(scene_preview_.get_shape());
    ImGui::SetNextItemWidth(SCENE_PREVIEW_SHAPE_WIDTH);
    if(ImGui::Combo("##preview_shape", &shape, SCENE_PREVIEW_SHAPES, int(std::size(SCENE_PREVIEW_SHAPES))))
    {
        scene_preview_.set_shape(material_graph_scene_preview::shape(shape));
    }
    set_wrapped_item_tooltip("The shape the preview shows the material on.");
    ImGui::SetCursorScreenPos(after);
    draw_preview_state();
}

void material_graph_panel::draw_preview_state()
{
    if(previews_.has_graph_errors())
    {
        ImGui::TextColored(COL_WARNING_TEXT, ICON_MDI_ALERT " The previews show the last graph without errors.");
    }
    else if(previews_.has_failed() && !previews_.is_compiling())
    {
        ImGui::TextColored(COL_ERROR_TEXT, ICON_MDI_CLOSE_CIRCLE " The preview shaders did not compile. The log says why.");
    }
    else if(previews_.is_compiling())
    {
        ImGui::TextDisabled(ICON_MDI_TIMER_SAND " Compiling the previews...");
    }
}

void material_graph_panel::draw_details(rtti::context& ctx)
{
    if(selection_.comment >= 0 && size_t(selection_.comment) < graph_.comments.size())
    {
        draw_comment_details();
        return;
    }
    if(selection_.nodes.size() == 1)
    {
        const int index = find_material_graph_node(graph_, selection_.nodes.front());
        if(index >= 0)
        {
            draw_node_details(ctx, size_t(index));
            return;
        }
    }
    if(selection_.nodes.size() > 1)
    {
        ImGui::TextDisabled("%zu nodes selected.", selection_.nodes.size());
        ImGui::Spacing();
    }
    draw_graph_summary();
}

void material_graph_panel::draw_node_details(rtti::context& ctx, size_t index)
{
    draw_node_title(index);
    const auto* def = find_material_node_def(graph_.nodes[index].type);
    if(!def)
    {
        ImGui::TextColored(COL_ERROR_TEXT, "Unknown node type \"%s\".", graph_.nodes[index].type.c_str());
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", def->description.c_str());
    ImGui::PopTextWrapPos();
    draw_diagnostics(compiled_.get_node_diagnostics(graph_.nodes[index].id));
    if(index < compiled_.nodes.size() && !compiled_.nodes[index].is_used)
    {
        ImGui::TextColored(COL_WARNING_TEXT, ICON_MDI_INFORMATION_OUTLINE " Not used: nothing it feeds reaches the output node.");
    }
    if(holds_material_graph_texture(graph_.nodes[index]))
    {
        draw_texture_field(ctx, index);
    }
    if(def->code == material_node_code::color_ramp)
    {
        draw_color_ramp(index);
    }
    if(!def->settings.empty())
    {
        ImGui::SeparatorText("Settings");
        for(const auto& setting : def->settings)
        {
            // The Surface Output hides the settings its Blend Mode does not read.
            if(def->code != material_node_code::surface_output || is_material_output_setting_shown(graph_.nodes[index], setting.name))
            {
                draw_setting(graph_.nodes[index], setting);
            }
        }
    }
    if(!def->inputs.empty())
    {
        ImGui::SeparatorText("Inputs");
        draw_inputs(index);
    }
    if(!def->outputs.empty())
    {
        ImGui::SeparatorText("Outputs");
        draw_outputs(index);
    }
}

void material_graph_panel::draw_node_title(size_t index)
{
    auto& node = graph_.nodes[index];
    const auto* def = find_material_node_def(node.type);
    if(def)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(get_material_category_color(def->category) | IM_COL32_A_MASK));
        ImGui::TextUnformatted(get_material_category_icon(def->category));
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(def ? def->label.c_str() : node.type.c_str());
    ImGui::PopFont();
    // The id: links and the generated code name the node by it. Renamed when the field is left.
    if(renaming_node_ != node.id)
    {
        renaming_node_ = node.id;
        rename_text_.fill('\0');
        node.id.copy(rename_text_.data(), rename_text_.size() - 1);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##node_id", rename_text_.data(), rename_text_.size(), ImGuiInputTextFlags_CharsNoBlank);
    set_wrapped_item_tooltip("The node's id: links and the generated code name it by this.");
    if(ImGui::IsItemDeactivatedAfterEdit() && node.id != rename_text_.data())
    {
        begin_edit();
        const std::string renamed = rename_material_graph_node(graph_, node.id, rename_text_.data());
        select_node(renamed);
        renaming_node_.clear();
        apply_edit(true);
    }
}

void material_graph_panel::draw_setting(material_graph_node& node, const material_setting_def& setting)
{
    const auto* stored = find_material_graph_setting(node, setting.name);
    std::string value = stored ? *stored : setting.default_value;
    ImGui::PushID(setting.name.c_str());
    ImGui::TextUnformatted(setting.label.c_str());
    if(!setting.tooltip.empty())
    {
        set_wrapped_item_tooltip("%s", setting.tooltip.c_str());
    }
    if(setting.kind == material_setting_kind::code)
    {
        draw_code_setting(node, setting, value);
        ImGui::PopID();
        return;
    }
    const auto* def = find_material_node_def(node.type);
    if(def != nullptr && def->code == material_node_code::function_call && setting.name == MATERIAL_FUNCTION_KEY_SETTING)
    {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);
        draw_function_setting(node, setting, value);
        ImGui::PopID();
        return;
    }
    if(def != nullptr && def->code == material_node_code::named_reroute && setting.name == REROUTE_NAME_SETTING)
    {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        draw_reroute_name_setting(node, setting, value);
        ImGui::PopID();
        return;
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool edited = false;
    switch(setting.kind)
    {
        case material_setting_kind::choice:
            if(ImGui::BeginCombo("##choice", get_material_setting_choice_label(setting, value).c_str()))
            {
                for(const auto& choice : setting.choices)
                {
                    if(ImGui::Selectable(get_material_setting_choice_label(setting, choice).c_str(), choice == value))
                    {
                        value = choice;
                        edited = true;
                    }
                }
                ImGui::EndCombo();
            }
            if(edited)
            {
                set_material_graph_setting(node, setting.name, value);
                commit_edit();
            }
            break;
        case material_setting_kind::number:
        {
            float number = std::strtof(value.c_str(), nullptr);
            if(ImGui::DragFloat("##number", &number, SETTING_DRAG_SPEED, 0.0f, 0.0f, "%.3g"))
            {
                set_material_graph_setting(node, setting.name, fmt::format("{}", number));
            }
            track_item_edit();
            break;
        }
        case material_setting_kind::text:
        case material_setting_kind::swizzle:
            if(input_text("##text", value, setting.kind == material_setting_kind::swizzle ? ImGuiInputTextFlags_CharsNoBlank : 0, node.id.c_str()))
            {
                set_material_graph_setting(node, setting.name, value);
            }
            track_item_edit();
            break;
        case material_setting_kind::code:
            // Drawn under its label by draw_code_setting, above.
            break;
    }
    ImGui::PopID();
}

void material_graph_panel::draw_code_setting(material_graph_node& node, const material_setting_def& setting, const std::string& value)
{
    // A buffer with room to type, the code's width under its label.
    std::vector<char> buffer(std::max(MIN_CODE_BUFFER_SIZE, value.size() + CODE_BUFFER_ROOM), '\0');
    std::copy(value.begin(), value.end(), buffer.begin());
    ImGui::PushFont(ImGui::Font::Mono);
    const ImVec2 size(-FLT_MIN, ImGui::GetTextLineHeight() * CODE_EDITOR_LINES);
    if(ImGui::InputTextMultiline("##code", buffer.data(), buffer.size(), size, ImGuiInputTextFlags_AllowTabInput))
    {
        set_material_graph_setting(node, setting.name, buffer.data());
    }
    ImGui::PopFont();
    track_item_edit();
}

void material_graph_panel::draw_function_setting(material_graph_node& node, const material_setting_def& setting, const std::string& value)
{
    const float open_width = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - open_width - ImGui::GetStyle().ItemSpacing.x);
    const std::string shown = value.empty() ? std::string("Pick a material function") : fs::path(value).stem().string();
    if(ImGui::BeginCombo("##function", shown.c_str()))
    {
        // Listed while the combo is open: every graph file of the project that is a function.
        bool has_any = false;
        fs::error_code ec;
        for(const auto& entry : fs::recursive_directory_iterator(fs::resolve_protocol(PROJECT_DATA_FOLDER), ec))
        {
            if(entry.path().extension() != GRAPH_EXTENSION)
            {
                continue;
            }
            const auto key = fs::convert_to_protocol(entry.path()).generic_string();
            const auto function = load_material_function(key);
            if(function == nullptr || !is_material_function_graph(*function))
            {
                continue;
            }
            has_any = true;
            if(ImGui::Selectable(key.c_str(), key == value))
            {
                begin_edit();
                set_material_graph_setting(node, setting.name, key);
                apply_edit(true);
            }
        }
        if(!has_any)
        {
            ImGui::TextDisabled("The project has no material function: New > Empty Material Function makes one.");
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(value.empty());
    if(ImGui::Button(ICON_MDI_OPEN_IN_NEW))
    {
        // After this frame: the graph the node belongs to is replaced.
        pending_open_ = fs::resolve_protocol(value).string();
    }
    ImGui::EndDisabled();
    set_wrapped_item_tooltip("Open the function in this window.");
}

void material_graph_panel::draw_reroute_name_setting(material_graph_node& node, const material_setting_def& setting, const std::string& value)
{
    if(!ImGui::BeginCombo("##reroute", value.empty() ? "Pick a Named Reroute" : value.c_str()))
    {
        return;
    }
    bool has_any = false;
    for(const auto& candidate : graph_.nodes)
    {
        const auto* candidate_def = find_material_node_def(candidate.type);
        if(candidate_def == nullptr || candidate_def->code != material_node_code::named_reroute_declaration)
        {
            continue;
        }
        has_any = true;
        const auto* name = find_material_graph_setting(candidate, REROUTE_NAME_SETTING);
        const std::string declared = name != nullptr && !name->empty() ? *name : candidate.id;
        if(ImGui::Selectable(declared.c_str(), declared == value))
        {
            begin_edit();
            set_material_graph_setting(node, setting.name, declared);
            apply_edit(true);
        }
    }
    if(!has_any)
    {
        ImGui::TextDisabled("The graph has no Named Reroute yet.");
    }
    ImGui::EndCombo();
}

void material_graph_panel::draw_texture_field(rtti::context& ctx, size_t index)
{
    ImGui::SeparatorText("Texture");
    const auto* linked = find_material_graph_input(graph_.nodes[index], MATERIAL_TEXTURE_PIN);
    if(linked != nullptr && !linked->link.empty())
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("A texture is linked into Texture: the node reads that one, not its own below.");
        ImGui::PopTextWrapPos();
    }
    // The window keeps the graph's history: the field's edits are its undo steps, not the scene's.
    inspect_without_undo_scope no_undo;
    entt::meta_any texture = entt::forward_as_meta(graph_.nodes[index].texture);
    var_info info;
    info.is_property = true;
    const auto result = inspect_var(ctx, texture, make_proxy(texture, "texture"), info);
    if(result.changed)
    {
        begin_edit();
        apply_edit(result.edit_finished);
    }
    else if(result.edit_finished && edit_open_)
    {
        apply_edit(true, false);
    }
}

namespace
{
/// The stops of a Color Ramp node in the order of their positions: index, position and colour.
struct ramp_stop
{
    uint32_t index{};
    float position{};
    math::vec4 color{};
};

auto read_ramp_stops(const material_graph_node& node, const material_node_def& def, uint32_t count) -> std::vector<ramp_stop>
{
    std::vector<ramp_stop> stops;
    for(uint32_t k = 0; k < count; ++k)
    {
        const auto color_pin = get_material_color_ramp_color_pin(k);
        const auto position_pin = get_material_color_ramp_position_pin(k);
        const auto* color = find_material_graph_input(node, color_pin);
        const auto* position = find_material_graph_input(node, position_pin);
        const auto* color_def = find_material_pin_def(def.inputs, color_pin);
        const auto* position_def = find_material_pin_def(def.inputs, position_pin);
        stops.push_back({k,
                         position != nullptr ? position->value.x : (position_def != nullptr ? position_def->default_value.x : 0.0f),
                         color != nullptr ? color->value : (color_def != nullptr ? color_def->default_value : math::vec4(1.0f))});
    }
    std::stable_sort(stops.begin(),
                     stops.end(),
                     [](const ramp_stop& a, const ramp_stop& b)
                     {
                         return a.position < b.position;
                     });
    return stops;
}

/// A linear colour as the bar shows it: sRGB-encoded, opaque.
auto to_bar_color(const math::vec4& color) -> ImU32
{
    const auto encode = [](float c)
    {
        const float clamped = std::clamp(c, 0.0f, 1.0f);
        return clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
    };
    return ImGui::ColorConvertFloat4ToU32(ImVec4(encode(color.x), encode(color.y), encode(color.z), 1.0f));
}
} // namespace

void material_graph_panel::draw_color_ramp(size_t index)
{
    auto& node = graph_.nodes[index];
    const auto& def = *find_material_node_def(node.type);
    const auto* count_setting = find_material_graph_setting(node, MATERIAL_COLOR_RAMP_COUNT_SETTING);
    const auto count = uint32_t(std::clamp(count_setting != nullptr ? std::atoi(count_setting->c_str()) : 2, 2, int(MATERIAL_COLOR_RAMP_MAX_STOPS)));
    const auto* interpolation = find_material_graph_setting(node, MATERIAL_COLOR_RAMP_INTERPOLATION_SETTING);
    const bool is_constant = interpolation != nullptr && *interpolation == "constant";
    ImGui::SeparatorText("Ramp");
    // The bar: each span between two stops, blended as the ramp blends it (Smooth draws as Linear).
    const auto stops = read_ramp_stops(node, def, count);
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw_list = ImGui::GetWindowDrawList();
    const auto x_at = [&](float position)
    {
        return origin.x + std::clamp(position, 0.0f, 1.0f) * width;
    };
    draw_list->AddRectFilled(origin, ImVec2(x_at(stops.front().position), origin.y + height), to_bar_color(stops.front().color));
    for(size_t k = 1; k < stops.size(); ++k)
    {
        const ImU32 left = to_bar_color(stops[k - 1].color);
        const ImU32 right = is_constant ? left : to_bar_color(stops[k].color);
        draw_list->AddRectFilledMultiColor(ImVec2(x_at(stops[k - 1].position), origin.y),
                                           ImVec2(x_at(stops[k].position), origin.y + height),
                                           left,
                                           right,
                                           right,
                                           left);
    }
    draw_list->AddRectFilled(ImVec2(x_at(stops.back().position), origin.y), ImVec2(origin.x + width, origin.y + height), to_bar_color(stops.back().color));
    for(const auto& stop : stops)
    {
        const float x = x_at(stop.position);
        draw_list->AddTriangleFilled(ImVec2(x, origin.y + height - 6.0f), ImVec2(x - 5.0f, origin.y + height + 2.0f), ImVec2(x + 5.0f, origin.y + height + 2.0f), IM_COL32_WHITE);
    }
    ImGui::Dummy(ImVec2(width, height + 4.0f));
    // A row per stop, in the order of their positions: its position and its colour.
    ImGui::PushID("ramp");
    for(const auto& stop : stops)
    {
        ImGui::PushID(int(stop.index));
        for(const auto& pin_name : {get_material_color_ramp_position_pin(stop.index), get_material_color_ramp_color_pin(stop.index)})
        {
            const auto* pin_def = find_material_pin_def(def.inputs, pin_name);
            if(pin_def == nullptr)
            {
                continue;
            }
            ImGui::PushID(pin_name.c_str());
            const auto* input = find_material_graph_input(node, pin_name);
            math::vec4 value = input != nullptr ? input->value : pin_def->default_value;
            const uint32_t components = pin_def->is_color ? 4 : 1;
            if(!pin_def->is_color)
            {
                ImGui::SetNextItemWidth(width * 0.3f);
            }
            else
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
            }
            auto style = make_node_value_style(node, *pin_def, components);
            if(!pin_def->is_color)
            {
                style.min_value = 0.0f;
                style.max_value = 1.0f;
            }
            const auto edit = inspect_vector_value(engine::context(), value, style);
            if(edit.changed)
            {
                get_or_add_material_graph_input(node, pin_name, pin_def->default_value).value = value;
            }
            track_value_edit(edit.changed, edit.edit_finished);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::BeginDisabled(count >= MATERIAL_COLOR_RAMP_MAX_STOPS);
    if(ImGui::SmallButton(ICON_MDI_PLUS " Stop"))
    {
        begin_edit();
        // The new stop halves the widest gap between two stops, in the blend of their colours.
        size_t widest = 1;
        for(size_t k = 1; k < stops.size(); ++k)
        {
            if(stops[k].position - stops[k - 1].position > stops[widest].position - stops[widest - 1].position)
            {
                widest = k;
            }
        }
        const auto& a = stops[widest - 1];
        const auto& b = stops[widest];
        get_or_add_material_graph_input(node, get_material_color_ramp_position_pin(count), {}).value = math::vec4((a.position + b.position) * 0.5f);
        get_or_add_material_graph_input(node, get_material_color_ramp_color_pin(count), {}).value = (a.color + b.color) * 0.5f;
        set_material_graph_setting(node, MATERIAL_COLOR_RAMP_COUNT_SETTING, std::to_string(count + 1));
        apply_edit(true);
    }
    ImGui::EndDisabled();
    set_wrapped_item_tooltip("Add a stop halfway along the widest gap between two stops.");
    ImGui::SameLine();
    ImGui::BeginDisabled(count <= 2);
    if(ImGui::SmallButton(ICON_MDI_MINUS " Stop"))
    {
        begin_edit();
        set_material_graph_setting(node, MATERIAL_COLOR_RAMP_COUNT_SETTING, std::to_string(count - 1));
        apply_edit(true);
    }
    ImGui::EndDisabled();
    set_wrapped_item_tooltip("Remove the last stop added.");
}

void material_graph_panel::draw_split_toggle(size_t index, const material_pin_def& pin, bool is_output)
{
    if(!can_split_material_pin(pin))
    {
        return;
    }
    auto& node = graph_.nodes[index];
    const bool split = is_output ? is_material_graph_output_split(node, pin.name) : is_material_graph_input_split(node, pin.name);
    ImGui::SameLine();
    if(ImGui::SmallButton(split ? ICON_MDI_CALL_MERGE : ICON_MDI_CALL_SPLIT))
    {
        begin_edit();
        set_material_graph_pin_split(node, pin.name, is_output, !split);
        apply_edit(true);
    }
    set_wrapped_item_tooltip(split ? "Join the components into one pin again." : "Split into a pin per component (double-click the pin too).");
}

void material_graph_panel::draw_inputs(size_t index)
{
    const auto& def = *find_material_node_def(graph_.nodes[index].type);
    // The pins' ids are their index among the inputs: the outputs' share the numbers.
    ImGui::PushID("inputs");
    for(size_t pin = 0; pin < def.inputs.size(); ++pin)
    {
        const auto shown = get_material_pin_presentation(graph_.nodes[index], def, false, pin);
        if(!shown.is_shown)
        {
            continue;
        }
        ImGui::PushID(int(pin));
        const auto& pin_def = def.inputs[pin];
        const auto type = get_resolved_input_type(compiled_, index, pin_def, pin);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(get_material_type_color(type)));
        ImGui::TextUnformatted(pin_def.is_value_only ? ICON_MDI_NUMERIC : ICON_MDI_CIRCLE_SMALL);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(shown.label.empty() ? "Input" : shown.label.c_str());
        set_wrapped_item_tooltip("%s%s%s", get_material_value_type_name(type), pin_def.tooltip.empty() ? "" : "\n", pin_def.tooltip.c_str());
        draw_split_toggle(index, pin_def, false);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);
        if(can_split_material_pin(pin_def) && is_material_graph_input_split(graph_.nodes[index], pin_def.name))
        {
            draw_input_components(index, pin);
        }
        else
        {
            draw_input_value(index, pin);
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

void material_graph_panel::draw_input_value(size_t index, size_t pin)
{
    auto& node = graph_.nodes[index];
    const auto& pin_def = find_material_node_def(node.type)->inputs[pin];
    auto* input = find_material_graph_input(node, pin_def.name);
    if(input && !input->link.empty())
    {
        ImGui::TextDisabled(ICON_MDI_ARROW_LEFT " %s", input->link.c_str());
        ImGui::SameLine();
        if(ImGui::SmallButton(ICON_MDI_LINK_VARIANT_OFF))
        {
            input->link.clear();
            commit_edit();
        }
        set_wrapped_item_tooltip("Break the link.");
        return;
    }
    if(!is_material_value_numeric(pin_def.type))
    {
        ImGui::TextDisabled(is_material_texture_type(pin_def.type) ? "Unlinked: the node's own texture." : "Unlinked: the default surface.");
        return;
    }
    if(!pin_def.default_code.empty())
    {
        ImGui::TextDisabled("Reads the mesh's own while unlinked.");
        return;
    }
    math::vec4 value = input ? input->value : pin_def.default_value;
    const auto type = get_resolved_input_type(compiled_, index, pin_def, pin);
    const uint32_t width = pin_def.type == material_value_type::dynamic ? 1 : get_material_value_width(type);
    ImGui::PushItemWidth(-FLT_MIN);
    const auto edit = inspect_vector_value(engine::context(), value, make_node_value_style(node, pin_def, width));
    ImGui::PopItemWidth();
    if(edit.changed)
    {
        get_or_add_material_graph_input(node, pin_def.name, pin_def.default_value).value = value;
    }
    track_value_edit(edit.changed, edit.edit_finished);
}

void material_graph_panel::draw_input_components(size_t index, size_t pin)
{
    auto& node = graph_.nodes[index];
    const auto& pin_def = find_material_node_def(node.type)->inputs[pin];
    const float column = ImGui::GetCursorPosX();
    ImGui::BeginGroup();
    for(int component = 0; component < int(get_material_value_width(pin_def.type)); ++component)
    {
        ImGui::PushID(component);
        const std::string entry = make_material_graph_component_pin(pin_def.name, component, get_material_component_letters(pin_def));
        const char letter = char(std::toupper(static_cast<unsigned char>(entry.back())));
        ImGui::SetCursorPosX(column);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%c", letter);
        ImGui::SameLine();
        auto* input = find_material_graph_input(node, entry);
        if(input && !input->link.empty())
        {
            ImGui::TextDisabled(ICON_MDI_ARROW_LEFT " %s", input->link.c_str());
            ImGui::SameLine();
            if(ImGui::SmallButton(ICON_MDI_LINK_VARIANT_OFF))
            {
                input->link.clear();
                commit_edit();
            }
            set_wrapped_item_tooltip("Break the link.");
        }
        else
        {
            // An unlinked component reads its part of the pin's own value.
            const auto* own = find_material_graph_input(node, pin_def.name);
            math::vec4 value = own ? own->value : pin_def.default_value;
            math::vec4 shown(value[component]);
            ImGui::PushItemWidth(-FLT_MIN);
            const auto edit = inspect_vector_value(engine::context(), shown, make_node_value_style(node, pin_def, 1));
            ImGui::PopItemWidth();
            if(edit.changed)
            {
                value[component] = shown.x;
                get_or_add_material_graph_input(node, pin_def.name, pin_def.default_value).value = value;
            }
            track_value_edit(edit.changed, edit.edit_finished);
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
}

void material_graph_panel::draw_outputs(size_t index)
{
    const auto& def = *find_material_node_def(graph_.nodes[index].type);
    ImGui::PushID("outputs");
    for(size_t pin = 0; pin < def.outputs.size(); ++pin)
    {
        const auto& output = def.outputs[pin];
        const auto shown = get_material_pin_presentation(graph_.nodes[index], def, true, pin);
        if(!shown.is_shown)
        {
            continue;
        }
        auto type = output.type;
        if(index < compiled_.nodes.size() && pin < compiled_.nodes[index].output_types.size())
        {
            type = compiled_.nodes[index].output_types[pin];
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(get_material_type_color(type)));
        ImGui::TextUnformatted(ICON_MDI_CIRCLE_SMALL);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("%s", shown.label.empty() ? "Output" : shown.label.c_str());
        ImGui::PushID(int(pin));
        draw_split_toggle(index, output, true);
        ImGui::PopID();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);
        ImGui::TextDisabled("%s   %s.%s", get_material_value_type_name(type), graph_.nodes[index].id.c_str(), output.name.c_str());
    }
    ImGui::PopID();
}

void material_graph_panel::draw_diagnostics(const std::vector<const material_graph_diagnostic*>& diagnostics)
{
    ImGui::PushTextWrapPos(0.0f);
    for(const auto* diagnostic : diagnostics)
    {
        const char* icon = get_severity_icon(diagnostic->severity);
        ImGui::TextColored(get_severity_color(diagnostic->severity),
                           "%s %s%s",
                           icon,
                           diagnostic->pin.empty() ? "" : (diagnostic->pin + ": ").c_str(),
                           diagnostic->message.c_str());
    }
    ImGui::PopTextWrapPos();
}

void material_graph_panel::draw_comment_details()
{
    auto& comment = graph_.comments[size_t(selection_.comment)];
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(ICON_MDI_COMMENT_TEXT_OUTLINE " Comment");
    ImGui::PopFont();
    std::array<char, 1024> buffer{};
    comment.text.copy(buffer.data(), buffer.size() - 1);
    if(ImGui::InputTextMultiline("##comment", buffer.data(), buffer.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6.0f)))
    {
        comment.text = buffer.data();
    }
    if(ImGui::IsItemEdited())
    {
        begin_edit();
        apply_edit(false, false);
    }
    if(ImGui::IsItemDeactivatedAfterEdit() && edit_open_)
    {
        apply_edit(true, false);
    }
}

void material_graph_panel::draw_graph_summary()
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(ICON_MDI_GRAPH " Graph");
    ImGui::PopFont();
    const size_t used = size_t(std::count_if(compiled_.nodes.begin(),
                                             compiled_.nodes.end(),
                                             [](const material_graph_node_info& info)
                                             {
                                                 return info.is_used;
                                             }));
    ImGui::TextDisabled("%zu nodes, %zu feeding the output. Select a node to edit it.", graph_.nodes.size(), used);
    draw_problems();
    ImGui::SeparatorText("Parameters");
    if(compiled_.parameters.empty())
    {
        ImGui::TextDisabled("None. Parameter nodes add values material instances can change.");
    }
    for(const auto& parameter : compiled_.parameters)
    {
        // A vector takes its whole register, a scalar one component of it.
        std::string location = std::string(MATERIAL_GRAPH_PARAMETERS_UNIFORM) + "[" +
                               std::to_string(parameter.register_index) + "]";
        if(get_material_value_width(parameter.type) == 1)
        {
            location += std::string(".") + "xyzw"[parameter.component];
        }
        ImGui::BulletText("%s  (%s, %s)",
                          parameter.name.c_str(),
                          get_material_value_type_name(parameter.type),
                          location.c_str());
    }
    ImGui::SeparatorText("Textures");
    if(compiled_.textures.empty())
    {
        ImGui::TextDisabled("None.");
    }
    for(const auto& texture : compiled_.textures)
    {
        const std::string key = texture.texture ? texture.texture.id() : std::string("(no texture)");
        ImGui::BulletText("%u: %s  %s", texture.stage, texture.node_id.c_str(), key.c_str());
    }
    if(compiled_.uses_world_position_offset)
    {
        ImGui::SeparatorText("World Position Offset");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s Its shadow and motion follow, and its material instances draw on their own.",
                            compiled_.is_world_position_offset_animated
                                ? "The vertices move every frame: the offset reads the time or the camera."
                                : "The vertices move by an offset that holds still.");
        if(compiled_.max_world_position_offset > 0.0f)
        {
            ImGui::TextDisabled("The bounds of its meshes grow by %g (Max Offset).", double(compiled_.max_world_position_offset));
        }
        else
        {
            ImGui::TextDisabled("Max Offset is 0: a mesh moved out of its bounds can be culled while on screen.");
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::SeparatorText("Shortcuts");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Space, Tab or right-click: add a node. Hold a key and click: 1-4 constants, M multiply, A add, "
                        "D divide, L lerp, O one minus, E power, T texture, N normal map, U UV, P panner, S scalar "
                        "parameter, V vector parameter. Drag from a pin into empty space to add a linked node. Alt+click "
                        "a pin breaks its links, double-click a link adds a reroute. C comments the selection, F frames it. "
                        "Ctrl+C copies nodes as JSON; Ctrl+V pastes graph JSON from anywhere.");
    ImGui::PopTextWrapPos();
}

void material_graph_panel::draw_problems()
{
    if(compiled_.diagnostics.empty())
    {
        return;
    }
    const bool has_problem = std::any_of(compiled_.diagnostics.begin(),
                                         compiled_.diagnostics.end(),
                                         [](const material_graph_diagnostic& diagnostic)
                                         {
                                             return diagnostic.severity != material_graph_severity::info;
                                         });
    ImGui::SeparatorText(has_problem ? "Problems" : "Notes");
    for(size_t i = 0; i < compiled_.diagnostics.size(); ++i)
    {
        const auto& diagnostic = compiled_.diagnostics[i];
        ImGui::PushID(int(i));
        ImGui::PushStyleColor(ImGuiCol_Text, get_severity_color(diagnostic.severity));
        const std::string text = (diagnostic.node_id.empty() ? std::string{} : diagnostic.node_id + ": ") + diagnostic.message;
        if(ImGui::Selectable(text.c_str()) && !diagnostic.node_id.empty())
        {
            select_node(diagnostic.node_id);
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

auto material_graph_panel::get_selected_lines() const -> std::pair<uint32_t, uint32_t>
{
    uint32_t first = 0;
    uint32_t last = 0;
    for(const auto& id : selection_.nodes)
    {
        const int index = find_material_graph_node(graph_, id);
        if(index < 0 || size_t(index) >= compiled_.nodes.size() || compiled_.nodes[size_t(index)].first_line == 0)
        {
            continue;
        }
        const auto& info = compiled_.nodes[size_t(index)];
        first = first == 0 ? info.first_line : std::min(first, info.first_line);
        last = std::max(last, info.first_line + info.line_count - 1);
    }
    return {first, first == 0 ? 0 : last - first + 1};
}

void material_graph_panel::draw_code()
{
    if(ImGui::RadioButton("Shader", !show_json_))
    {
        show_json_ = false;
    }
    ImGui::SameLine();
    if(ImGui::RadioButton("Graph JSON", show_json_))
    {
        show_json_ = true;
    }
    if(show_json_ && json_revision_ != text_revision_)
    {
        json_ = save_to_string(graph_);
        json_revision_ = text_revision_;
    }
    const std::string& text = show_json_ ? json_ : compiled_.shader;
    ImGui::SameLine();
    if(ImGui::SmallButton(ICON_MDI_CONTENT_COPY " Copy"))
    {
        ImGui::SetClipboardText(text.c_str());
    }
    set_wrapped_item_tooltip(show_json_ ? "Copy the graph as JSON: paste it into another graph, a file, or a message."
                                       : "Copy the generated .eshader.");
    std::vector<std::pair<size_t, size_t>> lines;
    for(size_t start = 0; start < text.size();)
    {
        const size_t end = text.find('\n', start);
        lines.emplace_back(start, (end == std::string::npos ? text.size() : end) - start);
        start = end == std::string::npos ? text.size() : end + 1;
    }
    const auto [highlight_first, highlight_count] = show_json_ ? std::pair<uint32_t, uint32_t>{0, 0} : get_selected_lines();
    ImGui::BeginChild("##code", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(ImGui::Font::Mono);
    const float line_height = ImGui::GetTextLineHeight();
    // The code follows the selection: a newly selected node's lines scroll into view.
    if(highlight_first > 0 && code_scrolled_selection_ != selection_)
    {
        ImGui::SetScrollY(std::max(0.0f, (float(highlight_first) - 4.0f) * line_height));
    }
    code_scrolled_selection_ = selection_;
    ImGuiListClipper clipper;
    clipper.Begin(int(lines.size()), line_height);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    while(clipper.Step())
    {
        for(int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
        {
            const uint32_t number = uint32_t(line + 1);
            const ImVec2 position = ImGui::GetCursorScreenPos();
            if(number >= highlight_first && number < highlight_first + highlight_count)
            {
                draw_list->AddRectFilled(position, ImVec2(position.x + ImGui::GetContentRegionAvail().x + ImGui::GetScrollMaxX() + 1000.0f, position.y + line_height), COL_CODE_HIGHLIGHT);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(COL_LINE_NUMBER));
            ImGui::Text("%4u ", number);
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 0.0f);
            const auto [start, length] = lines[size_t(line)];
            ImGui::TextUnformatted(text.data() + start, text.data() + start + length);
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
}

} // namespace unravel
