#include "deploy_content_view.h"
#include "deploy_panel_widgets.h"

#include <editor/assets/asset_icons.h>
#include <editor/editing/editor_actions.h>
#include <editor/format/format_bytes.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/ecs/prefab.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <unordered_map>

namespace unravel
{
namespace
{
using namespace deploy_widgets;

/// The protocol whose content a deploy selects.
constexpr const char* CONTENT_PROTOCOL = "app";
// Sizes in font units.
constexpr float TREE_MIN_HEIGHT = 10.0f;
constexpr float STATUS_COLUMN_WIDTH = 17.0f;
constexpr float SIZE_COLUMN_WIDTH = 5.5f;
constexpr float SCENE_PATH_COLUMN_SHARE = 1.4f;
constexpr float SCENE_PICKER_WIDTH = 26.0f;
constexpr float SCENE_PICKER_MAX_HEIGHT = 18.0f;
constexpr float FILTER_WIDTH = 9.0f;
constexpr const char* SCENE_STARTUP_LABEL = "Startup";
/// A detection younger than this reads "just now".
constexpr long long RECENT_DETECTION_SECONDS = 5;
/// Scene rows shown before the scene list scrolls.
constexpr int SCENE_ROWS_SHOWN = 4;
/// Folders scripts build paths in, listed in the notes before "and N more".
constexpr size_t NOTED_FOLDERS_MAX = 3;
/// Steps of a "used by" chain shown in a tooltip.
constexpr size_t USE_CHAIN_MAX = 12;
/// Rows that do not ship are drawn at this alpha.
constexpr float LEFT_OUT_ALPHA = 0.45f;
constexpr ImU32 FOLDER_ICON_COLOR = IM_COL32(214, 178, 104, 255);
constexpr ImU32 ADDED_COLOR = IM_COL32(110, 200, 130, 255);

auto get_file_name(const std::string& key) -> std::string
{
    const size_t slash = key.rfind('/');
    return slash == std::string::npos ? key : key.substr(slash + 1);
}

/// A key as the content browser shows it: under the data folder, without the protocol.
auto get_display_path(const std::string& key) -> std::string
{
    const std::string root = ex::get_data_directory(CONTENT_PROTOCOL) + "/";
    return key.compare(0, root.size(), root) == 0 ? key.substr(root.size()) : key;
}

auto has_own_rule(const deploy_settings& params, const std::string& key) -> bool
{
    const auto is_key = [&key](const std::string& rule)
    {
        return rule == key;
    };
    return std::any_of(params.deploy_included.begin(), params.deploy_included.end(), is_key) ||
           std::any_of(params.deploy_excluded.begin(), params.deploy_excluded.end(), is_key);
}

auto is_scene_key(const std::string& key) -> bool
{
    return ex::is_format<scene_prefab>(fs::path(key).extension().generic_string());
}

/// A size column cell: right aligned, muted.
void draw_size_text(std::uint64_t size, bool is_dimmed)
{
    const std::string text = format_bytes(size, 1);
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - width));
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    if(is_dimmed)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * LEFT_OUT_ALPHA);
    }
    ImGui::TextUnformatted(text.c_str());
    if(is_dimmed)
    {
        ImGui::PopStyleVar();
    }
    ImGui::PopStyleColor();
}

/// An icon (none when null) and a short text in a table cell, centered on a frame-high row; the text
/// ends in an ellipsis where the cell does. The cell is one item, for a tooltip.
void draw_cell_label(const char* icon, ImU32 icon_color, const std::string& text, ImU32 text_color)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float row_height = ImGui::GetFrameHeight();
    const float icon_width = icon == nullptr ? 0.0f : ImGui::GetFontSize() + to_pixels(ICON_GAP);
    const float text_y = min.y + (row_height - ImGui::GetFontSize()) * 0.5f;
    auto* draw_list = ImGui::GetWindowDrawList();
    if(icon != nullptr)
    {
        ImGui::RenderIconCentered(draw_list,
                                  ImVec2(min.x + ImGui::GetFontSize() * 0.5f, min.y + row_height * 0.5f),
                                  icon,
                                  icon_color);
    }
    const float max_x = min.x + ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(min.x + icon_width, text_y),
                              ImVec2(max_x, min.y + row_height),
                              max_x,
                              text.c_str(),
                              nullptr,
                              nullptr);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, row_height));
}
} // namespace

void deploy_content_view::invalidate()
{
    is_detection_requested_ = true;
}

void deploy_content_view::draw(rtti::context& ctx)
{
    poll_detection();
    auto& pm = ctx.get_cached<project_manager>();
    const auto startup_scene = pm.get_settings().standalone.startup_scene.id();
    if(has_content_ && startup_scene != detected_startup_scene_)
    {
        is_detection_requested_ = true;
    }
    if(is_detection_requested_ && !detect_job_.valid())
    {
        start_detection(ctx);
    }
    draw_scenes(ctx);
    draw_gap(SECTION_GAP);
    draw_assets_header(ctx);
    draw_tree(ctx);
}

void deploy_content_view::start_detection(rtti::context& ctx)
{
    is_detection_requested_ = false;
    auto& am = ctx.get_cached<asset_manager>();
    auto& th = ctx.get_cached<threader>();
    const auto params = ctx.get_cached<project_manager>().get_deploy_settings();
    const auto startup_scene = ctx.get_cached<project_manager>().get_settings().standalone.startup_scene.id();
    detected_startup_scene_ = startup_scene;
    detect_job_ = th.pool
                      ->schedule("Detecting Deploy Content",
                                 [&am, params, startup_scene]()
                                 {
                                     return detect_deploy_content(am, params, startup_scene);
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
        content_ = detect_job_.get();
        has_content_ = true;
        detected_at_ = std::chrono::steady_clock::now();
        build_tree();
    }
    catch(const std::exception& ex)
    {
        APPLOG_ERROR("Detecting the deploy content failed: {}", ex.what());
    }
    detect_job_ = {};
}

void deploy_content_view::build_tree()
{
    nodes_.clear();
    const std::string root_key = ex::get_data_directory(CONTENT_PROTOCOL);
    nodes_.push_back({get_file_name(root_key), root_key});
    std::unordered_map<std::string, size_t> folders{{root_key, 0}};
    for(size_t i = 0; i < content_.entries.size(); ++i)
    {
        const auto& key = content_.entries[i].key;
        if(key.compare(0, root_key.size() + 1, root_key + "/") != 0)
        {
            continue;
        }
        size_t parent = 0;
        size_t start = root_key.size() + 1;
        for(size_t slash = key.find('/', start); slash != std::string::npos; slash = key.find('/', start))
        {
            const auto folder_key = key.substr(0, slash);
            auto found = folders.find(folder_key);
            if(found == folders.end())
            {
                nodes_.push_back({key.substr(start, slash - start), folder_key});
                found = folders.emplace(folder_key, nodes_.size() - 1).first;
                nodes_[parent].children.push_back(found->second);
            }
            parent = found->second;
            start = slash + 1;
        }
        nodes_.push_back({get_file_name(key), key, static_cast<int>(i)});
        nodes_[parent].children.push_back(nodes_.size() - 1);
    }
    for(auto& node : nodes_)
    {
        std::sort(node.children.begin(),
                  node.children.end(),
                  [this](size_t lhs, size_t rhs)
                  {
                      const bool is_lhs_folder = nodes_[lhs].entry < 0;
                      const bool is_rhs_folder = nodes_[rhs].entry < 0;
                      if(is_lhs_folder != is_rhs_folder)
                      {
                          return is_lhs_folder;
                      }
                      return string_utils::to_lower(nodes_[lhs].name) < string_utils::to_lower(nodes_[rhs].name);
                  });
    }
}

auto deploy_content_view::is_row_shown(const tree_node& node, const deploy_settings& params) const -> bool
{
    const auto& entry = content_.entries[node.entry];
    if(search_.IsActive() && !search_.PassFilter(get_display_path(entry.key).c_str()))
    {
        return false;
    }
    switch(filter_)
    {
        case row_filter::shipped:
            return node.shipped_count > 0;
        case row_filter::left_out:
            return node.shipped_count == 0;
        case row_filter::changed:
            return find_deploy_content_rule(params, entry.key) != deploy_content_rule::none;
        case row_filter::all:
        default:
            return true;
    }
}

/// Children come after their parent in nodes_, so one backward pass sums every folder.
void deploy_content_view::update_tree(const deploy_settings& params)
{
    for(size_t i = nodes_.size(); i-- > 0;)
    {
        auto& node = nodes_[i];
        node.has_rule_inside = has_own_rule(params, node.key);
        if(node.entry >= 0)
        {
            const auto& entry = content_.entries[node.entry];
            const bool is_shipped = is_deploy_content_shipped(params, entry);
            node.asset_count = 1;
            node.shipped_count = is_shipped ? 1 : 0;
            node.total_size = entry.size;
            node.shipped_size = is_shipped ? entry.size : 0;
            node.is_visible = is_row_shown(node, params);
            continue;
        }
        node.asset_count = 0;
        node.shipped_count = 0;
        node.total_size = 0;
        node.shipped_size = 0;
        node.is_visible = false;
        for(const size_t child_index : node.children)
        {
            const auto& child = nodes_[child_index];
            node.asset_count += child.asset_count;
            node.shipped_count += child.shipped_count;
            node.total_size += child.total_size;
            node.shipped_size += child.shipped_size;
            node.has_rule_inside = node.has_rule_inside || child.has_rule_inside;
            node.is_visible = node.is_visible || child.is_visible;
        }
    }
}

void deploy_content_view::draw_scenes(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& params = pm.get_deploy_settings();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted("Scenes in Build");
    ImGui::PopFont();
    ImGui::SameLine();
    draw_add_scene_button(ctx);
    draw_muted_text("What these scenes use ships with the game. Add the scenes your scripts load.");
    draw_gap(LINE_GAP);
    const float row_height = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    const int row_count = static_cast<int>(params.deploy_scenes.size()) + 1;
    const float height = row_height * static_cast<float>(std::min(row_count, SCENE_ROWS_SHOWN));
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_PadOuterX;
    if(!ImGui::BeginTable("##deploy_scenes", 3, flags, ImVec2(0.0f, height + ImGui::GetStyle().CellPadding.y)))
    {
        return;
    }
    ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, SCENE_PATH_COLUMN_SHARE);
    const float startup_width = ImGui::GetFontSize() + to_pixels(ICON_GAP) + ImGui::CalcTextSize(SCENE_STARTUP_LABEL).x;
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, ImMax(startup_width, ImGui::GetFrameHeight()));
    std::string removed;
    const auto startup_scene = pm.get_settings().standalone.startup_scene.id();
    draw_scene_row(ctx, startup_scene, true, removed);
    for(const auto& scene : params.deploy_scenes)
    {
        if(scene != startup_scene)
        {
            draw_scene_row(ctx, scene, false, removed);
        }
    }
    ImGui::EndTable();
    if(!removed.empty())
    {
        params.deploy_scenes.erase(std::remove(params.deploy_scenes.begin(), params.deploy_scenes.end(), removed),
                                   params.deploy_scenes.end());
        pm.save_deploy_settings();
        is_detection_requested_ = true;
    }
}

void deploy_content_view::draw_scene_row(rtti::context& ctx, const std::string& key, bool is_startup, std::string& removed)
{
    const bool is_missing =
        !is_startup &&
        std::find(content_.missing_scenes.begin(), content_.missing_scenes.end(), key) != content_.missing_scenes.end();
    ImGui::PushID(key.c_str());
    ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight());
    ImGui::TableNextColumn();
    const std::string name = key.empty() ? std::string("No startup scene") : fs::path(get_file_name(key)).stem().string();
    draw_cell_label(is_missing ? ICON_MDI_ALERT_OUTLINE : ICON_MDI_MAP_OUTLINE,
                    is_missing ? WARNING_COLOR : get_accent_u32(),
                    name,
                    ImGui::GetColorU32(ImGuiCol_Text));
    if(is_missing)
    {
        ImGui::SetItemTooltipEx("%s", "This scene is no longer in the project. Remove it from the list.");
    }
    ImGui::TableNextColumn();
    draw_cell_label(nullptr, 0, get_display_path(key), imgui_style::get_muted_text_color_u32());
    ImGui::TableNextColumn();
    if(is_startup)
    {
        draw_cell_label(ICON_MDI_FLAG_CHECKERED, get_muted_color(), SCENE_STARTUP_LABEL, get_muted_color());
        ImGui::SetItemTooltipEx("%s", "The scene the game starts with, set in the Build tab.");
    }
    else
    {
        const float side = ImGui::GetFrameHeight();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - side));
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImRect rect(min, min + ImVec2(side, side));
        ImGui::ItemSize(rect);
        if(container_widgets::draw_icon_button({"##remove_scene", ICON_MDI_CLOSE, "Remove from the build", true}, rect))
        {
            removed = key;
        }
    }
    ImGui::PopID();
}

void deploy_content_view::draw_add_scene_button(rtti::context& ctx)
{
    const char* label = ICON_MDI_PLUS " Add Scene";
    const float width = calc_button_width(label);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - width));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(BUTTON_ROUNDING));
    if(ImGui::Button(label, ImVec2(width, 0.0f)))
    {
        scene_choices_.clear();
        for(const auto& key : ctx.get_cached<asset_manager>().get_all_assets(std::string(CONTENT_PROTOCOL) + ":/"))
        {
            if(is_scene_key(key))
            {
                scene_choices_.push_back(key);
            }
        }
        std::sort(scene_choices_.begin(), scene_choices_.end());
        scene_search_.Clear();
        ImGui::OpenPopup("##add_scene");
    }
    ImGui::PopStyleVar();
    ImGui::SetItemTooltipEx("%s", "Add a scene the game loads from a script, so what it uses ships too.");
    if(!ImGui::BeginPopup("##add_scene"))
    {
        return;
    }
    auto& pm = ctx.get_cached<project_manager>();
    auto& params = pm.get_deploy_settings();
    const auto startup_scene = pm.get_settings().standalone.startup_scene.id();
    std::vector<std::string> choices;
    for(const auto& key : scene_choices_)
    {
        const bool is_listed = key == startup_scene || std::find(params.deploy_scenes.begin(),
                                                                 params.deploy_scenes.end(),
                                                                 key) != params.deploy_scenes.end();
        if(!is_listed)
        {
            choices.push_back(key);
        }
    }
    if(ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::DrawFilterWithHint(scene_search_, ICON_MDI_MAGNIFY " Search scenes...", to_pixels(SCENE_PICKER_WIDTH));
    draw_gap(LINE_GAP);
    std::vector<std::string> added;
    if(choices.empty())
    {
        draw_muted_text("Every scene of the project is in the build.");
    }
    else
    {
        if(choices.size() > 1 && !scene_search_.IsActive() &&
           ImGui::Selectable(fmt::format(ICON_MDI_PLAYLIST_PLUS " Add all {} scenes", choices.size()).c_str()))
        {
            added = choices;
        }
        const float list_height = ImMin(to_pixels(SCENE_PICKER_MAX_HEIGHT),
                                        ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(choices.size() + 1));
        if(ImGui::BeginChild("##scene_choices", ImVec2(to_pixels(SCENE_PICKER_WIDTH), list_height)))
        {
            for(const auto& key : choices)
            {
                const auto path = get_display_path(key);
                if(scene_search_.PassFilter(path.c_str()) &&
                   ImGui::Selectable(fmt::format(ICON_MDI_MAP_OUTLINE " {}##{}", path, key).c_str()))
                {
                    added.push_back(key);
                }
            }
        }
        ImGui::EndChild();
    }
    if(!added.empty())
    {
        params.deploy_scenes.insert(params.deploy_scenes.end(), added.begin(), added.end());
        pm.save_deploy_settings();
        is_detection_requested_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void deploy_content_view::draw_assets_header(rtti::context& ctx)
{
    draw_section_title("Assets");
    draw_mode(ctx);
    draw_gap(LINE_GAP);
    draw_toolbar(ctx);
    draw_gap(LINE_GAP);
    draw_summary();
    draw_notes(ctx);
    draw_gap(LINE_GAP);
}

void deploy_content_view::draw_mode(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& params = pm.get_deploy_settings();
    bool is_changed = false;
    if(ImGui::RadioButton("Everything", !params.deploy_only_used_assets))
    {
        params.deploy_only_used_assets = false;
        is_changed = true;
    }
    ImGui::SetItemTooltipEx("%s",
                            "Every project asset ships, except what you leave out below.\n"
                            "Safe: the game finds whatever it loads.");
    ImGui::SameLine(0.0f, to_pixels(SECTION_GAP));
    if(ImGui::RadioButton("Only used", params.deploy_only_used_assets))
    {
        params.deploy_only_used_assets = true;
        is_changed = true;
    }
    ImGui::SetItemTooltipEx("%s",
                            "Ships what the scenes above use, directly or through other assets, and what\n"
                            "scripts name by path. Check what scripts load by a path built at run time.");
    if(is_changed)
    {
        pm.save_deploy_settings();
    }
}

void deploy_content_view::draw_toolbar(rtti::context& ctx)
{
    struct filter_choice
    {
        row_filter filter;
        const char* label;
    };
    static constexpr filter_choice filters[] = {
        {row_filter::all, "All assets"},
        {row_filter::shipped, "Shipping"},
        {row_filter::left_out, "Left out"},
        {row_filter::changed, "Changed by you"},
    };
    const char* detect_label = ICON_MDI_MAGNIFY_SCAN " Detect";
    const float filter_width = to_pixels(FILTER_WIDTH);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float search_width = ImMax(to_pixels(FILTER_WIDTH),
                                     ImGui::GetContentRegionAvail().x - filter_width - calc_button_width(detect_label) -
                                         2.0f * spacing);
    ImGui::DrawFilterWithHint(search_, ICON_MDI_MAGNIFY " Search assets...", search_width);
    ImGui::DrawItemActivityOutline();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(filter_width);
    if(ImGui::BeginCombo("##row_filter", filters[static_cast<int>(filter_)].label))
    {
        for(const auto& choice : filters)
        {
            if(ImGui::Selectable(choice.label, filter_ == choice.filter))
            {
                filter_ = choice.filter;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltipEx("%s", "Show only some of the assets");
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(BUTTON_ROUNDING));
    ImGui::BeginDisabled(detect_job_.valid());
    if(ImGui::Button(detect_label, ImVec2(calc_button_width(detect_label), 0.0f)))
    {
        start_detection(ctx);
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::SetItemTooltipEx("%s",
                            "Find again what the scenes and scripts use, after adding or changing assets.\n"
                            "A deploy always finds it again for itself.");
}

void deploy_content_view::draw_summary()
{
    const ImU32 muted = imgui_style::get_muted_text_color_u32();
    if(!has_content_)
    {
        draw_icon_line(ICON_MDI_PROGRESS_CLOCK, muted, "Finding what the game uses...", muted);
        return;
    }
    const auto& root = nodes_.front();
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
    draw_icon_line(ICON_MDI_PACKAGE_VARIANT_CLOSED, get_accent_u32(), text.c_str(), muted);
}

/// What detection cannot settle alone: left out assets the scenes use, folders scripts build paths in.
void deploy_content_view::draw_notes(rtti::context& ctx)
{
    if(!has_content_)
    {
        return;
    }
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    const auto left_out_used = std::count_if(content_.entries.begin(),
                                             content_.entries.end(),
                                             [&params](const deploy_content_entry& entry)
                                             {
                                                 return entry.is_used && !is_deploy_content_shipped(params, entry);
                                             });
    if(left_out_used > 0)
    {
        draw_gap(LINE_GAP);
        draw_icon_line(ICON_MDI_ALERT_OUTLINE,
                       WARNING_COLOR,
                       fmt::format("{} asset(s) you left out are used by the scenes; the game may miss them.", left_out_used)
                           .c_str(),
                       ImGui::GetColorU32(ImGuiCol_Text));
    }
    if(!params.deploy_only_used_assets)
    {
        return;
    }
    size_t noted = 0;
    for(const auto& folder : content_.named_folders)
    {
        if(find_deploy_content_rule(params, folder) == deploy_content_rule::include)
        {
            continue;
        }
        if(noted == NOTED_FOLDERS_MAX)
        {
            draw_muted_text("Scripts build paths in more folders: check them in the tree.");
            break;
        }
        ++noted;
        draw_gap(LINE_GAP);
        ImGui::PushID(folder.c_str());
        const char* label = ICON_MDI_PLUS " Ship Folder";
        const float button_width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float right_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::PushTextWrapPos(right_x - button_width - ImGui::GetStyle().ItemSpacing.x);
        draw_icon_line(ICON_MDI_INFORMATION_OUTLINE,
                       get_accent_u32(),
                       fmt::format("A script builds paths in {}: what it loads from there cannot be found.",
                                   get_display_path(folder))
                           .c_str(),
                       get_muted_color());
        ImGui::PopTextWrapPos();
        ImGui::SameLine();
        ImGui::SetCursorPosX(right_x - button_width);
        if(ImGui::SmallButton(label))
        {
            set_shipped(ctx, folder, true);
        }
        ImGui::SetItemTooltipEx("%s", "Ship everything in the folder");
        ImGui::PopID();
    }
}

void deploy_content_view::draw_tree(rtti::context& ctx)
{
    if(!has_content_)
    {
        return;
    }
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    update_tree(params);
    const float height = ImMax(to_pixels(TREE_MIN_HEIGHT), ImGui::GetContentRegionAvail().y);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody |
                                  ImGuiTableFlags_Resizable;
    if(!ImGui::BeginTable("##deploy_content_tree", 3, flags, ImVec2(0.0f, height)))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide);
    ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_WidthFixed, to_pixels(STATUS_COLUMN_WIDTH));
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, to_pixels(SIZE_COLUMN_WIDTH));
    ImGui::TableHeadersRow();
    const auto& root = nodes_.front();
    for(const size_t child : root.children)
    {
        draw_node(ctx, child);
    }
    if(!root.is_visible)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        draw_muted_text(content_.entries.empty() ? "The project has no assets yet." : "No asset matches.");
    }
    ImGui::EndTable();
}

void deploy_content_view::draw_node(rtti::context& ctx, size_t index)
{
    const auto& node = nodes_[index];
    if(!node.is_visible)
    {
        return;
    }
    const bool is_folder = node.entry < 0;
    const bool is_shipped = node.shipped_count > 0;
    const float row_height = ImGui::GetFrameHeight();
    ImGui::PushID(node.key.c_str());
    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
    ImGui::TableNextColumn();
    const ImVec2 cell_min = ImGui::GetCursorScreenPos();
    const float cell_max_x = cell_min.x + ImGui::GetContentRegionAvail().x;
    // A click on a folder's row opens it; the checkbox over the row takes its own clicks.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_AllowOverlap |
                               ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DrawLinesToNodes;
    if(!is_folder)
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if(search_.IsActive() || filter_ != row_filter::all)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    const bool is_open = ImGui::TreeNodeEx("##node", flags);
    draw_node_menu(ctx, node);
    const float arrow_width = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(cell_min.x + arrow_width, cell_min.y));
    if(draw_ship_checkbox(node))
    {
        set_shipped(ctx, node.key, node.shipped_count < node.asset_count);
    }
    const float alpha = is_shipped ? 1.0f : LEFT_OUT_ALPHA;
    const float icon_x = ImGui::GetItemRectMax().x + to_pixels(ICON_GAP);
    const char* icon = is_folder ? (is_open ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER)
                                 : get_asset_icon_for_extension(fs::path(node.key).extension().generic_string());
    ImVec4 icon_color = ImGui::ColorConvertU32ToFloat4(is_folder ? FOLDER_ICON_COLOR : get_muted_color());
    icon_color.w *= alpha;
    auto* draw_list = ImGui::GetWindowDrawList();
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(icon_x + ImGui::GetFontSize() * 0.5f, cell_min.y + row_height * 0.5f),
                              icon,
                              ImGui::ColorConvertFloat4ToU32(icon_color));
    const float text_x = icon_x + ImGui::GetFontSize() + to_pixels(ICON_GAP);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_Text, alpha));
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(text_x, cell_min.y + (row_height - ImGui::GetFontSize()) * 0.5f),
                              ImVec2(cell_max_x, cell_min.y + row_height),
                              cell_max_x,
                              node.name.c_str(),
                              nullptr,
                              nullptr);
    ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    draw_status_cell(node, ctx.get_cached<project_manager>().get_deploy_settings());
    ImGui::TableNextColumn();
    draw_size_text(is_folder ? node.shipped_size : node.total_size, !is_shipped);
    if(is_folder && is_open)
    {
        for(const size_t child : node.children)
        {
            draw_node(ctx, child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

/// The checkbox of a row: checked when everything in it ships, a dash when some of it does.
auto deploy_content_view::draw_ship_checkbox(const tree_node& node) -> bool
{
    const float side = ImGui::GetFrameHeight();
    const bool is_all = node.asset_count > 0 && node.shipped_count == node.asset_count;
    const bool is_none = node.shipped_count == 0;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool is_pressed = ImGui::InvisibleButton("##ship", ImVec2(side, side));
    const bool is_hovered = ImGui::IsItemHovered();
    const char* icon = is_all ? ICON_MDI_CHECKBOX_MARKED : (is_none ? ICON_MDI_CHECKBOX_BLANK_OUTLINE : ICON_MDI_MINUS_BOX);
    ImU32 color = is_none ? get_muted_color() : get_accent_u32();
    if(is_hovered)
    {
        color = ImGui::GetColorU32(ImGuiCol_Text);
    }
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(), min + ImVec2(side, side) * 0.5f, icon, color);
    const bool is_folder = node.entry < 0;
    if(is_all)
    {
        ImGui::SetItemTooltipEx("%s",
                                is_folder ? "Everything here ships. Click to leave it all out."
                                          : "Ships with the game. Click to leave it out.");
    }
    else if(is_none)
    {
        ImGui::SetItemTooltipEx("%s",
                                is_folder ? "Nothing here ships. Click to ship it all."
                                          : "Left out of the game. Click to ship it.");
    }
    else
    {
        ImGui::SetItemTooltipEx("%zu of %zu assets here ship. Click to ship them all.", node.shipped_count, node.asset_count);
    }
    return is_pressed;
}

void deploy_content_view::draw_status_cell(const tree_node& node, const deploy_settings& params)
{
    const ImU32 muted = get_muted_color();
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    if(node.entry < 0)
    {
        const auto rule = has_own_rule(params, node.key) ? find_deploy_content_rule(params, node.key)
                                                         : deploy_content_rule::none;
        if(rule == deploy_content_rule::include)
        {
            draw_cell_label(ICON_MDI_PLUS_CIRCLE_OUTLINE, ADDED_COLOR, "Folder added by you", text);
        }
        else if(rule == deploy_content_rule::exclude)
        {
            draw_cell_label(ICON_MDI_MINUS_CIRCLE_OUTLINE, WARNING_COLOR, "Folder left out by you", text);
        }
        else
        {
            draw_cell_label(nullptr, 0, fmt::format("{} of {} ship", node.shipped_count, node.asset_count), muted);
        }
        return;
    }
    const auto& entry = content_.entries[node.entry];
    switch(get_deploy_content_reason(params, entry))
    {
        case deploy_content_reason::scene:
            draw_cell_label(ICON_MDI_MAP_OUTLINE, get_accent_u32(), "Scene in the build", text);
            break;
        case deploy_content_reason::referenced:
            draw_cell_label(ICON_MDI_LINK_VARIANT, get_accent_u32(), fmt::format("Used by {}", get_file_name(entry.used_by)), text);
            ImGui::SetItemTooltipEx("%s", get_use_chain(entry).c_str());
            break;
        case deploy_content_reason::named_in_script:
            draw_cell_label(ICON_MDI_CODE_BRACES,
                            get_accent_u32(),
                            fmt::format("Named in {}", get_file_name(entry.used_by)),
                            text);
            ImGui::SetItemTooltipEx("A script names its path:\n%s", get_display_path(entry.used_by).c_str());
            break;
        case deploy_content_reason::included:
            draw_cell_label(ICON_MDI_PLUS_CIRCLE_OUTLINE, ADDED_COLOR, "Added by you", text);
            ImGui::SetItemTooltipEx("%s", "You chose to ship it. Right click > Use Detected to undo.");
            break;
        case deploy_content_reason::excluded:
            draw_cell_label(ICON_MDI_MINUS_CIRCLE_OUTLINE, WARNING_COLOR, entry.is_used ? "Left out, but used" : "Left out by you", text);
            if(entry.is_used)
            {
                ImGui::SetItemTooltipEx("%s\n\nThe game may miss it.", get_use_chain(entry).c_str());
            }
            else
            {
                ImGui::SetItemTooltipEx("%s", "You chose to leave it out. Right click > Use Detected to undo.");
            }
            break;
        case deploy_content_reason::unused:
        default:
            draw_cell_label(nullptr, 0, params.deploy_only_used_assets ? "Not used" : "Not used by the scenes", muted);
            break;
    }
}

void deploy_content_view::draw_node_menu(rtti::context& ctx, const tree_node& node)
{
    if(!ImGui::BeginPopupContextItem("##node_menu"))
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
    ImGui::SetItemTooltipEx("%s", "Drop your choices here, so detection decides again");
    ImGui::Separator();
    if(node.entry >= 0 && ImGui::MenuItem(ICON_MDI_FOLDER_OPEN_OUTLINE " Show in Content Browser"))
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
auto deploy_content_view::get_use_chain(const deploy_content_entry& entry) const -> std::string
{
    std::string chain = get_display_path(entry.key);
    const deploy_content_entry* current = &entry;
    for(size_t step = 0; step < USE_CHAIN_MAX && current != nullptr && !current->used_by.empty(); ++step)
    {
        chain += "\n  used by " + get_display_path(current->used_by);
        const auto found = std::lower_bound(content_.entries.begin(),
                                            content_.entries.end(),
                                            current->used_by,
                                            [](const deploy_content_entry& lhs, const std::string& key)
                                            {
                                                return lhs.key < key;
                                            });
        current = found != content_.entries.end() && found->key == current->used_by ? &*found : nullptr;
    }
    return chain;
}

void deploy_content_view::set_shipped(rtti::context& ctx, const std::string& key, bool is_shipped)
{
    auto& pm = ctx.get_cached<project_manager>();
    set_deploy_content_shipped(pm.get_deploy_settings(), content_, key, is_shipped);
    pm.save_deploy_settings();
}

void deploy_content_view::reset_rules(rtti::context& ctx, const std::string& key)
{
    auto& pm = ctx.get_cached<project_manager>();
    reset_deploy_content_rules(pm.get_deploy_settings(), key);
    pm.save_deploy_settings();
}

} // namespace unravel
