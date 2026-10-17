#include "deploy_panel.h"
#include "../panel.h"
#include "../panels_defs.h"

#include <editor/format/format_bytes.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>
#include <editor/system/project_manager.h>

#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <hpp/string_view.hpp>

#include <algorithm>
#include <initializer_list>

namespace unravel
{
namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, like the start page and the About window.
constexpr float DEPLOY_WIDTH = 42.0f;
constexpr float DEPLOY_ROUNDING = 0.75f;
constexpr float DEPLOY_TITLE_SCALE = 1.3f;
constexpr float DEPLOY_TITLE_ICON_GAP = 0.45f;
constexpr float DEPLOY_SECTION_GAP = 1.0f;
constexpr float DEPLOY_LINE_GAP = 0.35f;
constexpr float DEPLOY_BUTTON_HEIGHT = 2.2f;
constexpr float DEPLOY_BUTTON_PADDING_X = 1.0f;
constexpr float DEPLOY_BUTTON_ROUNDING = 0.4f;
constexpr float DEPLOY_CLOSE_INSET = 0.6f;
constexpr float DEPLOY_SCREEN_MARGIN = 1.0f;
// A tab body is at least this tall, so switching tabs barely moves the window, and at most this
// share of the work area, so long lists scroll inside it and the status and buttons stay in view.
constexpr float DEPLOY_BODY_MIN_HEIGHT = 20.0f;
constexpr float DEPLOY_BODY_MAX_SHARE = 0.5f;
constexpr float DEPLOY_ACCENT_ACTIVE_SHADE = 0.85f;
constexpr ImU32 DEPLOY_WARNING_COLOR = IM_COL32(255, 190, 60, 255);
// The theme's fields are darker than a window and all but vanish on the lighter popup, so they
// are washed light here, as on the start page.
constexpr ImU32 DEPLOY_FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 DEPLOY_FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 DEPLOY_FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);
// Jobs are named "Deploying X"; the steps show X.
constexpr hpp::string_view DEPLOY_JOB_PREFIX = "Deploying ";
// The empty Patch Against Release.
constexpr const char* DEPLOY_FULL_BUILD_LABEL = "None - full build";

void draw_muted_text(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void draw_gap(float font_units)
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(font_units)));
}

/// An icon and a line of text, the icon centered on the line.
void draw_icon_line(const char* icon, ImU32 icon_color, const char* text, ImU32 text_color)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float icon_width = ImGui::GetFontSize();
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(min.x + icon_width * 0.5f, min.y + ImGui::GetTextLineHeight() * 0.5f),
                              icon,
                              icon_color);
    ImGui::SetCursorScreenPos(ImVec2(min.x + icon_width + to_pixels(DEPLOY_TITLE_ICON_GAP), min.y));
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    // Long paths wrap under themselves, not under the icon.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void draw_header()
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(DEPLOY_TITLE_SCALE);
    draw_icon_line(ICON_MDI_PACKAGE_VARIANT,
                   ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()),
                   "Deploy Project",
                   ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    draw_muted_text("Builds the game for this platform into a folder.");
}

void draw_section_title(const char* title)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    draw_gap(DEPLOY_LINE_GAP);
}

/// The close button in the top right corner of the window.
auto draw_close_button() -> bool
{
    const float inset = to_pixels(DEPLOY_CLOSE_INSET);
    const float side = ImGui::GetFrameHeight();
    const ImVec2 window_min = ImGui::GetWindowPos();
    const ImVec2 min(window_min.x + ImGui::GetWindowWidth() - inset - side, window_min.y + inset);
    return container_widgets::draw_icon_button({"##deploy_close", ICON_MDI_CLOSE, "Close"}, ImRect(min, min + ImVec2(side, side)));
}

auto calc_button_width(const char* label) -> float
{
    return ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * to_pixels(DEPLOY_BUTTON_PADDING_X);
}

/// A section title with a button at the right end of its line, for what acts on the section.
auto draw_section_title_button(const char* title, const char* label, const char* tooltip, bool is_enabled) -> bool
{
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::SameLine();
    const float width = calc_button_width(label);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - width));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(DEPLOY_BUTTON_ROUNDING));
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label, ImVec2(width, 0.0f));
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::SetItemTooltipEx("%s", tooltip);
    draw_gap(DEPLOY_LINE_GAP);
    return is_pressed;
}

/// Bytes of the files under `folder`.
auto get_folder_size(const fs::path& folder) -> std::uint64_t
{
    std::uint64_t size = 0;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        const auto file_size = it->is_regular_file(ec) ? it->file_size(ec) : 0;
        size += ec ? 0 : file_size;
    }
    return size;
}

/// The deploy settings' properties `names`, in rows as the inspector draws them; saved once an edit
/// is finished.
void inspect_deploy_properties(rtti::context& ctx, std::initializer_list<entt::hashed_string> names)
{
    auto& pm = ctx.get_cached<project_manager>();
    entt::meta_any object = entt::forward_as_meta(pm.get_deploy_settings());
    const auto type = entt::resolve<deploy_settings>();
    inspect_result result;
    for(const auto& name : names)
    {
        if(const auto prop = type.data(name))
        {
            result |= inspect_property(ctx, object, make_proxy(object), prop);
        }
    }
    if(result.edit_finished)
    {
        pm.save_deploy_settings();
    }
}

/// A button of the window: the primary one takes the accent color.
auto draw_button(const char* label, bool is_primary, bool is_enabled) -> bool
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(DEPLOY_BUTTON_ROUNDING));
    if(is_primary)
    {
        const ImVec4 accent = imgui_style::get_accent_color();
        const ImVec4 active(accent.x * DEPLOY_ACCENT_ACTIVE_SHADE,
                            accent.y * DEPLOY_ACCENT_ACTIVE_SHADE,
                            accent.z * DEPLOY_ACCENT_ACTIVE_SHADE,
                            1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    }
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label, ImVec2(calc_button_width(label), to_pixels(DEPLOY_BUTTON_HEIGHT)));
    ImGui::EndDisabled();
    if(is_primary)
    {
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar();
    return is_pressed;
}

auto get_step_name(hpp::string_view job_name) -> std::string
{
    if(job_name.starts_with(DEPLOY_JOB_PREFIX))
    {
        job_name.remove_prefix(DEPLOY_JOB_PREFIX.size());
    }
    while(job_name.ends_with('.'))
    {
        job_name.remove_suffix(1);
    }
    return std::string(job_name);
}

/// The error of a finished step, empty when it succeeded: a ready future rethrows its job's exception.
auto get_step_error(const tpp::shared_future<void>& job) -> std::string
{
    if(!job.is_ready())
    {
        return {};
    }
    try
    {
        job.get();
    }
    catch(const std::exception& ex)
    {
        return ex.what();
    }
    catch(...)
    {
        return "unknown error";
    }
    return {};
}
} // namespace

deploy_panel::deploy_panel(imgui_panels* parent) : parent_(parent)
{
}

void deploy_panel::show(bool s)
{
    show_request_ = s;

    if(!s)
    {
        deploy_jobs_.clear();
    }
}

void deploy_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        ImGui::OpenPopup(name);
        show_request_ = false;
        refresh_releases();
        release_message_.clear();
    }

    const float width = to_pixels(DEPLOY_WIDTH);
    // Taller than the editor's work area, the window scrolls instead of losing its buttons off screen.
    const float max_height = ImGui::GetMainViewport()->WorkSize.y - 2.0f * to_pixels(DEPLOY_SCREEN_MARGIN);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, ImMax(max_height, 0.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, screen_card::get_padding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, to_pixels(DEPLOY_ROUNDING));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    const bool is_open = ImGui::BeginPopupModal(name, nullptr, flags);
    // Only the window itself: tooltips and pickers opened from it keep the theme's padding.
    ImGui::PopStyleVar(2);
    if(!is_open)
    {
        return;
    }

    draw_ui(ctx);

    const bool is_escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive();
    if(draw_close_button() || is_escape_pressed)
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

auto deploy_panel::get_progress() const -> float
{
    if(deploy_jobs_.empty())
    {
        return 1.0f;
    }

    size_t ready = 0;
    for(const auto& kvp : deploy_jobs_)
    {
        if(kvp.second.is_ready())
        {
            ready++;
        }
    }

    return float(ready) / float(deploy_jobs_.size());
}

auto deploy_panel::is_deploying() const -> bool
{
    float progress = get_progress();
    bool is_in_progress = progress < 0.99f;
    return is_in_progress;
}

auto deploy_panel::start_deploy(rtti::context& ctx, const deploy_settings& params) -> bool
{
    if(is_deploying() || !editor_actions::can_deploy_project(ctx, params))
    {
        return false;
    }
    deploy_jobs_ = editor_actions::deploy_project(ctx, params);
    return true;
}

auto deploy_panel::get_jobs() const -> const std::map<std::string, tpp::shared_future<void>>&
{
    return deploy_jobs_;
}

void deploy_panel::deploy_and_run(rtti::context& ctx, const deploy_settings& params)
{
    if(!start_deploy(ctx, params))
    {
        show(true);
    }
}

void deploy_panel::draw_ui(rtti::context& ctx)
{
    draw_header();
    draw_gap(DEPLOY_SECTION_GAP);
    draw_tabs(ctx);
    draw_gap(DEPLOY_LINE_GAP);
    ImGui::Separator();
    draw_gap(DEPLOY_LINE_GAP);
    draw_status(ctx);
    draw_gap(DEPLOY_SECTION_GAP);
    draw_actions(ctx);
}

void deploy_panel::draw_tabs(rtti::context& ctx)
{
    struct tab
    {
        const char* label;
        tab_drawer draw;
    };
    static constexpr tab tabs[] = {
        {ICON_MDI_HAMMER_WRENCH " Build", &deploy_panel::draw_build_tab},
        {ICON_MDI_PACKAGE_VARIANT " Content", &deploy_panel::draw_content_tab},
        {ICON_MDI_TAG_MULTIPLE_OUTLINE " Releases", &deploy_panel::draw_releases_tab},
    };
    if(!ImGui::BeginTabBar("##deploy_tabs"))
    {
        return;
    }
    for(const auto& item : tabs)
    {
        if(ImGui::BeginTabItem(item.label))
        {
            draw_tab_body(ctx, item.draw);
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

void deploy_panel::draw_tab_body(rtti::context& ctx, tab_drawer draw)
{
    const float min_height = to_pixels(DEPLOY_BODY_MIN_HEIGHT);
    const float max_height = ImMax(min_height, ImGui::GetMainViewport()->WorkSize.y * DEPLOY_BODY_MAX_SHARE);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, min_height), ImVec2(FLT_MAX, max_height));
    if(ImGui::BeginChild("##deploy_tab_body",
                         ImVec2(0.0f, 0.0f),
                         ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        // What is being deployed does not change under a running deploy.
        ImGui::BeginDisabled(is_deploying());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, DEPLOY_FIELD_COLOR);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, DEPLOY_FIELD_HOVERED_COLOR);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, DEPLOY_FIELD_ACTIVE_COLOR);
        (this->*draw)(ctx);
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
}

/// What the game is and where it is built.
void deploy_panel::draw_build_tab(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();
    draw_section_title("Product");
    if(inspect(ctx, settings.app).edit_finished)
    {
        pm.save_project_settings(ctx);
    }
    if(inspect(ctx, settings.standalone).edit_finished)
    {
        pm.save_project_settings(ctx);
    }
    draw_gap(DEPLOY_SECTION_GAP);
    draw_section_title("Output");
    inspect_deploy_properties(ctx, {"deploy_location"_hs, "deploy_and_run"_hs});
}

/// What the build ships: the assets and how they are stored, the binaries beside them.
void deploy_panel::draw_content_tab(rtti::context& ctx)
{
    draw_section_title("Assets");
    inspect_deploy_properties(ctx, {"deploy_packed_assets"_hs, "deploy_cooked_assets"_hs});
    draw_gap(DEPLOY_SECTION_GAP);
    draw_section_title("Binaries");
    inspect_deploy_properties(ctx, {"deploy_dependencies"_hs, "deploy_debug_symbols"_hs});
}

/// The release a deploy patches and the releases kept to patch.
void deploy_panel::draw_releases_tab(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    const auto& release = pm.get_deploy_settings().deploy_patch_release;
    draw_section_title("Patching");
    if(draw_patch_release_picker(ctx))
    {
        pm.save_deploy_settings();
    }
    draw_gap(DEPLOY_LINE_GAP);
    draw_muted_text(release.empty()
                        ? "Every pack is written whole."
                        : fmt::format("The packs of {} ship unchanged; content_p1.pak holds every change since.", release)
                              .c_str());
    draw_gap(DEPLOY_SECTION_GAP);
    draw_kept_releases(ctx);
}

auto deploy_panel::draw_patch_release_picker(rtti::context& ctx) -> bool
{
    auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    entt::meta_any object = entt::forward_as_meta(params);
    property_layout layout(entt::resolve<deploy_settings>().data("deploy_patch_release"_hs), object);
    auto& value = params.deploy_patch_release;
    const bool is_missing = !value.empty() && std::none_of(releases_.begin(),
                                                           releases_.end(),
                                                           [&](const kept_release& release)
                                                           {
                                                               return release.name == value;
                                                           });
    const std::string preview =
        value.empty() ? std::string(DEPLOY_FULL_BUILD_LABEL) : (is_missing ? ICON_MDI_ALERT " " + value : value);
    bool is_changed = false;
    if(ImGui::BeginCombo("##patch_release", preview.c_str()))
    {
        if(ImGui::IsWindowAppearing())
        {
            refresh_releases();
        }
        if(ImGui::Selectable(DEPLOY_FULL_BUILD_LABEL, value.empty()))
        {
            value.clear();
            is_changed = true;
        }
        for(const auto& release : releases_)
        {
            if(ImGui::Selectable(release.name.c_str(), release.name == value))
            {
                value = release.name;
                is_changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if(is_missing)
    {
        ImGui::SetItemTooltipEx("No release named '%s' is kept.", value.c_str());
    }
    return is_changed;
}

/// The kept releases with their size, and Keep as Release for the build in the deploy folder.
void deploy_panel::draw_kept_releases(rtti::context& ctx)
{
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    fs::error_code ec;
    const bool has_folder = fs::is_directory(params.deploy_location, ec);
    if(draw_section_title_button("Kept Releases",
                                 ICON_MDI_TAG_PLUS_OUTLINE " Keep as Release",
                                 "Keep the build in the deploy folder as a release named by the product version,\n"
                                 "so later deploys can ship patches for it.",
                                 has_folder && !is_deploying()))
    {
        keep_release(ctx);
    }
    if(releases_.empty())
    {
        draw_muted_text("None yet. Deploy a packed build, then keep it to ship patches for it.");
    }
    else if(ImGui::BeginTable("##kept_releases", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupColumn("Release", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
        const ImU32 accent = ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color());
        for(const auto& release : releases_)
        {
            const bool is_patched = release.name == params.deploy_patch_release;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            draw_icon_line(ICON_MDI_TAG_OUTLINE,
                           is_patched ? accent : imgui_style::get_muted_text_color_u32(),
                           is_patched ? fmt::format("{} - patching against it", release.name).c_str() : release.name.c_str(),
                           ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
            ImGui::TextUnformatted(format_bytes(release.size).c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    if(!release_message_.empty())
    {
        draw_gap(DEPLOY_LINE_GAP);
        draw_icon_line(is_release_message_error_ ? ICON_MDI_ALERT_CIRCLE_OUTLINE : ICON_MDI_TAG_OUTLINE,
                       is_release_message_error_ ? DEPLOY_WARNING_COLOR : ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()),
                       release_message_.c_str(),
                       ImGui::GetColorU32(ImGuiCol_Text));
    }
}

/// Keeps the build in the deploy folder as a release named by the product version.
void deploy_panel::keep_release(rtti::context& ctx)
{
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    std::string name;
    std::string error;
    is_release_message_error_ = !editor_actions::keep_release(ctx, params, name, error);
    release_message_ = is_release_message_error_
                           ? error
                           : fmt::format("Kept release {}. Pick it in Patch Against Release to ship patches for it.", name);
    refresh_releases();
}

void deploy_panel::refresh_releases()
{
    releases_.clear();
    for(auto& name : editor_actions::list_releases())
    {
        const auto size = get_folder_size(editor_actions::get_release_folder(name));
        releases_.push_back({std::move(name), size});
    }
}

//-----------------------------------------------------------------------------
/// <summary>
/// The steps and their progress while a deploy runs; otherwise what keeps the project from
/// deploying, the steps of the last deploy, or that it is ready.
/// </summary>
//-----------------------------------------------------------------------------
void deploy_panel::draw_status(rtti::context& ctx)
{
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    const bool is_in_progress = is_deploying();
    if(!is_in_progress)
    {
        const auto problems = editor_actions::get_deploy_problems(ctx, params);
        if(!problems.empty())
        {
            for(const auto& problem : problems)
            {
                draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE, DEPLOY_WARNING_COLOR, problem.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
            }
            return;
        }
    }

    const std::string location = params.deploy_location.generic_string();
    if(deploy_jobs_.empty())
    {
        draw_icon_line(ICON_MDI_CHECK_CIRCLE_OUTLINE,
                       ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()),
                       fmt::format("Ready to deploy to {}", location).c_str(),
                       imgui_style::get_muted_text_color_u32());
        return;
    }

    std::map<std::string, std::string> step_errors;
    for(const auto& [name, job] : deploy_jobs_)
    {
        auto error = get_step_error(job);
        if(!error.empty())
        {
            step_errors.emplace(name, std::move(error));
        }
    }

    const auto step_count = deploy_jobs_.size();
    const auto done_count = static_cast<std::size_t>(get_progress() * static_cast<float>(step_count) + 0.5f);
    std::string summary = fmt::format("Deployed to {}", location);
    if(is_in_progress)
    {
        summary = fmt::format("Deploying to {}", location);
    }
    else if(!step_errors.empty())
    {
        summary = fmt::format("Deploy to {} failed", location);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::TextWrapped("%s", summary.c_str());
    ImGui::PopStyleColor();
    draw_gap(DEPLOY_LINE_GAP);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, imgui_style::get_accent_color());
    ImGui::ProgressBar(get_progress(), ImVec2(-FLT_MIN, 0.0f), fmt::format("{} of {} steps", done_count, step_count).c_str());
    ImGui::PopStyleColor();
    draw_gap(DEPLOY_LINE_GAP);

    const ImU32 done_color = ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color());
    for(const auto& [name, job] : deploy_jobs_)
    {
        const auto error = step_errors.find(name);
        if(error != step_errors.end())
        {
            draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE,
                           DEPLOY_WARNING_COLOR,
                           fmt::format("{}: {}", get_step_name(name), error->second).c_str(),
                           ImGui::GetColorU32(ImGuiCol_Text));
            continue;
        }
        const bool is_done = job.is_ready();
        draw_icon_line(is_done ? ICON_MDI_CHECK_CIRCLE : ICON_MDI_PROGRESS_CLOCK,
                       is_done ? done_color : imgui_style::get_muted_text_color_u32(),
                       get_step_name(name).c_str(),
                       is_done ? ImGui::GetColorU32(ImGuiCol_Text) : imgui_style::get_muted_text_color_u32());
    }
}

/// Open Folder and Deploy, at the right end.
void deploy_panel::draw_actions(rtti::context& ctx)
{
    auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    const bool can_deploy = !is_deploying() && editor_actions::can_deploy_project(ctx, params);
    fs::error_code ec;
    const bool has_folder = fs::is_directory(params.deploy_location, ec);

    const char* open_label = ICON_MDI_FOLDER_OPEN_OUTLINE " Open Folder";
    const char* deploy_label = params.deploy_and_run ? ICON_MDI_PLAY " Deploy and Run" : ICON_MDI_PACKAGE_VARIANT " Deploy";
    const float buttons_width =
        calc_button_width(open_label) + ImGui::GetStyle().ItemSpacing.x + calc_button_width(deploy_label);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - buttons_width));

    if(draw_button(open_label, false, has_folder))
    {
        ImGui::OpenInShell(params.deploy_location.string().c_str());
    }
    ImGui::SetItemTooltipEx("%s", "Show the deploy folder in the file browser");
    ImGui::SameLine();
    if(draw_button(deploy_label, true, can_deploy))
    {
        deploy_jobs_ = editor_actions::deploy_project(ctx, params);
    }
    if(!can_deploy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", is_deploying() ? "A deploy is running." : "See what is missing above.");
    }
}

} // namespace unravel
