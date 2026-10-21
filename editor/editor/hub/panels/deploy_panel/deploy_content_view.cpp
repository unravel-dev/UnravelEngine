#include "deploy_content_view.h"
#include "deploy_panel_widgets.h"

#include <deploy/keys.h>
#include <deploy/profile.h>
#include <editor/assets/asset_icons.h>
#include <editor/deploy/deploy_preflight.h>
#include <editor/editing/editor_actions.h>
#include <editor/format/format_bytes.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector_asset_preview.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/ecs/prefab.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <algorithm>
#include <array>

namespace unravel
{
namespace
{
namespace widgets = deploy_widgets;

// Sizes in font units.
constexpr float TREE_MIN_HEIGHT = 10.0f;
constexpr float TREE_INDENT = 1.2f;
constexpr float STATUS_COLUMN_WIDTH = 17.0f;
constexpr float SIZE_COLUMN_WIDTH = 5.5f;
constexpr float SCENE_PATH_COLUMN_SHARE = 1.4f;
constexpr float FILTER_WIDTH = 9.0f;
constexpr float ARROW_SCALE = 0.7f;
constexpr const char* SCENE_STARTUP_LABEL = "Startup";
constexpr const char* SCENE_PICKER_ID = "Add Scene to Build";
constexpr const char* RESET_LABEL = ICON_MDI_RESTORE " Reset";
/// Scene rows shown before the scene list scrolls.
constexpr int SCENE_ROWS_SHOWN = 4;
/// Folders scripts build paths in, and rules that name nothing, listed before "and N more".
constexpr std::size_t NOTED_KEYS_MAX = 3;
/// Steps of a "used by" chain shown in a tooltip.
constexpr std::size_t USE_CHAIN_MAX = 12;
/// A detection younger than this reads "just now".
constexpr long long RECENT_DETECTION_SECONDS = 5;
/// Rows that do not ship are drawn at this alpha, and so are the tree's guide lines.
constexpr float LEFT_OUT_ALPHA = 0.45f;
constexpr float GUIDE_ALPHA = 0.25f;
constexpr ImU32 FOLDER_ICON_COLOR = IM_COL32(214, 178, 104, 255);
constexpr ImU32 ADDED_COLOR = IM_COL32(110, 200, 130, 255);

/**
 * @brief The project's scenes that are not in the build yet - neither the startup scene nor listed - and
 * whose path passes `search` (null: every one), by path.
 */
auto find_scenes_outside_build(rtti::context& ctx,
                               const deploy_settings& params,
                               const std::string& startup_scene,
                               const ImGuiTextFilter* search) -> std::vector<asset_handle<scene_prefab>>
{
    const std::string project_root = std::string(deployment::CONTENT_PROTOCOL) + ":/";
    auto scenes = ctx.get_cached<asset_manager>().get_assets_with_predicate<scene_prefab>(
        [&](const asset_handle<scene_prefab>& scene)
        {
            const auto& key = scene.id();
            const bool is_listed = std::any_of(params.deploy_scenes.begin(),
                                               params.deploy_scenes.end(),
                                               [&key](const std::string& listed)
                                               {
                                                   return deployment::is_same_key(listed, key);
                                               });
            const bool passes_search = search == nullptr || search->PassFilter(deployment::get_display_path(key).c_str());
            return deployment::is_key_under(key, project_root) && !is_listed && !deployment::is_same_key(key, startup_scene) &&
                   passes_search;
        });
    // The storage puts the empty handle first without asking the predicate.
    scenes.erase(std::remove_if(scenes.begin(),
                                scenes.end(),
                                [](const asset_handle<scene_prefab>& scene)
                                {
                                    return !scene;
                                }),
                 scenes.end());
    std::sort(scenes.begin(),
              scenes.end(),
              [](const asset_handle<scene_prefab>& lhs, const asset_handle<scene_prefab>& rhs)
              {
                  return lhs.id() < rhs.id();
              });
    return scenes;
}

auto get_scene_keys(const std::vector<asset_handle<scene_prefab>>& scenes) -> std::vector<std::string>
{
    std::vector<std::string> keys;
    std::transform(scenes.begin(),
                   scenes.end(),
                   std::back_inserter(keys),
                   [](const asset_handle<scene_prefab>& scene)
                   {
                       return scene.id();
                   });
    return keys;
}

auto with_alpha(ImU32 color, float alpha) -> ImU32
{
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(value);
}

/// What the content is detected from: when it changes, the content is stale.
/// What the walk depends on besides the assets: the project, the shipped settings (their startup scene and
/// handles) and the scenes the deploy names.
auto make_source_signature(const deploy_settings& params, const std::string& settings_document) -> std::string
{
    std::string signature = deployment::get_project_root().generic_string() + "|" + settings_document;
    for(const auto& scene : params.deploy_scenes)
    {
        signature += "|" + scene;
    }
    return signature;
}

/// A size column cell: right aligned, muted.
void draw_size_text(std::uint64_t size, bool is_dimmed)
{
    const std::string text = format_bytes(size, 1);
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x));
    ImGui::PushStyleColor(ImGuiCol_Text, with_alpha(widgets::get_muted_color(), is_dimmed ? LEFT_OUT_ALPHA : 1.0f));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

/// An icon (none when null) and a short text in a table cell, centered on a frame-high row; the text ends
/// in an ellipsis where the cell does. The cell is one item, for a tooltip.
void draw_cell_label(const char* icon, ImU32 icon_color, const std::string& text, ImU32 text_color)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float row_height = ImGui::GetFrameHeight();
    const float icon_width = icon == nullptr ? 0.0f : ImGui::GetFontSize() + widgets::to_pixels(widgets::ICON_GAP);
    auto* draw_list = ImGui::GetWindowDrawList();
    if(icon != nullptr)
    {
        ImGui::RenderIconCentered(draw_list, ImVec2(min.x + ImGui::GetFontSize() * 0.5f, min.y + row_height * 0.5f), icon, icon_color);
    }
    const float max_x = min.x + ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(min.x + icon_width, min.y + (row_height - ImGui::GetFontSize()) * 0.5f),
                              ImVec2(max_x, min.y + row_height),
                              max_x,
                              text.c_str(),
                              nullptr,
                              nullptr);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, row_height));
}

/// The vertical guide lines of a row's ancestors, as a tree draws them.
void draw_tree_guides(const ImVec2& cell_min, std::size_t depth, float row_height)
{
    auto* draw_list = ImGui::GetWindowDrawList();
    const float indent = widgets::to_pixels(TREE_INDENT);
    const ImU32 color = with_alpha(ImGui::GetColorU32(ImGuiCol_Text), GUIDE_ALPHA);
    for(std::size_t level = 0; level < depth; ++level)
    {
        const float x = ImFloor(cell_min.x + indent * float(level) + ImGui::GetFontSize() * 0.5f);
        draw_list->AddLine(ImVec2(x, cell_min.y), ImVec2(x, cell_min.y + row_height), color);
    }
}

/// The note on a folder scripts build asset paths in, naming them: "Game.cs builds ...", "A.cs and B.cs
/// build ...", "A.cs and 2 other scripts build ...".
auto make_named_folder_note(const deployment::named_folder& folder) -> std::string
{
    const auto& scripts = folder.scripts;
    std::string who = "A script";
    if(!scripts.empty())
    {
        who = deployment::get_key_name(scripts.front());
    }
    if(scripts.size() == 2)
    {
        who += " and " + deployment::get_key_name(scripts.back());
    }
    else if(scripts.size() > 2)
    {
        who += fmt::format(" and {} other scripts", scripts.size() - 1);
    }
    const bool is_one = scripts.size() <= 1;
    return fmt::format("{} {} asset paths in {} at run time, so what {} from there isn't detected. Ship the folder to be "
                       "safe.",
                       who,
                       is_one ? "builds" : "build",
                       deployment::get_display_path(folder.key),
                       is_one ? "it loads" : "they load");
}

void draw_tooltip_text(const std::string& text)
{
    if(ImGui::BeginItemTooltip())
    {
        ImGui::TextUnformatted(text.c_str());
        ImGui::EndTooltip();
    }
}
} // namespace

void deploy_content_view::invalidate()
{
    is_invalidated_ = true;
}

auto deploy_content_view::get_shipped_settings(rtti::context& ctx) const -> settings
{
    return ctx.get_cached<project_manager>().get_profile_settings(editor_->get_profile());
}

auto deploy_content_view::get_startup_scene(rtti::context& ctx) const -> std::string
{
    return get_shipped_settings(ctx).standalone.startup_scene.id();
}

void deploy_content_view::draw(rtti::context& ctx, deploy_settings_editor& editor)
{
    editor_ = &editor;
    update_content(ctx);
    view_.search = search_.InputBuf;
    if(content_ != nullptr)
    {
        tree_.update(editor_->get(), view_);
    }
    draw_scenes(ctx);
    widgets::draw_gap(widgets::SECTION_GAP);
    draw_assets_header(ctx);
    draw_tree(ctx);
}

/// Takes a finished detection, drops content of another project, and starts a detection when the
/// content is stale and none is running.
void deploy_content_view::update_content(rtti::context& ctx)
{
    poll_detection();
    const auto root = deployment::get_project_root().generic_string();
    if(content_ != nullptr && content_root_ != root)
    {
        content_.reset();
        tree_ = {};
    }
    const auto signature =
        make_source_signature(editor_->get(), serialization::save_to_document(get_shipped_settings(ctx), SETTINGS_DOCUMENT_ROOT));
    const bool is_stale = is_invalidated_ || signature != content_signature_;
    if(is_stale && !detect_job_.valid())
    {
        start_detection(ctx, signature);
    }
}

void deploy_content_view::start_detection(rtti::context& ctx, const std::string& signature)
{
    is_invalidated_ = false;
    const auto shipped_settings = get_shipped_settings(ctx);
    auto source = deployment::make_project_content_source(ctx.get_cached<asset_manager>(), shipped_settings);
    const auto params = editor_->get();
    const auto startup_scene = shipped_settings.standalone.startup_scene.id();
    detect_signature_ = signature;
    detect_job_ = ctx.get_cached<threader>()
                      .pool
                      ->schedule("Detecting Deploy Content",
                                 [source = std::move(source), params, startup_scene]()
                                 {
                                     return deployment::detect_content(source, params, startup_scene);
                                 })
                      .share();
}

void deploy_content_view::poll_detection()
{
    if(!detect_job_.valid() || !detect_job_.is_ready())
    {
        return;
    }
    try
    {
        content_ = std::make_unique<deployment::content>(detect_job_.get());
        content_signature_ = detect_signature_;
        content_root_ = detect_signature_.substr(0, detect_signature_.find('|'));
        detected_at_ = std::chrono::steady_clock::now();
        tree_.build(*content_);
    }
    catch(const std::exception& ex)
    {
        APPLOG_ERROR("Finding what the game uses failed: {}", ex.what());
    }
    detect_job_ = {};
}

void deploy_content_view::draw_scenes(rtti::context& ctx)
{
    auto& params = editor_->get();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted("Scenes in Build");
    ImGui::PopFont();
    ImGui::SameLine();
    draw_scene_buttons(ctx);
    widgets::draw_muted_text("What these scenes use ships with the game. Add the scenes your scripts load.");
    widgets::draw_gap(widgets::LINE_GAP);
    const float row_height = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    const int row_count = static_cast<int>(params.deploy_scenes.size()) + 1;
    const float height = row_height * static_cast<float>(std::min(row_count, SCENE_ROWS_SHOWN)) + ImGui::GetStyle().CellPadding.y;
    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX;
    if(!ImGui::BeginTable("##deploy_scenes", 3, flags, ImVec2(0.0f, height)))
    {
        return;
    }
    const float startup_width = ImGui::GetFontSize() + widgets::to_pixels(widgets::ICON_GAP) + ImGui::CalcTextSize(SCENE_STARTUP_LABEL).x;
    ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, SCENE_PATH_COLUMN_SHARE);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, ImMax(startup_width, ImGui::GetFrameHeight()));
    std::string removed;
    const auto startup_scene = get_startup_scene(ctx);
    draw_scene_row(startup_scene, true, removed);
    for(const auto& scene : params.deploy_scenes)
    {
        if(!deployment::is_same_key(scene, startup_scene))
        {
            draw_scene_row(scene, false, removed);
        }
    }
    ImGui::EndTable();
    if(!removed.empty())
    {
        params.deploy_scenes.erase(std::remove(params.deploy_scenes.begin(), params.deploy_scenes.end(), removed),
                                   params.deploy_scenes.end());
        editor_->commit(ctx);
    }
}

void deploy_content_view::draw_scene_row(const std::string& key, bool is_startup, std::string& removed)
{
    const bool is_missing = !is_startup && content_ != nullptr &&
                            std::any_of(content_->missing_scenes.begin(),
                                        content_->missing_scenes.end(),
                                        [&key](const std::string& missing)
                                        {
                                            return deployment::is_same_key(missing, key);
                                        });
    ImGui::PushID(key.c_str());
    ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight());
    ImGui::TableNextColumn();
    const std::string name = key.empty() ? std::string("No startup scene") : fs::path(deployment::get_key_name(key)).stem().string();
    draw_cell_label(is_missing ? ICON_MDI_ALERT_OUTLINE : ICON_MDI_MAP_OUTLINE,
                    is_missing ? widgets::WARNING_COLOR : widgets::get_accent_u32(),
                    name,
                    ImGui::GetColorU32(ImGuiCol_Text));
    if(is_missing)
    {
        draw_tooltip_text("This scene is no longer in the project. Remove it from the list.");
    }
    ImGui::TableNextColumn();
    draw_cell_label(nullptr, 0, deployment::get_display_path(key), widgets::get_muted_color());
    ImGui::TableNextColumn();
    if(is_startup)
    {
        draw_cell_label(ICON_MDI_FLAG_CHECKERED, widgets::get_muted_color(), SCENE_STARTUP_LABEL, widgets::get_muted_color());
        draw_tooltip_text("The scene the game starts with, set in the Build tab.");
        ImGui::PopID();
        return;
    }
    const float side = ImGui::GetFrameHeight();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - side));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, min + ImVec2(side, side));
    ImGui::ItemSize(rect);
    if(container_widgets::draw_icon_button({"##remove_scene", ICON_MDI_DELETE_OUTLINE, "Remove from the build", true}, rect))
    {
        removed = key;
    }
    ImGui::PopID();
}

/// Add All Scenes and Add Scene, at the right end of the section's title; both rest while every scene of
/// the project is in the build.
void deploy_content_view::draw_scene_buttons(rtti::context& ctx)
{
    const auto outside = find_scenes_outside_build(ctx, editor_->get(), get_startup_scene(ctx), nullptr);
    const char* add_all_label = ICON_MDI_PLAYLIST_PLUS " Add All Scenes";
    const char* add_label = ICON_MDI_PLUS " Add Scene";
    const float width = widgets::calc_button_width(add_all_label) + ImGui::GetStyle().ItemSpacing.x + widgets::calc_button_width(add_label);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - width));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, widgets::to_pixels(widgets::BUTTON_ROUNDING));
    ImGui::BeginDisabled(outside.empty());
    if(ImGui::Button(add_all_label, ImVec2(widgets::calc_button_width(add_all_label), 0.0f)))
    {
        add_scenes(ctx, get_scene_keys(outside));
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::SetTooltip("%s",
                          outside.empty() ? "Every scene of the project is in the build."
                                          : fmt::format("Add the {} scene(s) of the project not in the build yet.", outside.size()).c_str());
    }
    ImGui::SameLine();
    if(ImGui::Button(add_label, ImVec2(widgets::calc_button_width(add_label), 0.0f)))
    {
        scene_search_.Clear();
        ImGui::OpenPopup(SCENE_PICKER_ID);
    }
    ImGui::SetItemTooltipEx("%s", "Pick a scene the game loads from a script, so what it uses ships too.");
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    draw_scene_picker(ctx);
}

/// The editor's asset picker over the scenes not in the build: a click adds the scene and closes it.
void deploy_content_view::draw_scene_picker(rtti::context& ctx)
{
    if(!asset_picker::begin_window(SCENE_PICKER_ID, get_asset_icon<scene_prefab>(), "Add Scene to Build", scene_search_))
    {
        return;
    }
    const auto scenes = find_scenes_outside_build(ctx, editor_->get(), get_startup_scene(ctx), &scene_search_);
    auto& tm = ctx.get_cached<thumbnail_manager>();
    const auto get_tile = [&](std::size_t index) -> asset_picker::tile_content
    {
        asset_picker::tile_content tile{};
        tile.thumbnail = asset_picker::make_preview(tm, scenes[index]);
        tile.name = scenes[index].name();
        return tile;
    };
    const asset_picker::grid_result grid = asset_picker::draw_grid(scenes.size(), get_tile);
    if(grid.picked >= 0)
    {
        add_scenes(ctx, {scenes[std::size_t(grid.picked)].id()});
        ImGui::CloseCurrentPopup();
    }
    std::string footer = fmt::format("{} scene(s) not in the build", scenes.size());
    if(grid.hovered >= 0)
    {
        footer = deployment::get_display_path(scenes[std::size_t(grid.hovered)].id());
    }
    else if(scenes.empty())
    {
        footer = scene_search_.IsActive() ? "No scene outside the build matches the search." : "Every scene of the project is in the build.";
    }
    asset_picker::draw_footer(footer);
    asset_picker::end_window();
}

void deploy_content_view::add_scenes(rtti::context& ctx, const std::vector<std::string>& keys)
{
    auto& scenes = editor_->get().deploy_scenes;
    scenes.insert(scenes.end(), keys.begin(), keys.end());
    editor_->commit(ctx);
}

void deploy_content_view::draw_assets_header(rtti::context& ctx)
{
    widgets::draw_section_title("Assets");
    draw_mode(ctx);
    widgets::draw_gap(widgets::LINE_GAP);
    draw_toolbar(ctx);
    widgets::draw_gap(widgets::LINE_GAP);
    draw_summary();
    draw_notes(ctx);
    widgets::draw_gap(widgets::LINE_GAP);
}

void deploy_content_view::draw_mode(rtti::context& ctx)
{
    auto& params = editor_->get();
    bool is_changed = false;
    if(ImGui::RadioButton("Everything", !params.deploy_only_used_assets))
    {
        params.deploy_only_used_assets = false;
        is_changed = true;
    }
    ImGui::SetItemTooltipEx("%s",
                            "Every project asset ships, except what you leave out below.\n"
                            "Safe: the game finds whatever it loads.");
    ImGui::SameLine(0.0f, widgets::to_pixels(widgets::SECTION_GAP));
    if(ImGui::RadioButton("Used", params.deploy_only_used_assets))
    {
        params.deploy_only_used_assets = true;
        is_changed = true;
    }
    ImGui::SetItemTooltipEx("%s",
                            "Ships what the scenes above use, directly or through other assets, and what\n"
                            "scripts name by path. Check what scripts load by a path built at run time.");
    if(is_changed)
    {
        editor_->commit(ctx);
    }
}

void deploy_content_view::draw_toolbar(rtti::context& ctx)
{
    static constexpr std::array<std::pair<deployment::content_filter, const char*>, 4> filters = {{
        {deployment::content_filter::all, "All assets"},
        {deployment::content_filter::shipped, "Shipping"},
        {deployment::content_filter::left_out, "Left out"},
        {deployment::content_filter::changed, "Changed by you"},
    }};
    const char* detect_label = ICON_MDI_MAGNIFY_SCAN " Detect";
    const float filter_width = widgets::to_pixels(FILTER_WIDTH);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttons_width = widgets::calc_button_width(detect_label) + widgets::calc_button_width(RESET_LABEL);
    const float search_width = ImMax(filter_width, ImGui::GetContentRegionAvail().x - filter_width - buttons_width - 3.0f * spacing);
    ImGui::DrawFilterWithHint(search_, ICON_MDI_MAGNIFY " Search assets...", search_width);
    ImGui::DrawItemActivityOutline();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(filter_width);
    if(ImGui::BeginCombo("##row_filter", filters[static_cast<std::size_t>(view_.filter)].second))
    {
        for(const auto& [filter, label] : filters)
        {
            if(ImGui::Selectable(label, view_.filter == filter))
            {
                view_.filter = filter;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltipEx("%s", "Show only some of the assets.");
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, widgets::to_pixels(widgets::BUTTON_ROUNDING));
    ImGui::BeginDisabled(detect_job_.valid());
    if(ImGui::Button(detect_label, ImVec2(widgets::calc_button_width(detect_label), 0.0f)))
    {
        start_detection(ctx,
                        make_source_signature(editor_->get(),
                                              serialization::save_to_document(get_shipped_settings(ctx), SETTINGS_DOCUMENT_ROOT)));
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::SetItemTooltipEx("%s",
                            "Find again what the scenes and scripts use, after adding or changing assets.\n"
                            "A deploy always finds it again for itself.");
    ImGui::SameLine();
    draw_reset_menu(ctx);
}

/// Drops the choices made by hand in one go: what was added, what was left out, or both.
void deploy_content_view::draw_reset_menu(rtti::context& ctx)
{
    auto& params = editor_->get();
    const std::size_t added_count = params.deploy_included.size();
    const std::size_t left_out_count = params.deploy_excluded.size();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, widgets::to_pixels(widgets::BUTTON_ROUNDING));
    ImGui::BeginDisabled(added_count + left_out_count == 0);
    if(ImGui::Button(RESET_LABEL, ImVec2(widgets::calc_button_width(RESET_LABEL), 0.0f)))
    {
        ImGui::OpenPopup("##reset_rules");
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::SetTooltip("%s",
                          added_count + left_out_count == 0
                              ? "Nothing was added or left out by hand."
                              : "Undo the folders and assets you added or left out by hand, so detection decides again.");
    }
    if(!ImGui::BeginPopup("##reset_rules"))
    {
        return;
    }
    bool is_changed = false;
    if(ImGui::MenuItem(fmt::format(ICON_MDI_PLUS_CIRCLE_OUTLINE " Reset Added by You ({})", added_count).c_str(),
                       nullptr,
                       false,
                       added_count > 0))
    {
        params.deploy_included.clear();
        is_changed = true;
    }
    if(ImGui::MenuItem(fmt::format(ICON_MDI_MINUS_CIRCLE_OUTLINE " Reset Left Out by You ({})", left_out_count).c_str(),
                       nullptr,
                       false,
                       left_out_count > 0))
    {
        params.deploy_excluded.clear();
        is_changed = true;
    }
    ImGui::Separator();
    if(ImGui::MenuItem(ICON_MDI_RESTORE " Reset All Your Choices", nullptr, false, added_count + left_out_count > 0))
    {
        params.deploy_included.clear();
        params.deploy_excluded.clear();
        is_changed = true;
    }
    if(is_changed)
    {
        editor_->commit(ctx);
    }
    ImGui::EndPopup();
}

void deploy_content_view::draw_summary()
{
    const ImU32 muted = widgets::get_muted_color();
    if(content_ == nullptr)
    {
        widgets::draw_icon_line(ICON_MDI_PROGRESS_CLOCK, muted, "Finding what the game uses...", muted);
        return;
    }
    const auto& root = tree_.get_root();
    const auto age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - detected_at_);
    std::string detected = fmt::format("found {} s ago", age.count());
    if(detect_job_.valid())
    {
        detected = "finding again...";
    }
    else if(age.count() < RECENT_DETECTION_SECONDS)
    {
        detected = "found just now";
    }
    const std::string text = fmt::format("{} of {} assets ship ({} of {}) - {}",
                                         root.shipped_count,
                                         root.asset_count,
                                         format_bytes(root.shipped_size, 1),
                                         format_bytes(root.total_size, 1),
                                         detected);
    widgets::draw_icon_line(ICON_MDI_PACKAGE_VARIANT_CLOSED, widgets::get_accent_u32(), text.c_str(), muted);
}

/// What detection cannot settle alone: left out assets the scenes use, folders scripts build paths in,
/// rules that name nothing.
void deploy_content_view::draw_notes(rtti::context& ctx)
{
    if(content_ == nullptr)
    {
        return;
    }
    if(tree_.get_left_out_used_count() > 0)
    {
        widgets::draw_gap(widgets::LINE_GAP);
        widgets::draw_icon_line(ICON_MDI_ALERT_OUTLINE,
                                widgets::WARNING_COLOR,
                                fmt::format("{} asset(s) you left out are used by the scenes. The game may miss them.",
                                            tree_.get_left_out_used_count())
                                    .c_str(),
                                ImGui::GetColorU32(ImGuiCol_Text));
    }
    draw_orphan_rule_note(ctx);
    if(editor_->get().deploy_only_used_assets)
    {
        draw_named_folder_notes(ctx);
    }
}

void deploy_content_view::draw_named_folder_notes(rtti::context& ctx)
{
    const auto& params = editor_->get();
    std::size_t noted = 0;
    for(const auto& folder : content_->named_folders)
    {
        if(deployment::find_rule(params, folder.key) == deployment::content_rule::include)
        {
            continue;
        }
        if(noted == NOTED_KEYS_MAX)
        {
            widgets::draw_muted_text("Scripts build asset paths in more folders at run time: check them in the tree.");
            break;
        }
        ++noted;
        widgets::draw_gap(widgets::LINE_GAP);
        ImGui::PushID(folder.key.c_str());
        const char* label = ICON_MDI_PLUS " Ship Folder";
        const float button_width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float right_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        widgets::draw_icon_line(ICON_MDI_INFORMATION_OUTLINE,
                                widgets::get_accent_u32(),
                                make_named_folder_note(folder).c_str(),
                                widgets::get_muted_color(),
                                right_x - button_width - ImGui::GetStyle().ItemSpacing.x);
        if(ImGui::BeginItemTooltip())
        {
            ImGui::TextUnformatted("Scripts building asset paths here:");
            for(const auto& script : folder.scripts)
            {
                ImGui::BulletText("%s", deployment::get_display_path(script).c_str());
            }
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(right_x - button_width);
        if(ImGui::SmallButton(label))
        {
            set_shipped(ctx, folder.key, true);
        }
        ImGui::SetItemTooltipEx("%s", "Ship everything in the folder.");
        ImGui::PopID();
    }
}

void deploy_content_view::draw_orphan_rule_note(rtti::context& ctx)
{
    auto& params = editor_->get();
    const auto orphans = deployment::find_orphan_rules(params, *content_);
    if(orphans.empty())
    {
        return;
    }
    std::string listed;
    for(std::size_t i = 0; i < orphans.size() && i < NOTED_KEYS_MAX; ++i)
    {
        listed += (i == 0 ? "" : ", ") + deployment::get_display_path(orphans[i]);
    }
    if(orphans.size() > NOTED_KEYS_MAX)
    {
        listed += fmt::format(" and {} more", orphans.size() - NOTED_KEYS_MAX);
    }
    widgets::draw_gap(widgets::LINE_GAP);
    const char* label = ICON_MDI_DELETE_OUTLINE " Remove";
    const float button_width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float right_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    widgets::draw_icon_line(ICON_MDI_ALERT_OUTLINE,
                            widgets::WARNING_COLOR,
                            fmt::format("{} rule(s) name nothing in the project: {}", orphans.size(), listed).c_str(),
                            ImGui::GetColorU32(ImGuiCol_Text),
                            right_x - button_width - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    ImGui::SetCursorPosX(right_x - button_width);
    if(ImGui::SmallButton(label))
    {
        for(const auto& orphan : orphans)
        {
            deployment::reset_rules(params, orphan);
        }
        editor_->commit(ctx);
    }
    ImGui::SetItemTooltipEx("%s", "Remove the rules on paths that are no longer in the project.");
}

void deploy_content_view::draw_tree(rtti::context& ctx)
{
    if(content_ == nullptr)
    {
        return;
    }
    tree_.collect_rows(
        [this](const deployment::tree_node& node)
        {
            return open_folders_.contains(node.key);
        },
        rows_);
    const float height = ImMax(widgets::to_pixels(TREE_MIN_HEIGHT), ImGui::GetContentRegionAvail().y);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable;
    if(!ImGui::BeginTable("##deploy_content_tree", 3, flags, ImVec2(0.0f, height)))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide);
    ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_WidthFixed, widgets::to_pixels(STATUS_COLUMN_WIDTH));
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, widgets::to_pixels(SIZE_COLUMN_WIDTH));
    ImGui::TableHeadersRow();
    const float row_height = ImGui::GetFrameHeight();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows_.size()), row_height);
    while(clipper.Step())
    {
        for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            draw_row(ctx, rows_[std::size_t(row)], row_height);
        }
    }
    if(rows_.empty())
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        widgets::draw_muted_text(content_->entries.empty() ? "The project has no assets yet." : "No asset matches.");
    }
    ImGui::EndTable();
}

void deploy_content_view::draw_row(rtti::context& ctx, std::size_t index, float row_height)
{
    const auto& node = tree_.get_nodes()[index];
    const bool is_shipped = node.shipped_count > 0;
    const float alpha = is_shipped ? 1.0f : LEFT_OUT_ALPHA;
    ImGui::PushID(node.key.c_str());
    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
    ImGui::TableNextColumn();
    const ImVec2 cell_min = ImGui::GetCursorScreenPos();
    const float cell_max_x = cell_min.x + ImGui::GetContentRegionAvail().x;
    // The row's own item: hovered across the row; a click opens a folder, a right click the menu.
    if(ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, row_height)) &&
       node.is_folder())
    {
        toggle_folder(node.key);
    }
    draw_row_menu(ctx, node);
    draw_tree_guides(cell_min, node.depth, row_height);
    auto* draw_list = ImGui::GetWindowDrawList();
    const float font_size = ImGui::GetFontSize();
    float x = cell_min.x + widgets::to_pixels(TREE_INDENT) * float(node.depth);
    if(node.is_folder())
    {
        const bool is_open = view_.is_narrowed() || open_folders_.contains(node.key);
        ImGui::RenderArrow(draw_list,
                           ImVec2(x + font_size * (1.0f - ARROW_SCALE) * 0.5f, cell_min.y + (row_height - font_size * ARROW_SCALE) * 0.5f),
                           ImGui::GetColorU32(ImGuiCol_Text, alpha),
                           is_open ? ImGuiDir_Down : ImGuiDir_Right,
                           ARROW_SCALE);
    }
    x += font_size;
    ImGui::SetCursorScreenPos(ImVec2(x, cell_min.y));
    if(draw_ship_checkbox(node))
    {
        set_shipped(ctx, node.key, node.shipped_count < node.asset_count);
    }
    const float icon_x = ImGui::GetItemRectMax().x + widgets::to_pixels(widgets::ICON_GAP);
    const bool is_open_folder = node.is_folder() && (view_.is_narrowed() || open_folders_.contains(node.key));
    const char* icon = node.is_folder() ? (is_open_folder ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER)
                                        : get_asset_icon_for_extension(fs::path(node.key).extension().generic_string());
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(icon_x + font_size * 0.5f, cell_min.y + row_height * 0.5f),
                              icon,
                              with_alpha(node.is_folder() ? FOLDER_ICON_COLOR : widgets::get_muted_color(), alpha));
    const float text_x = icon_x + font_size + widgets::to_pixels(widgets::ICON_GAP);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_Text, alpha));
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(text_x, cell_min.y + (row_height - font_size) * 0.5f),
                              ImVec2(cell_max_x, cell_min.y + row_height),
                              cell_max_x,
                              node.name.c_str(),
                              nullptr,
                              nullptr);
    ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    draw_status_cell(node, editor_->get());
    ImGui::TableNextColumn();
    draw_size_text(node.is_folder() ? node.shipped_size : node.total_size, !is_shipped);
    ImGui::PopID();
}

void deploy_content_view::toggle_folder(const std::string& key)
{
    if(!open_folders_.erase(key))
    {
        open_folders_.insert(key);
    }
}

/// The checkbox of a row: checked when everything in it ships, a dash when some of it does.
auto deploy_content_view::draw_ship_checkbox(const deployment::tree_node& node) const -> bool
{
    const float side = ImGui::GetFrameHeight();
    const bool is_all = node.asset_count > 0 && node.shipped_count == node.asset_count;
    const bool is_none = node.shipped_count == 0;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool is_pressed = ImGui::InvisibleButton("##ship", ImVec2(side, side));
    const char* icon = is_all ? ICON_MDI_CHECKBOX_MARKED : (is_none ? ICON_MDI_CHECKBOX_BLANK_OUTLINE : ICON_MDI_MINUS_BOX);
    ImU32 color = is_none ? widgets::get_muted_color() : widgets::get_accent_u32();
    if(ImGui::IsItemHovered())
    {
        color = ImGui::GetColorU32(ImGuiCol_Text);
    }
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(), min + ImVec2(side, side) * 0.5f, icon, color);
    if(!ImGui::BeginItemTooltip())
    {
        return is_pressed;
    }
    if(is_all)
    {
        ImGui::TextUnformatted(node.is_folder() ? "Everything here ships. Click to leave it all out." : "Ships with the game. Click to leave it out.");
    }
    else if(is_none)
    {
        ImGui::TextUnformatted(node.is_folder() ? "Nothing here ships. Click to ship it all." : "Left out of the game. Click to ship it.");
    }
    else
    {
        ImGui::Text("%zu of %zu assets here ship. Click to ship them all.", node.shipped_count, node.asset_count);
    }
    ImGui::EndTooltip();
    return is_pressed;
}

void deploy_content_view::draw_status_cell(const deployment::tree_node& node, const deploy_settings& params) const
{
    const ImU32 muted = widgets::get_muted_color();
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    if(node.is_folder())
    {
        const auto rule = deployment::has_own_rule(params, node.key) ? deployment::find_rule(params, node.key) : deployment::content_rule::none;
        if(rule == deployment::content_rule::include)
        {
            draw_cell_label(ICON_MDI_PLUS_CIRCLE_OUTLINE, ADDED_COLOR, "Folder added by you", text);
            return;
        }
        if(rule == deployment::content_rule::exclude)
        {
            draw_cell_label(ICON_MDI_MINUS_CIRCLE_OUTLINE, widgets::WARNING_COLOR, "Folder left out by you", text);
            return;
        }
        draw_cell_label(nullptr, 0, fmt::format("{} of {} ship", node.shipped_count, node.asset_count), muted);
        return;
    }
    const auto& entry = content_->entries[std::size_t(node.entry)];
    switch(deployment::get_reason(params, entry))
    {
        case deployment::content_reason::scene:
            draw_cell_label(ICON_MDI_MAP_OUTLINE, widgets::get_accent_u32(), "Scene in the build", text);
            break;
        case deployment::content_reason::referenced:
            draw_cell_label(ICON_MDI_LINK_VARIANT,
                            widgets::get_accent_u32(),
                            fmt::format("Used by {}", deployment::get_key_name(entry.used_by)),
                            text);
            if(ImGui::BeginItemTooltip())
            {
                ImGui::TextUnformatted(get_use_chain(entry).c_str());
                ImGui::EndTooltip();
            }
            break;
        case deployment::content_reason::named_in_script:
            draw_cell_label(ICON_MDI_CODE_BRACES,
                            widgets::get_accent_u32(),
                            fmt::format("Named in {}", deployment::get_key_name(entry.used_by)),
                            text);
            draw_tooltip_text("A script names its path:\n" + deployment::get_display_path(entry.used_by));
            break;
        case deployment::content_reason::included:
            draw_cell_label(ICON_MDI_PLUS_CIRCLE_OUTLINE, ADDED_COLOR, "Added by you", text);
            draw_tooltip_text("You chose to ship it. Right click > Use Detected to undo.");
            break;
        case deployment::content_reason::excluded:
            draw_cell_label(ICON_MDI_MINUS_CIRCLE_OUTLINE,
                            widgets::WARNING_COLOR,
                            entry.is_used ? "Left out, but used" : "Left out by you",
                            text);
            if(ImGui::BeginItemTooltip())
            {
                ImGui::TextUnformatted(entry.is_used ? (get_use_chain(entry) + "\n\nThe game may miss it.").c_str()
                                                     : "You chose to leave it out. Right click > Use Detected to undo.");
                ImGui::EndTooltip();
            }
            break;
        case deployment::content_reason::unused:
        default:
            draw_cell_label(nullptr, 0, params.deploy_only_used_assets ? "Not used" : "Not used by the scenes", muted);
            break;
    }
}

void deploy_content_view::draw_row_menu(rtti::context& ctx, const deployment::tree_node& node)
{
    if(!ImGui::BeginPopupContextItem("##row_menu"))
    {
        return;
    }
    if(ImGui::MenuItem(ICON_MDI_CHECKBOX_MARKED_OUTLINE " Ship", nullptr, false, node.shipped_count < node.asset_count))
    {
        set_shipped(ctx, node.key, true);
    }
    if(ImGui::MenuItem(ICON_MDI_CHECKBOX_BLANK_OUTLINE " Leave Out", nullptr, false, node.shipped_count > 0))
    {
        set_shipped(ctx, node.key, false);
    }
    if(ImGui::MenuItem(ICON_MDI_RESTORE " Use Detected", nullptr, false, node.has_rule_inside))
    {
        reset_rules(ctx, node.key);
    }
    ImGui::SetItemTooltipEx("%s", "Drop your choices here, so detection decides again.");
    ImGui::Separator();
    if(!node.is_folder() && ImGui::MenuItem(ICON_MDI_FOLDER_OPEN_OUTLINE " Show in Content Browser"))
    {
        editor_actions::reveal_in_content_browser(ctx, node.key);
    }
    if(ImGui::MenuItem(ICON_MDI_CONTENT_COPY " Copy Path"))
    {
        ImGui::SetClipboardText(node.key.c_str());
    }
    ImGui::EndPopup();
}

/// The asset, then each "used by" step back to the scene, script or settings the walk started from.
auto deploy_content_view::get_use_chain(const deployment::content_entry& entry) const -> std::string
{
    std::string chain = deployment::get_display_path(entry.key);
    const deployment::content_entry* current = &entry;
    for(std::size_t step = 0; step < USE_CHAIN_MAX && current != nullptr && !current->used_by.empty(); ++step)
    {
        chain += "\n  used by " + deployment::get_display_path(current->used_by);
        current = content_->find(current->used_by);
    }
    return chain;
}

void deploy_content_view::set_shipped(rtti::context& ctx, const std::string& key, bool is_shipped)
{
    deployment::set_shipped(editor_->get(), *content_, key, is_shipped);
    editor_->commit(ctx);
}

void deploy_content_view::reset_rules(rtti::context& ctx, const std::string& key)
{
    deployment::reset_rules(editor_->get(), key);
    editor_->commit(ctx);
}

} // namespace unravel
