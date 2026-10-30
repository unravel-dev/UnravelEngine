#include "deploy_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "../profile_labels.h"
#include "../visualization_widgets.h"
#include "deploy_panel_widgets.h"

#include <deploy/steam.h>
#include <editor/deploy/deploy_android.h>
#include <editor/deploy/deploy_steam.h>
#include <editor/deploy/project_releases.h>
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

#include <algorithm>
#include <initializer_list>

namespace unravel
{
namespace
{
namespace widgets = deploy_widgets;

// Sizes are in units of the font size.
// Wide enough for the Build tab's two columns side by side.
constexpr float DEPLOY_WIDTH = 84.0f;
// Space each side of the line between the Build tab's columns.
constexpr float DEPLOY_COLUMN_GUTTER = 0.75f;
// Space between the step labels in the status row.
constexpr float DEPLOY_STEP_SPACING = 1.25f;
constexpr float DEPLOY_ROUNDING = 0.75f;
constexpr float DEPLOY_TITLE_SCALE = 1.3f;
constexpr float DEPLOY_CLOSE_INSET = 0.6f;
constexpr float DEPLOY_SCREEN_MARGIN = 1.0f;
// Every tab body has the same height, so switching tabs never moves the window: this share of the
// work area, kept between the two limits. Longer content scrolls inside it, and the status and
// buttons under it stay in view.
constexpr float DEPLOY_BODY_SHARE = 0.5f;
constexpr float DEPLOY_BODY_MIN_HEIGHT = 22.0f;
constexpr float DEPLOY_BODY_MAX_HEIGHT = 30.0f;
// The width of the profile popups' fields.
constexpr float PROFILE_POPUP_WIDTH = 22.0f;
// Characters an application id field takes, the terminator included; Android allows far fewer.
constexpr std::size_t ANDROID_APPLICATION_ID_CAPACITY = 256;
/// The icon buttons beside the profile picker: new, rename, delete.
constexpr float PROFILE_PICKER_BUTTONS = 3.0f;
// The empty Patch Against Release.
constexpr const char* DEPLOY_FULL_BUILD_LABEL = "None - full build";
// The edit scope switch: the deploy settings every profile shares, and the deploy profile's overrides.
constexpr int EDIT_SCOPE_SEGMENTS = 2;
constexpr int PROFILE_SCOPE_INDEX = 1;
constexpr const char* SHARED_SCOPE_LABEL = "Shared by All Profiles";
// The profile's segment when "<profile> Only" does not fit in it.
constexpr const char* PROFILE_SCOPE_FALLBACK_LABEL = "This Profile Only";
// Room a segment keeps beside its label for the override count and its edges, in font sizes.
constexpr float EDIT_SCOPE_LABEL_MARGIN = 3.0f;
// The revert item of a deploy setting the deploy profile overrides.
constexpr const char* REVERT_TO_SHARED_LABEL = "Revert to Shared Value";
// The Releases tab picks the release a profile patches, always in the profile: no override the switch shows.
constexpr const char* PATCH_RELEASE_PATH = "deploy_patch_release";
constexpr const char* NEW_PROFILE_POPUP = "##new_profile";
constexpr const char* RENAME_PROFILE_POPUP = "##rename_profile";
constexpr const char* REMOVE_PROFILE_POPUP = "##remove_profile_confirm";
constexpr const char* STEAM_UPLOAD_POPUP = "##steam_upload_confirm";
constexpr const char* STEAM_UPLOAD_LABEL = ICON_MDI_STEAM " Upload to Steam";
// The space between a device's name and its details, in item spacings.
constexpr float DEVICE_DETAILS_GAP = 2.0f;
constexpr const char* CHOOSE_DEVICE_LABEL = "Choose a device";

void draw_header()
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(DEPLOY_TITLE_SCALE);
    widgets::draw_icon_line(ICON_MDI_PACKAGE_VARIANT, widgets::get_accent_u32(), "Deploy Project", ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    widgets::draw_muted_text("Builds the game into a folder, for this computer or an Android device, with the settings of a profile.");
}

/// The close button in the top right corner of the window.
auto draw_close_button() -> bool
{
    const float inset = widgets::to_pixels(DEPLOY_CLOSE_INSET);
    const float side = ImGui::GetFrameHeight();
    const ImVec2 window_min = ImGui::GetWindowPos();
    const ImVec2 min(window_min.x + ImGui::GetWindowWidth() - inset - side, window_min.y + inset);
    return container_widgets::draw_icon_button({"##deploy_close", ICON_MDI_CLOSE, "Close"}, ImRect(min, min + ImVec2(side, side)));
}

/// The popup `id` asking for a confirmation: the question, what it costs, and the confirm and Cancel
/// buttons. True once confirmed.
auto draw_confirmation(const char* id, const std::string& question, const char* consequence, const char* confirm_label) -> bool
{
    if(!ImGui::BeginPopup(id))
    {
        return false;
    }
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(question.c_str());
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, widgets::get_muted_color());
    ImGui::TextUnformatted(consequence);
    ImGui::PopStyleColor();
    widgets::draw_gap(widgets::LINE_GAP);
    const bool is_confirmed = ImGui::Button(confirm_label);
    ImGui::SameLine();
    if(is_confirmed || ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return is_confirmed;
}

/// The profile's Steam settings in rows, with what keeps them from uploading once an app is set.
auto draw_steam_settings(rtti::context& ctx, steam_deploy_settings& steam) -> inspect_result
{
    ImGui::SeparatorText("Steam");
    widgets::draw_muted_text("Upload to Steam sends the build in the profile's folder to this depot with steamcmd, "
                             "which Editor Settings > External Tools names.");
    entt::meta_any object = entt::forward_as_meta(steam);
    const auto type = entt::resolve<steam_deploy_settings>();
    inspect_result result;
    {
        const inspect_without_undo_scope no_undo;
        for(const auto& name : {"app_id"_hs, "depot_id"_hs, "account"_hs, "branch"_hs})
        {
            result |= inspect_property(ctx, object, make_proxy(object), type.data(name));
        }
    }
    const auto problem = steam.app_id == 0 ? std::string{} : deployment::check_steam_settings(steam);
    if(!problem.empty())
    {
        widgets::draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE, widgets::WARNING_COLOR, problem.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
    }
    return result;
}

/// What the upload's confirmation says it does.
auto describe_steam_upload(const deploy_settings& params) -> std::string
{
    const auto& steam = params.desktop.steam;
    return fmt::format("The build in {} goes to app {}, depot {}, as {}.\n{}",
                       get_output_location(params).generic_string(),
                       steam.app_id,
                       steam.depot_id,
                       steam.account,
                       steam.branch.empty() ? std::string("Set it live on the Steamworks website.")
                                            : fmt::format("It is set live on branch {}.", steam.branch));
}

/// A square icon button that takes its place in the layout.
auto draw_inline_icon_button(const char* id, const char* icon, const char* tooltip, bool is_enabled, bool is_destructive = false) -> bool
{
    const float side = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, min + ImVec2(side, side));
    ImGui::ItemSize(rect);
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = container_widgets::draw_icon_button({id, icon, tooltip, is_destructive}, rect);
    ImGui::EndDisabled();
    return is_pressed;
}

/// The delete button of a kept release, which asks first. True once the deletion is confirmed.
auto draw_release_remove_button(const std::string& name, bool is_patched, bool is_enabled) -> bool
{
    if(draw_inline_icon_button("##remove_release", ICON_MDI_DELETE_OUTLINE, "Delete this release.", is_enabled, true))
    {
        ImGui::OpenPopup("##remove_release_confirm");
    }
    return draw_confirmation("##remove_release_confirm",
                             fmt::format("Delete release {}?", name),
                             is_patched ? "Its packs are deleted, and deploys make full builds again."
                                        : "Its packs are deleted. Patches can no longer be made for it.",
                             ICON_MDI_DELETE_OUTLINE " Delete");
}

/// A button of the device row, which starts a new line when the row has no room left for it. The tooltip
/// says what it does, or why it cannot run now.
auto draw_device_action(const char* label, const std::string& tooltip, bool is_enabled, bool is_first) -> bool
{
    const float width = ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * ImGui::GetStyle().FramePadding.x;
    if(!is_first)
    {
        ImGui::SameLine();
        if(ImGui::GetContentRegionAvail().x < width)
        {
            ImGui::NewLine();
        }
    }
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
    return is_pressed;
}

/// A deploy folder as the window names it: relative to the project when it lies in it, which reads
/// shorter and the same on every machine.
auto describe_folder(const fs::path& folder) -> std::string
{
    const auto shown = fs::make_relative_if_inside(folder, deployment::get_project_root());
    return shown.is_relative() ? shown.generic_string() + " in the project" : shown.generic_string();
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

/// The text of a name field, without the zeros after it.
template<std::size_t Capacity>
auto get_field_text(const std::array<char, Capacity>& field) -> std::string
{
    return std::string(field.data());
}

template<std::size_t Capacity>
void set_field_text(std::array<char, Capacity>& field, const std::string& text)
{
    field.fill('\0');
    std::copy_n(text.begin(), std::min(text.size(), field.size() - 1), field.begin());
}

/// A row of a profile popup: its label, then the field the caller draws next, at the popup's field width.
void begin_popup_row(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(widgets::to_pixels(PROFILE_POPUP_WIDTH));
}

/// The deploy profile's overrides the Build and Content tabs edit.
auto count_switched_overrides(const settings_profile& profile) -> int
{
    const auto& paths = profile.deploy.get_paths();
    return static_cast<int>(std::count_if(paths.begin(),
                                          paths.end(),
                                          [](const std::string& path)
                                          {
                                              return !serialization::is_path_within(path, PATCH_RELEASE_PATH);
                                          }));
}

/// A deploy step's name with its state: done, failed or still running.
void draw_step_label(const deployment::deploy_job& step)
{
    const bool has_failed = !deployment::get_step_error(step.job).empty();
    const bool is_done = step.job.is_ready();
    const char* icon = has_failed ? ICON_MDI_ALERT_CIRCLE_OUTLINE : (is_done ? ICON_MDI_CHECK_CIRCLE : ICON_MDI_PROGRESS_CLOCK);
    const ImU32 icon_color = has_failed ? widgets::WARNING_COLOR : (is_done ? widgets::get_accent_u32() : widgets::get_muted_color());
    widgets::draw_icon_line(icon,
                            icon_color,
                            step.name.c_str(),
                            is_done ? ImGui::GetColorU32(ImGuiCol_Text) : widgets::get_muted_color());
}

/// The steps in one row, in the order they run, wrapping when the window is too narrow; the reason of
/// each failed step under them.
void draw_step_row(const deployment::deploy_jobs& steps)
{
    const float spacing = widgets::to_pixels(DEPLOY_STEP_SPACING);
    const float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    for(std::size_t i = 0; i < steps.size(); ++i)
    {
        const float width = ImGui::GetFontSize() + widgets::to_pixels(widgets::ICON_GAP) +
                            ImGui::CalcTextSize(steps[i].name.c_str()).x;
        if(i > 0)
        {
            ImGui::SameLine(0.0f, spacing);
            if(ImGui::GetCursorPosX() + width > right_edge)
            {
                ImGui::NewLine();
            }
        }
        draw_step_label(steps[i]);
    }
    for(const auto& step : steps)
    {
        const auto error = deployment::get_step_error(step.job);
        if(!error.empty())
        {
            widgets::draw_gap(widgets::LINE_GAP);
            widgets::draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE,
                                    widgets::WARNING_COLOR,
                                    fmt::format("{}: {}", step.name, error).c_str(),
                                    ImGui::GetColorU32(ImGuiCol_Text));
        }
    }
}

/// A line under a section: what the last action did, or why it failed.
void draw_message(const std::string& message, bool is_error)
{
    if(message.empty())
    {
        return;
    }
    widgets::draw_gap(widgets::LINE_GAP);
    widgets::draw_icon_line(is_error ? ICON_MDI_ALERT_CIRCLE_OUTLINE : ICON_MDI_CHECK_CIRCLE_OUTLINE,
                            is_error ? widgets::WARNING_COLOR : widgets::get_accent_u32(),
                            message.c_str(),
                            ImGui::GetColorU32(ImGuiCol_Text));
}

/// Draws text from `pos`, cut with an ellipsis at `max_x`. The end is given, so ImGui draws the text as it is
/// and never reads "##" in it as the start of an id.
void draw_plain_text(ImDrawList* draw_list, const ImVec2& pos, float max_x, const std::string& text)
{
    const char* text_end = text.c_str() + text.size();
    const ImVec2 size = ImGui::CalcTextSize(text.c_str(), text_end);
    ImGui::RenderTextEllipsis(draw_list, pos, ImVec2(max_x, pos.y + size.y), max_x, text.c_str(), text_end, &size);
}

/// A device as the device list shows it: its name, then its details dimmed. The name is the one the phone's
/// owner typed, so it is drawn as text, never passed as an ImGui label.
void draw_device_text(ImDrawList* draw_list, const ImVec2& pos, float max_x, const deployment::android_device& device)
{
    const auto name = device.get_name();
    draw_plain_text(draw_list, pos, max_x, name);
    const float details_x = pos.x + ImGui::CalcTextSize(name.c_str(), name.c_str() + name.size()).x +
                            ImGui::GetStyle().ItemSpacing.x * DEVICE_DETAILS_GAP;
    if(details_x >= max_x)
    {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    draw_plain_text(draw_list, ImVec2(details_x, pos.y), max_x, device.get_details());
    ImGui::PopStyleColor();
}

/// What tells a device apart when its name does not: two phones of one model can carry the same name.
auto make_device_tooltip(const deployment::android_device& device) -> std::string
{
    if(device.hardware_serial.empty())
    {
        return fmt::format("adb reaches it as {}.", device.serial);
    }
    return fmt::format("Serial number {}. adb reaches it as {}.", device.hardware_serial, device.serial);
}

/**
 * @brief Draws the closed device list's text: the chosen device, the serial of a chosen device that is not
 * connected, or the prompt. Call right after the combo opened with ImGuiComboFlags_CustomPreview.
 */
void draw_device_preview(const deployment::android_device* chosen, const std::string& chosen_serial)
{
    if(!ImGui::BeginComboPreview())
    {
        return;
    }
    const ImRect& rect = ImGui::GetCurrentContext()->ComboPreviewData.PreviewRect;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos(rect.Min.x + style.FramePadding.x, rect.Min.y + style.FramePadding.y);
    const float max_x = rect.Max.x - style.FramePadding.x;
    auto* draw_list = ImGui::GetWindowDrawList();
    if(chosen != nullptr)
    {
        draw_device_text(draw_list, pos, max_x, *chosen);
    }
    else if(!chosen_serial.empty())
    {
        draw_plain_text(draw_list, pos, max_x, ICON_MDI_ALERT " " + chosen_serial);
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        draw_plain_text(draw_list, pos, max_x, CHOOSE_DEVICE_LABEL);
        ImGui::PopStyleColor();
    }
    ImGui::EndComboPreview();
}
} // namespace

void deploy_panel::profile_name_field::check(const deployment::project_profiles& profiles, const std::string& renamed)
{
    const auto name = get_field_text(text);
    if(name == checked)
    {
        return;
    }
    checked = name;
    error = name.empty() ? std::string{} : profiles.check_new_name(name, renamed).error;
}

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
    else
    {
        has_listed_android_devices_ = false;
    }
}

void deploy_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    // A Deploy and Run that could not replace the game on the device waits for a choice here.
    const auto conflict_count = deployment::get_android_install_conflict_count();
    if(conflict_count != seen_install_conflicts_)
    {
        seen_install_conflicts_ = conflict_count;
        if(!deployment::get_android_install_conflict().empty())
        {
            show(true);
        }
    }
    if(show_request_)
    {
        ImGui::OpenPopup(name);
        show_request_ = false;
        release_message_.clear();
        profile_message_.clear();
        content_view_.invalidate();
        problems_.invalidate();
    }

    const ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;
    const float margin = 2.0f * widgets::to_pixels(DEPLOY_SCREEN_MARGIN);
    const float width = ImMin(widgets::to_pixels(DEPLOY_WIDTH), ImMax(work_size.x - margin, 0.0f));
    // Taller than the editor's work area, the window scrolls instead of losing its buttons off screen.
    const float max_height = ImMax(work_size.y - margin, 0.0f);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, max_height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, screen_card::get_padding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, widgets::to_pixels(DEPLOY_ROUNDING));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    const bool is_open = ImGui::BeginPopupModal(name, nullptr, flags);
    // Only the window itself: tooltips and pickers opened from it keep the theme's padding.
    ImGui::PopStyleVar(2);
    if(!is_open)
    {
        return;
    }

    settings_editor_.sync(ctx);
    draw_ui(ctx);

    const bool is_escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive();
    if(draw_close_button() || is_escape_pressed)
    {
        close_window();
    }
    ImGui::EndPopup();
}

void deploy_panel::close_window()
{
    ImGui::CloseCurrentPopup();
    if(!is_deploying())
    {
        deploy_jobs_.clear();
    }
}

auto deploy_panel::get_progress() const -> float
{
    if(deploy_jobs_.empty())
    {
        return 1.0f;
    }
    const auto ready = std::count_if(deploy_jobs_.begin(),
                                     deploy_jobs_.end(),
                                     [](const auto& step)
                                     {
                                         return step.job.is_ready();
                                     });
    return float(ready) / float(deploy_jobs_.size());
}

auto deploy_panel::is_deploying() const -> bool
{
    return std::any_of(deploy_jobs_.begin(),
                       deploy_jobs_.end(),
                       [](const auto& step)
                       {
                           return !step.job.is_ready();
                       });
}

auto deploy_panel::start_deploy(rtti::context& ctx, const deploy_settings& params) -> bool
{
    if(is_deploying() || !deployment::can_deploy(ctx, params))
    {
        return false;
    }
    deploy_jobs_ = deployment::deploy_project(ctx, params);
    deployed_location_ = get_output_location(params);
    is_steam_upload_ = false;
    return true;
}

auto deploy_panel::run_last_android_build(rtti::context& ctx, const deploy_settings& params) -> bool
{
    if(is_deploying())
    {
        return false;
    }
    deploy_jobs_ = deployment::run_last_android_build(ctx, params);
    deployed_location_ = get_output_location(params);
    is_steam_upload_ = false;
    return true;
}

auto deploy_panel::start_steam_upload(rtti::context& ctx, const deploy_settings& params) -> bool
{
    if(is_deploying() || !deployment::get_steam_upload_problem(ctx, params).empty())
    {
        return false;
    }
    deploy_jobs_ = deployment::upload_to_steam(ctx, params);
    deployed_location_ = get_output_location(params);
    is_steam_upload_ = true;
    return true;
}

auto deploy_panel::get_jobs() const -> const deployment::deploy_jobs&
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
    widgets::draw_gap(widgets::SECTION_GAP);
    draw_tabs(ctx);
    widgets::draw_gap(widgets::LINE_GAP);
    ImGui::Separator();
    widgets::draw_gap(widgets::LINE_GAP);
    draw_status(ctx);
    widgets::draw_gap(widgets::SECTION_GAP);
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
    const float height = ImClamp(ImGui::GetMainViewport()->WorkSize.y * DEPLOY_BODY_SHARE,
                                 widgets::to_pixels(DEPLOY_BODY_MIN_HEIGHT),
                                 widgets::to_pixels(DEPLOY_BODY_MAX_HEIGHT));
    if(ImGui::BeginChild("##deploy_tab_body", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        // What is being deployed does not change under a running deploy.
        ImGui::BeginDisabled(is_deploying());
        widgets::push_field_colors();
        (this->*draw)(ctx);
        widgets::pop_field_colors();
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
}

/// Two columns: what every profile shares on the left - the product, where builds go, how files are
/// stored - and the profile, with its platform's own settings, on the right.
void deploy_panel::draw_build_tab(rtti::context& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(widgets::to_pixels(DEPLOY_COLUMN_GUTTER), 0.0f));
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame;
    if(ImGui::BeginTable("##build_columns", 2, flags))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        draw_common_settings(ctx);
        ImGui::TableSetColumnIndex(1);
        draw_profile_section(ctx);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

auto deploy_panel::make_deploy_marks(rtti::context& ctx) -> property_override_marks
{
    auto& pm = ctx.get_cached<project_manager>();
    const std::string profile = settings_editor_.get_profile();
    property_override_marks marks;
    if(pm.get_profiles().find(profile) == nullptr)
    {
        return marks;
    }
    marks.get_mark = [&pm, profile](const std::string& path)
    {
        const auto* found = pm.get_profiles().find(profile);
        return found != nullptr ? get_override_mark(found->deploy, path) : override_mark::none;
    };
    marks.revert = [this, &ctx](const std::string& path)
    {
        settings_editor_.revert(ctx, path);
        problems_.invalidate();
        content_view_.invalidate();
    };
    marks.contains_note = fmt::format("Profile {} overrides settings in this group.", profile);
    if(settings_editor_.is_profile_mode())
    {
        marks.revert_label = REVERT_TO_SHARED_LABEL;
        marks.overridden_note = fmt::format("Profile {} overrides this setting.", profile);
        return marks;
    }
    // Edits of the shared settings do not reach the profile where it overrides them.
    marks.style = override_style::shadowing;
    marks.revert_label = fmt::format("Revert {} to Shared Value", profile);
    marks.overridden_note = fmt::format("Profile {} overrides this setting, so its deploys use their own value.", profile);
    return marks;
}

void deploy_panel::inspect_deploy_properties(rtti::context& ctx, std::initializer_list<entt::hashed_string> names)
{
    auto& shown = settings_editor_.get();
    entt::meta_any object = entt::forward_as_meta(shown);
    const auto type = entt::resolve<deploy_settings>();
    inspect_result result;
    {
        // The shown settings are a copy, taken again every frame: an undo step could not reach them.
        const inspect_without_undo_scope no_undo;
        auto marks = make_deploy_marks(ctx);
        hpp::optional<property_override_scope> marked;
        if(marks.get_mark)
        {
            marked.emplace(ctx, std::string{}, std::move(marks));
        }
        for(const auto& name : names)
        {
            if(const auto prop = type.data(name))
            {
                result |= inspect_property(ctx, object, make_proxy(object), prop);
            }
        }
    }
    if(result.edit_finished)
    {
        settings_editor_.commit(ctx);
        problems_.invalidate();
        content_view_.invalidate();
    }
}

auto deploy_panel::make_product_marks(rtti::context& ctx) -> property_override_marks
{
    auto& pm = ctx.get_cached<project_manager>();
    const std::string profile = settings_editor_.get_profile();
    property_override_marks marks;
    if(pm.get_profiles().find(profile) == nullptr)
    {
        return marks;
    }
    marks.get_mark = [&pm, profile](const std::string& path)
    {
        const auto* found = pm.get_profiles().find(profile);
        return found != nullptr ? get_override_mark(found->settings, path) : override_mark::none;
    };
    marks.revert = [this, &ctx, &pm, profile](const std::string& path)
    {
        product_error_ = pm.revert_profile_settings(ctx, profile, path).error;
        problems_.invalidate();
        content_view_.invalidate();
    };
    marks.style = override_style::shadowing;
    marks.revert_label = fmt::format("Revert {} to Project Settings", profile);
    marks.overridden_note = fmt::format("Profile {} overrides this setting, so its deploys use their own value.", profile);
    marks.contains_note = fmt::format("Profile {} overrides settings in this group.", profile);
    return marks;
}

void deploy_panel::draw_product_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();
    const auto inspect_marked = [this, &ctx](const char* root, auto& object)
    {
        auto marks = make_product_marks(ctx);
        hpp::optional<property_override_scope> marked;
        if(marks.get_mark)
        {
            marked.emplace(ctx, root, std::move(marks));
        }
        return inspect(ctx, object).edit_finished;
    };
    bool is_edited = inspect_marked("app", settings.app);
    is_edited |= inspect_marked("standalone", settings.standalone);
    if(is_edited)
    {
        pm.save_project_settings(ctx);
        product_error_.clear();
    }
    const auto* profile = pm.get_profiles().find(settings_editor_.get_profile());
    if(profile != nullptr && (profile->settings.is_related("app") || profile->settings.is_related("standalone")))
    {
        widgets::draw_muted_text(fmt::format("Profile {} deploys with its own values of the highlighted settings. "
                                             "Right-click one to revert it.",
                                             profile->name)
                                     .c_str());
    }
    draw_message(product_error_, true);
}

/// The product and the deploy folder every profile shares, then the deploy settings: running the game and
/// packaging, edited for every profile or as the deploy profile's overrides.
void deploy_panel::draw_common_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    widgets::draw_section_title("Product");
    draw_product_settings(ctx);
    widgets::draw_gap(widgets::SECTION_GAP);
    widgets::draw_section_title("Output");
    {
        // Every profile builds under the one location: always the shared setting.
        auto& shared = pm.get_deploy_settings();
        entt::meta_any object = entt::forward_as_meta(shared);
        const auto prop = entt::resolve<deploy_settings>().data("deploy_location"_hs);
        if(inspect_property(ctx, object, make_proxy(object), prop).edit_finished)
        {
            pm.save_deploy_settings();
            problems_.invalidate();
        }
    }
    const auto request = pm.get_deploy_request();
    if(request.deploy_location.empty())
    {
        draw_missing_location_notice(ctx);
    }
    else
    {
        widgets::draw_muted_text(
            fmt::format("{} builds go to {}.", request.profile, describe_folder(get_output_location(request))).c_str());
    }
    widgets::draw_gap(widgets::SECTION_GAP);
    widgets::draw_section_title("Deploy Settings");
    draw_edit_target(ctx);
    widgets::draw_gap(widgets::LINE_GAP);
    inspect_deploy_properties(ctx, {"deploy_and_run"_hs});
    if(settings_editor_.get().deploy_and_run)
    {
        inspect_deploy_properties(ctx, {"deploy_run_arguments"_hs});
    }
    if(settings_editor_.get().deploy_target != deploy_platform::android)
    {
        inspect_deploy_properties(ctx, {"deploy_dependencies"_hs});
    }
    ImGui::SeparatorText("Packaging");
    inspect_deploy_properties(ctx, {"deploy_packed_assets"_hs, "deploy_cooked_assets"_hs, "deploy_debug_symbols"_hs});
    draw_message(settings_editor_.get_error(), true);
}

/// One segment per scope, the profile's counting its overrides, and a line under them saying where changes go.
void deploy_panel::draw_edit_target(rtti::context& ctx)
{
    const auto& profile = settings_editor_.get_profile();
    const auto* found = ctx.get_cached<project_manager>().get_profiles().find(profile);
    const int override_count = found != nullptr ? count_switched_overrides(*found) : 0;
    const float width = ImGui::GetContentRegionAvail().x;
    std::string profile_label = fmt::format("{} Only", profile);
    const float label_room = width / float(EDIT_SCOPE_SEGMENTS) - widgets::to_pixels(EDIT_SCOPE_LABEL_MARGIN);
    if(ImGui::CalcTextSize(profile_label.c_str()).x > label_room)
    {
        profile_label = PROFILE_SCOPE_FALLBACK_LABEL;
    }
    const char* labels[EDIT_SCOPE_SEGMENTS] = {SHARED_SCOPE_LABEL, profile_label.c_str()};
    const int badges[EDIT_SCOPE_SEGMENTS] = {0, override_count};
    int index = settings_editor_.is_profile_mode() ? PROFILE_SCOPE_INDEX : 0;
    if(visualization_widgets::draw_segmented("##edit_scope", labels, badges, EDIT_SCOPE_SEGMENTS, width, index))
    {
        settings_editor_.set_profile_mode(ctx, index == PROFILE_SCOPE_INDEX);
        content_view_.invalidate();
    }
    widgets::draw_gap(widgets::LINE_GAP);
    std::string note;
    if(settings_editor_.is_profile_mode())
    {
        note = fmt::format("Changes are saved in profile {} only, as overrides of the shared settings. Overridden settings "
                           "are bold, and their context menu reverts them.",
                           profile);
    }
    else
    {
        note = "Every profile deploys with these settings unless it overrides them.";
        if(override_count > 0)
        {
            note += fmt::format(" Profile {} overrides {} of them.", profile, override_count);
        }
    }
    widgets::draw_muted_text(note.c_str());
}

/// Where the deploy folder line goes once one is set: that none is, and a button that sets the project's own.
void deploy_panel::draw_missing_location_notice(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    const std::string label = fmt::format(ICON_MDI_FOLDER_PLUS_OUTLINE " Use {}", DEFAULT_DEPLOY_LOCATION);
    // The icon and the text sit on the button's line, centered on its frame.
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float icon_width = ImGui::GetFontSize();
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(min.x + icon_width * 0.5f, min.y + ImGui::GetFrameHeight() * 0.5f),
                              ICON_MDI_ALERT_CIRCLE_OUTLINE,
                              widgets::WARNING_COLOR);
    ImGui::SetCursorScreenPos(ImVec2(min.x + icon_width + widgets::to_pixels(widgets::ICON_GAP), min.y));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, widgets::get_muted_color());
    ImGui::TextUnformatted("No deploy folder is set.");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if(ImGui::Button(label.c_str()))
    {
        pm.get_deploy_settings().deploy_location = DEFAULT_DEPLOY_LOCATION;
        pm.save_deploy_settings();
        problems_.invalidate();
    }
    ImGui::SetItemTooltipEx("Deploy into %s in the project folder, which the deploy creates. Each profile builds "
                            "into a folder of its own in it.",
                            DEFAULT_DEPLOY_LOCATION);
}

/// The deploy profile and its platform's settings.
void deploy_panel::draw_profile_section(rtti::context& ctx)
{
    widgets::draw_section_title("Profile");
    draw_profile_picker(ctx);
    draw_message(profile_message_, is_profile_message_error_);
    widgets::draw_gap(widgets::LINE_GAP);
    if(settings_editor_.get().deploy_target == deploy_platform::android)
    {
        draw_android_section(ctx);
    }
    else
    {
        draw_desktop_section(ctx);
    }
}

void deploy_panel::draw_profile_picker(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    const auto& profiles = pm.get_profiles();
    const auto& current = pm.get_deploy_profile();
    const auto* selected = profiles.find(current);
    const float side = ImGui::GetFrameHeight();
    const float buttons_width = PROFILE_PICKER_BUTTONS * (side + ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::GetContentRegionAvail().x - buttons_width));
    const std::string preview = selected != nullptr ? profile_labels::get_label(*selected) : current;
    const bool is_open = ImGui::BeginCombo("##deploy_profile", preview.c_str());
    ImGui::SetItemTooltipEx("%s",
                            "The profile the deploy builds: a platform, and the settings it changes from the project's.\n"
                            "Each profile builds into a folder named after it in the deploy folder.");
    if(is_open)
    {
        for(const auto& profile : profiles.get_all())
        {
            if(ImGui::Selectable(profile_labels::get_label(profile).c_str(), profile.name == current) && profile.name != current)
            {
                set_profile_message(pm.set_deploy_profile(profile.name), {});
                settings_editor_.sync(ctx);
                problems_.invalidate();
                content_view_.invalidate();
                refresh_releases();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if(draw_inline_icon_button("##new_profile_button", ICON_MDI_PLUS, "Create a profile.", true))
    {
        new_profile_name_ = {};
        new_profile_platform_ = selected != nullptr ? selected->platform : target_platform::desktop;
        new_profile_source_.clear();
        ImGui::OpenPopup(NEW_PROFILE_POPUP);
    }
    ImGui::SameLine();
    if(draw_inline_icon_button("##rename_profile_button", ICON_MDI_PENCIL_OUTLINE, "Rename this profile.", selected != nullptr))
    {
        rename_profile_name_ = {};
        set_field_text(rename_profile_name_.text, current);
        ImGui::OpenPopup(RENAME_PROFILE_POPUP);
    }
    ImGui::SameLine();
    const bool can_remove = selected != nullptr && profiles.get_all().size() > 1;
    if(draw_inline_icon_button("##remove_profile_button",
                               ICON_MDI_DELETE_OUTLINE,
                               can_remove ? "Delete this profile." : "A project keeps at least one profile.",
                               can_remove,
                               true))
    {
        ImGui::OpenPopup(REMOVE_PROFILE_POPUP);
    }
    draw_new_profile_popup(ctx);
    draw_rename_profile_popup(ctx);
    if(draw_confirmation(REMOVE_PROFILE_POPUP,
                         fmt::format("Delete profile {}?", current),
                         "Its settings, its kept releases and its texture cache are deleted. Its builds in the deploy "
                         "folder stay.",
                         ICON_MDI_DELETE_OUTLINE " Delete"))
    {
        const std::string removed = current;
        set_profile_message(pm.remove_profile(ctx, removed), fmt::format("Deleted profile {}.", removed));
        settings_editor_.sync(ctx);
        problems_.invalidate();
        content_view_.invalidate();
        refresh_releases();
    }
}

void deploy_panel::draw_new_profile_popup(rtti::context& ctx)
{
    if(!ImGui::BeginPopup(NEW_PROFILE_POPUP))
    {
        return;
    }
    auto& pm = ctx.get_cached<project_manager>();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted("New Profile");
    ImGui::PopFont();
    widgets::draw_gap(widgets::LINE_GAP);
    bool is_entered = false;
    if(ImGui::BeginTable("##new_profile_fields", 2, ImGuiTableFlags_SizingFixedFit))
    {
        begin_popup_row("Name");
        if(ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        is_entered = ImGui::InputTextWithHint("##new_profile_name",
                                              "Such as Android or Demo",
                                              new_profile_name_.text.data(),
                                              new_profile_name_.text.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        begin_popup_row("Platform");
        for(const auto platform : {target_platform::desktop, target_platform::android})
        {
            if(platform != target_platform::desktop)
            {
                ImGui::SameLine();
            }
            const auto label = fmt::format("{} {}", profile_labels::get_platform_icon(platform), profile_labels::get_platform_name(platform));
            if(ImGui::RadioButton(label.c_str(), new_profile_platform_ == platform) && new_profile_platform_ != platform)
            {
                new_profile_platform_ = platform;
                new_profile_source_.clear();
            }
        }
        begin_popup_row("Start From");
        const std::string defaults_label = fmt::format("{} starting values", profile_labels::get_platform_name(new_profile_platform_));
        const std::string source_label = new_profile_source_.empty() ? defaults_label : "A copy of " + new_profile_source_;
        if(ImGui::BeginCombo("##new_profile_source", source_label.c_str()))
        {
            if(ImGui::Selectable(defaults_label.c_str(), new_profile_source_.empty()))
            {
                new_profile_source_.clear();
            }
            for(const auto& profile : pm.get_profiles().get_all())
            {
                if(profile.platform == new_profile_platform_ &&
                   ImGui::Selectable(("A copy of " + profile.name).c_str(), profile.name == new_profile_source_))
                {
                    new_profile_source_ = profile.name;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltipEx("%s", "Start from the values a new profile of the platform gets, or from a copy of a profile.");
        ImGui::EndTable();
    }
    new_profile_name_.check(pm.get_profiles());
    const auto name = get_field_text(new_profile_name_.text);
    draw_message(new_profile_name_.error, true);
    widgets::draw_gap(widgets::LINE_GAP);
    const bool can_create = !name.empty() && new_profile_name_.error.empty();
    ImGui::BeginDisabled(!can_create);
    const bool is_created = ImGui::Button(ICON_MDI_PLUS " Create") || (is_entered && can_create);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    if(is_created)
    {
        const auto created = pm.create_profile(ctx, name, new_profile_platform_, new_profile_source_);
        if(created.is_ok())
        {
            set_profile_message(created, fmt::format("Created profile {}. Deploys build it now.", name));
            pm.set_deploy_profile(name);
            settings_editor_.sync(ctx);
            problems_.invalidate();
            content_view_.invalidate();
            refresh_releases();
            ImGui::CloseCurrentPopup();
        }
        else
        {
            new_profile_name_.error = created.error;
        }
    }
    ImGui::EndPopup();
}

void deploy_panel::draw_rename_profile_popup(rtti::context& ctx)
{
    if(!ImGui::BeginPopup(RENAME_PROFILE_POPUP))
    {
        return;
    }
    auto& pm = ctx.get_cached<project_manager>();
    const std::string current = pm.get_deploy_profile();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(fmt::format("Rename {}", current).c_str());
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, widgets::get_muted_color());
    ImGui::TextUnformatted("Its kept releases and texture cache move with it. Its builds in the deploy folder keep the old name.");
    ImGui::PopStyleColor();
    widgets::draw_gap(widgets::LINE_GAP);
    if(ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(widgets::to_pixels(PROFILE_POPUP_WIDTH));
    const bool is_entered = ImGui::InputText("##rename_profile_name",
                                             rename_profile_name_.text.data(),
                                             rename_profile_name_.text.size(),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    const auto name = get_field_text(rename_profile_name_.text);
    if(name != current)
    {
        rename_profile_name_.check(pm.get_profiles(), current);
    }
    draw_message(name != current ? rename_profile_name_.error : std::string{}, true);
    widgets::draw_gap(widgets::LINE_GAP);
    const bool can_rename = !name.empty() && name != current && rename_profile_name_.error.empty();
    ImGui::BeginDisabled(!can_rename);
    const bool is_renamed = ImGui::Button(ICON_MDI_PENCIL_OUTLINE " Rename") || (is_entered && can_rename);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    if(is_renamed)
    {
        const auto renamed = pm.rename_profile(ctx, current, name);
        if(renamed.is_ok())
        {
            set_profile_message(renamed, fmt::format("Renamed profile {} to {}.", current, name));
            settings_editor_.sync(ctx);
            problems_.invalidate();
            refresh_releases();
            ImGui::CloseCurrentPopup();
        }
        else
        {
            rename_profile_name_.error = renamed.error;
        }
    }
    ImGui::EndPopup();
}

void deploy_panel::set_profile_message(const deployment::result& outcome, const std::string& success)
{
    is_profile_message_error_ = !outcome.is_ok();
    profile_message_ = is_profile_message_error_ ? outcome.error : success;
}

void deploy_panel::draw_desktop_section(rtti::context& ctx)
{
    widgets::draw_muted_text("The player for this computer, with the game's data beside it. Run After Deploy starts it.");
    ImGui::SeparatorText("Executable");
    widgets::draw_muted_text(fmt::format("Named after the project. Its product name, company and version come from the "
                                         "project settings, its icon from profile {}.",
                                         settings_editor_.get_profile())
                                 .c_str());
    auto& desktop = settings_editor_.get().desktop;
    entt::meta_any object = entt::forward_as_meta(desktop);
    inspect_result result;
    {
        const inspect_without_undo_scope no_undo;
        result |= inspect_property(ctx, object, make_proxy(object), entt::resolve<desktop_deploy_settings>().data("icon"_hs));
    }
    if(!desktop.icon)
    {
        widgets::draw_muted_text("Without an image, the icon is the engine's logo.");
    }
    result |= draw_steam_settings(ctx, desktop.steam);
    if(result.edit_finished)
    {
        auto& pm = ctx.get_cached<project_manager>();
        set_profile_message(pm.set_profile_desktop(ctx, settings_editor_.get_profile(), desktop), {});
        problems_.invalidate();
    }
}

void deploy_panel::inspect_android_properties(rtti::context& ctx, std::initializer_list<entt::hashed_string> names)
{
    entt::meta_any object = entt::forward_as_meta(settings_editor_.get().android);
    const auto type = entt::resolve<android_deploy_settings>();
    std::vector<entt::meta_data> props;
    for(const auto& name : names)
    {
        if(const auto prop = type.data(name))
        {
            props.push_back(prop);
        }
    }
    inspect_android_data(ctx, object, props);
}

void deploy_panel::inspect_android_data(rtti::context& ctx, entt::meta_any& object, const std::vector<entt::meta_data>& props)
{
    inspect_result result;
    {
        const inspect_without_undo_scope no_undo;
        for(const auto& prop : props)
        {
            result |= inspect_property(ctx, object, make_proxy(object), prop);
        }
    }
    if(result.edit_finished)
    {
        save_android_settings(ctx);
    }
}

void deploy_panel::save_android_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    set_profile_message(pm.set_profile_android(ctx, settings_editor_.get_profile(), settings_editor_.get().android), {});
    problems_.invalidate();
}

void deploy_panel::draw_application_id(rtti::context& ctx)
{
    auto& android = settings_editor_.get().android;
    const auto generated = deployment::get_generated_android_application_id(ctx, settings_editor_.get_profile());
    entt::meta_any object = entt::forward_as_meta(android);
    property_layout layout(entt::resolve<android_deploy_settings>().data("application_id"_hs), object);
    const bool is_overridden = !android.application_id.empty();
    const float side = ImGui::GetFrameHeight();
    const float reset_width = is_overridden ? side + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::GetContentRegionAvail().x - reset_width));
    auto buffer = ImGui::CreateInputTextBuffer<ANDROID_APPLICATION_ID_CAPACITY>(android.application_id);
    if(ImGui::InputTextWithHint("##application_id", generated.c_str(), buffer.data(), buffer.size()))
    {
        android.application_id = buffer.data();
    }
    bool is_finished = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DrawItemActivityOutline();
    if(is_overridden)
    {
        ImGui::SameLine();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImRect rect(min, min + ImVec2(side, side));
        ImGui::ItemSize(rect);
        const auto tooltip = fmt::format("Use the id made from Company and Product: {}.", generated);
        if(container_widgets::draw_icon_button({"##generated_application_id", ICON_MDI_RESTORE, tooltip.c_str()}, rect))
        {
            android.application_id.clear();
            is_finished = true;
        }
    }
    if(!is_finished)
    {
        return;
    }
    // An id typed as the generated one is no override: it keeps following the product settings.
    if(android.application_id == generated)
    {
        android.application_id.clear();
    }
    save_android_settings(ctx);
}

void deploy_panel::draw_android_permissions(rtti::context& ctx)
{
    entt::meta_any object = entt::forward_as_meta(settings_editor_.get().android.permissions);
    const auto type = entt::resolve<android_permissions>();
    std::vector<entt::meta_data> props;
    for(const auto& entry : deployment::get_android_permission_entries())
    {
        if(const auto prop = type.data(entt::hashed_string{entry.key}))
        {
            props.push_back(prop);
        }
    }
    if(const auto prop = type.data("other"_hs))
    {
        props.push_back(prop);
    }
    inspect_android_data(ctx, object, props);
}

void deploy_panel::draw_android_section(rtti::context& ctx)
{
    const auto& android = settings_editor_.get().android;
    widgets::draw_muted_text(fmt::format("An Android package (.apk). Run After Deploy installs it on the chosen device and "
                                         "starts it there. The package and signing settings are saved in profile {}.",
                                         settings_editor_.get_profile())
                                 .c_str());
    if(!has_listed_android_devices_)
    {
        has_listed_android_devices_ = true;
        deployment::refresh_android_devices(ctx);
    }
    ImGui::SeparatorText("Device");
    if(draw_android_device_picker(ctx))
    {
        problems_.invalidate();
    }
    draw_android_device_actions(ctx);
    ImGui::SeparatorText("Package");
    draw_application_id(ctx);
    inspect_android_properties(ctx, {"version_code"_hs, "screen_orientation"_hs, "texture_compression"_hs, "icon"_hs});
    if(!android.icon)
    {
        widgets::draw_muted_text("Without an image, the icon is the engine's logo.");
    }
    inspect_android_properties(ctx, {"icon_background"_hs});
    ImGui::SeparatorText("Signing");
    inspect_android_properties(ctx, {"release"_hs});
    if(android.release)
    {
        inspect_android_properties(ctx, {"keystore"_hs, "key_alias"_hs});
    }
    ImGui::SeparatorText("Permissions");
    draw_android_permissions(ctx);
}

auto deploy_panel::draw_android_device_picker(rtti::context& ctx) -> bool
{
    // The device is this session's choice, kept with the shared settings and never saved.
    auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    const auto devices = deployment::get_android_devices();
    // The only device ready is the one to use; the choice is not saved, since devices belong to a computer.
    const auto ready_count = std::count_if(devices.begin(),
                                           devices.end(),
                                           [](const deployment::android_device& device)
                                           {
                                               return device.is_ready();
                                           });
    bool is_changed = false;
    if(params.android_device.empty() && ready_count == 1)
    {
        params.android_device = std::find_if(devices.begin(),
                                             devices.end(),
                                             [](const deployment::android_device& device)
                                             {
                                                 return device.is_ready();
                                             })
                                    ->serial;
        is_changed = true;
    }
    const auto chosen = std::find_if(devices.begin(),
                                     devices.end(),
                                     [&params](const deployment::android_device& device)
                                     {
                                         return device.is_named(params.android_device);
                                     });
    // A second connection to the phone was folded into this entry: the entry's own serial is used.
    if(chosen != devices.end() && chosen->serial != params.android_device)
    {
        params.android_device = chosen->serial;
        is_changed = true;
    }
    entt::meta_any object = entt::forward_as_meta(params);
    property_layout layout(entt::resolve<deploy_settings>().data("android_device"_hs), object);
    // A square button, so the width kept for it beside the list is exactly what it takes.
    const ImVec2 refresh_size(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::GetContentRegionAvail().x - refresh_size.x - ImGui::GetStyle().ItemSpacing.x));
    // The preview and the rows draw the devices' text themselves (draw_device_text).
    if(ImGui::BeginCombo("##android_device", "", ImGuiComboFlags_CustomPreview))
    {
        if(devices.empty())
        {
            ImGui::TextDisabled("%s", "No device found. Connect one and refresh the list.");
        }
        for(const auto& device : devices)
        {
            // The row's id is the adb serial, unique per connection: an id made from the name would clash
            // between two phones of one model that keep the name they came with.
            ImGui::PushID(device.serial.c_str());
            const ImVec2 row_min = ImGui::GetCursorScreenPos();
            if(ImGui::Selectable("##device", device.serial == params.android_device, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetTextLineHeight())))
            {
                params.android_device = device.serial;
                is_changed = true;
            }
            ImGui::SetItemTooltipEx("%s", make_device_tooltip(device).c_str());
            draw_device_text(ImGui::GetWindowDrawList(), row_min, ImGui::GetItemRectMax().x, device);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    draw_device_preview(chosen != devices.end() ? &*chosen : nullptr, params.android_device);
    ImGui::SameLine();
    ImGui::BeginDisabled(deployment::is_refreshing_android_devices());
    // The icon is drawn centered by its own shape: as button text it sits off center.
    const bool is_refresh_pressed = ImGui::Button("##refresh_android_devices", refresh_size);
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImGui::GetItemRectMin() + refresh_size * 0.5f,
                              ICON_MDI_REFRESH,
                              ImGui::GetColorU32(ImGuiCol_Text));
    if(is_refresh_pressed)
    {
        deployment::refresh_android_devices(ctx);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("%s", "List the connected devices again.");
    if(is_changed)
    {
        settings_editor_.sync(ctx);
    }
    return is_changed;
}

void deploy_panel::draw_android_device_actions(rtti::context& ctx)
{
    const auto params = ctx.get_cached<project_manager>().get_deploy_request();
    const auto devices = deployment::get_android_devices();
    const bool is_device_ready = std::any_of(devices.begin(),
                                             devices.end(),
                                             [&params](const deployment::android_device& device)
                                             {
                                                 return device.is_named(params.android_device) && device.is_ready();
                                             });
    std::string blocker;
    if(!is_device_ready)
    {
        blocker = "Choose a connected device first.";
    }
    else if(is_deploying() || deployment::is_android_device_busy())
    {
        blocker = "Wait until the deploy or the device command ends.";
    }
    const bool can_act = blocker.empty();
    const bool has_package = deployment::has_android_package(ctx, params);
    const bool is_following = deployment::is_android_game_running();
    const auto describe = [&blocker](const char* what)
    {
        return blocker.empty() ? std::string(what) : blocker;
    };
    property_layout layout("On Device", "The game on the chosen device, without a new deploy.");
    const std::string run_tooltip =
        has_package ? describe("Install the package in the profile's deploy folder on the device and start it, without a "
                               "new build. Its log follows in the Console.")
                    : std::string("No package yet. Deploy this profile first.");
    if(draw_device_action(ICON_MDI_PLAY_CIRCLE_OUTLINE " Run Last Build", run_tooltip, can_act && has_package, true))
    {
        run_last_android_build(ctx, params);
    }
    const std::string follow_tooltip = is_following ? std::string("The Console follows the game already. Stop Game ends it.")
                                                    : describe("Follow the log of the game already running on the "
                                                               "device in the Console.");
    if(draw_device_action(ICON_MDI_TEXT_BOX_SEARCH_OUTLINE " Follow Log", follow_tooltip, can_act && !is_following, false))
    {
        deployment::follow_android_game(ctx, params);
    }
    if(draw_device_action(ICON_MDI_ERASER " Clear Data", describe("Remove the game's saved data on the device. The game stops."), can_act, false))
    {
        ImGui::OpenPopup("##clear_game_data_confirm");
    }
    if(draw_confirmation("##clear_game_data_confirm",
                         "Clear the game's saved data on the device?",
                         "The game stops, and starts without its saved data the next time.",
                         ICON_MDI_ERASER " Clear Data"))
    {
        deployment::clear_android_game_data(ctx, params);
    }
    if(draw_device_action(ICON_MDI_DELETE_OUTLINE " Uninstall", describe("Uninstall the game from the device, with its saved data."), can_act, false))
    {
        ImGui::OpenPopup("##uninstall_game_confirm");
    }
    if(draw_confirmation("##uninstall_game_confirm",
                         "Uninstall the game from the device?",
                         "Its saved data on the device is removed with it.",
                         ICON_MDI_DELETE_OUTLINE " Uninstall"))
    {
        deployment::uninstall_android_game(ctx, params);
    }
}

/// The scenes the game is built from and the assets that ship with it.
void deploy_panel::draw_content_tab(rtti::context& ctx)
{
    draw_edit_target(ctx);
    widgets::draw_gap(widgets::LINE_GAP);
    content_view_.draw(ctx, settings_editor_);
}

/// The release a deploy patches and the releases kept to patch.
void deploy_panel::draw_releases_tab(rtti::context& ctx)
{
    // Releases can be kept or deleted from elsewhere (MCP) while the window is open.
    if(releases_tab_frame_ != ImGui::GetFrameCount() - 1)
    {
        refresh_releases();
    }
    releases_tab_frame_ = ImGui::GetFrameCount();
    auto& pm = ctx.get_cached<project_manager>();
    const auto request = pm.get_deploy_request();
    widgets::draw_section_title(fmt::format("Patching {}", request.profile).c_str());
    if(request.deploy_target == deploy_platform::android)
    {
        widgets::draw_muted_text("Patches and releases are for desktop profiles. An Android package always carries every "
                                 "pack whole.");
        return;
    }
    draw_patch_release_picker(ctx);
    widgets::draw_gap(widgets::LINE_GAP);
    const auto& release = request.deploy_patch_release;
    widgets::draw_muted_text(release.empty()
                                 ? "Every pack is written whole."
                                 : fmt::format("The packs of {} ship unchanged, and content_p1.pak holds every change since.", release)
                                       .c_str());
    widgets::draw_gap(widgets::SECTION_GAP);
    draw_kept_releases(ctx);
}

auto deploy_panel::draw_patch_release_picker(rtti::context& ctx) -> bool
{
    auto& pm = ctx.get_cached<project_manager>();
    auto request = pm.get_deploy_request();
    entt::meta_any object = entt::forward_as_meta(request);
    property_layout layout(entt::resolve<deploy_settings>().data("deploy_patch_release"_hs), object);
    auto& value = request.deploy_patch_release;
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
        ImGui::SetItemTooltipEx("No release named '%s' is kept for this profile.", value.c_str());
    }
    if(is_changed)
    {
        // A release is one profile's build, so the release patched is always the profile's own setting.
        const auto changed = pm.override_profile_deploy(ctx, request.profile, request);
        is_release_message_error_ = !changed.is_ok();
        release_message_ = changed.error;
        problems_.invalidate();
    }
    return is_changed;
}

/// The kept releases with their size, Keep as Release for the build in the deploy folder, and a delete
/// button per release.
void deploy_panel::draw_kept_releases(rtti::context& ctx)
{
    const auto request = ctx.get_cached<project_manager>().get_deploy_request();
    fs::error_code ec;
    const bool has_folder = fs::is_directory(get_output_location(request), ec);
    if(widgets::draw_section_title_button("Kept Releases",
                                          ICON_MDI_TAG_PLUS_OUTLINE " Keep as Release",
                                          "Keep the profile's build in the deploy folder as a release named by the product\n"
                                          "version, so later deploys of the profile can ship patches for it. Its executable\n"
                                          "and the player's debug symbols are kept with it, to read its crash dumps.",
                                          has_folder && !is_deploying()))
    {
        keep_release(ctx);
    }
    if(releases_.empty())
    {
        widgets::draw_muted_text("None yet. Deploy a packed build, then keep it to ship patches for it.");
    }
    else if(ImGui::BeginTable("##kept_releases", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupColumn("Release", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        std::string removed_release;
        for(const auto& release : releases_)
        {
            const bool is_patched = release.name == request.deploy_patch_release;
            ImGui::PushID(release.name.c_str());
            const float text_offset = ImGui::GetStyle().FramePadding.y;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + text_offset);
            widgets::draw_icon_line(ICON_MDI_TAG_OUTLINE,
                                    is_patched ? widgets::get_accent_u32() : widgets::get_muted_color(),
                                    is_patched ? fmt::format("{} - Deploys create a patch for it.", release.name).c_str()
                                               : release.name.c_str(),
                                    ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + text_offset);
            ImGui::PushStyleColor(ImGuiCol_Text, widgets::get_muted_color());
            ImGui::TextUnformatted(format_bytes(release.size).c_str());
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if(draw_release_remove_button(release.name, is_patched, !is_deploying()))
            {
                removed_release = release.name;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if(!removed_release.empty())
        {
            remove_release(ctx, removed_release);
        }
    }
    draw_message(release_message_, is_release_message_error_);
}

/// Keeps the build in the deploy folder as a release named by the product version.
void deploy_panel::keep_release(rtti::context& ctx)
{
    const auto request = ctx.get_cached<project_manager>().get_deploy_request();
    const auto name = deployment::get_default_release_name(ctx, request.profile);
    const auto result = deployment::keep_project_release(request, name);
    is_release_message_error_ = !result.is_ok();
    release_message_ = is_release_message_error_
                           ? result.error
                           : fmt::format("Kept release {}. Pick it in Patch Against Release to ship patches for it.", name);
    refresh_releases();
}

void deploy_panel::remove_release(rtti::context& ctx, const std::string& name)
{
    const auto& profile = ctx.get_cached<project_manager>().get_deploy_profile();
    const auto result = deployment::remove_project_release(ctx, profile, name);
    is_release_message_error_ = !result.is_ok();
    release_message_ = is_release_message_error_ ? result.error : fmt::format("Deleted release {}.", name);
    refresh_releases();
}

void deploy_panel::refresh_releases()
{
    releases_.clear();
    const auto& profile = settings_editor_.get_profile();
    for(auto& name : deployment::list_project_releases(profile))
    {
        const auto size = get_folder_size(deployment::get_project_release_folder(profile, name));
        releases_.push_back({std::move(name), size});
    }
}

//-----------------------------------------------------------------------------
/// <summary>
/// The steps and their progress while a deploy runs; otherwise what keeps the project from
/// deploying, the steps of the last deploy, or that it is ready.
/// </summary>
//-----------------------------------------------------------------------------
void deploy_panel::draw_install_conflict()
{
    const auto device = deployment::get_android_install_conflict();
    if(device.empty())
    {
        return;
    }
    widgets::draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE,
                            widgets::WARNING_COLOR,
                            "The game on the device was signed with another key, so this build cannot replace it. Debug "
                            "and release builds are signed with different keys.",
                            ImGui::GetColorU32(ImGuiCol_Text));
    widgets::draw_gap(widgets::LINE_GAP);
    if(ImGui::Button(ICON_MDI_DELETE_OUTLINE " Uninstall and Install"))
    {
        ImGui::OpenPopup("##replace_installed_confirm");
    }
    ImGui::SetItemTooltipEx("%s", "Uninstall the game from the device, then install and start this build.");
    if(draw_confirmation("##replace_installed_confirm",
                         "Uninstall the game from the device?",
                         "Its saved data on the device is removed with it.",
                         ICON_MDI_DELETE_OUTLINE " Uninstall and Install"))
    {
        deploy_jobs_ = deployment::replace_android_game();
        is_steam_upload_ = false;
    }
    widgets::draw_gap(widgets::LINE_GAP);
}

void deploy_panel::draw_status(rtti::context& ctx)
{
    const auto params = ctx.get_cached<project_manager>().get_deploy_request();
    const bool is_in_progress = is_deploying();
    draw_install_conflict();
    if(!is_in_progress)
    {
        const auto& problems = problems_.get(ctx, params);
        for(const auto& problem : problems)
        {
            widgets::draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE,
                                    widgets::WARNING_COLOR,
                                    problem.c_str(),
                                    ImGui::GetColorU32(ImGuiCol_Text));
        }
        if(!problems.empty())
        {
            return;
        }
    }

    // The steps shown are of the last deploy, which may have gone to another profile's folder.
    const std::string location = describe_folder(deploy_jobs_.empty() ? get_output_location(params) : deployed_location_);
    if(deploy_jobs_.empty())
    {
        widgets::draw_icon_line(ICON_MDI_CHECK_CIRCLE_OUTLINE,
                                widgets::get_accent_u32(),
                                fmt::format("Ready to deploy {} to {}.", params.profile, location).c_str(),
                                widgets::get_muted_color());
        return;
    }

    const bool has_failed = std::any_of(deploy_jobs_.begin(),
                                        deploy_jobs_.end(),
                                        [](const auto& step)
                                        {
                                            return !deployment::get_step_error(step.job).empty();
                                        });
    const auto step_count = deploy_jobs_.size();
    const auto done_count = static_cast<std::size_t>(get_progress() * static_cast<float>(step_count) + 0.5f);
    std::string summary = is_steam_upload_ ? fmt::format("Uploaded {} to Steam", location) : fmt::format("Deployed to {}", location);
    if(is_in_progress)
    {
        summary = is_steam_upload_ ? fmt::format("Uploading {} to Steam", location) : fmt::format("Deploying to {}", location);
    }
    else if(has_failed)
    {
        summary = is_steam_upload_ ? fmt::format("Upload of {} to Steam failed", location)
                                   : fmt::format("Deploy to {} failed", location);
    }
    // One bar says what the deploy does and how far it is; the steps follow in a row under it.
    widgets::draw_progress_bar(get_progress(), summary.c_str(), fmt::format("{} of {} steps", done_count, step_count).c_str());
    widgets::draw_gap(widgets::LINE_GAP);
    draw_step_row(deploy_jobs_);
}

/// Close at the left end; Open Folder and Deploy at the right end.
void deploy_panel::draw_actions(rtti::context& ctx)
{
    const auto params = ctx.get_cached<project_manager>().get_deploy_request();
    const bool can_deploy = !is_deploying() && problems_.get(ctx, params).empty();
    fs::error_code ec;
    const bool has_folder = fs::is_directory(get_output_location(params), ec);

    const char* open_label = ICON_MDI_FOLDER_OPEN_OUTLINE " Open Folder";
    const char* deploy_label = params.deploy_and_run ? ICON_MDI_PLAY " Deploy and Run" : ICON_MDI_PACKAGE_VARIANT " Deploy";
    const char* stop_label = ICON_MDI_STOP " Stop Game";
    const bool is_game_running = deployment::is_android_game_running();
    const bool has_steam_upload = deployment::has_steam_upload(params);
    float buttons_width =
        widgets::calc_button_width(open_label) + ImGui::GetStyle().ItemSpacing.x + widgets::calc_button_width(deploy_label);
    if(is_game_running)
    {
        buttons_width += widgets::calc_button_width(stop_label) + ImGui::GetStyle().ItemSpacing.x;
    }
    if(has_steam_upload)
    {
        buttons_width += widgets::calc_button_width(STEAM_UPLOAD_LABEL) + ImGui::GetStyle().ItemSpacing.x;
    }
    if(widgets::draw_button("Close", false, true))
    {
        close_window();
    }
    ImGui::SetItemTooltipEx("%s", "Close the window. A running deploy goes on, and the toolbar shows its progress.");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - buttons_width));

    if(widgets::draw_button(open_label, false, has_folder))
    {
        ImGui::OpenInShell(get_output_location(params).string().c_str());
    }
    ImGui::SetItemTooltipEx("%s", "Show the profile's folder in the file browser.");
    ImGui::SameLine();
    if(has_steam_upload)
    {
        draw_steam_upload_button(ctx, params);
        ImGui::SameLine();
    }
    if(is_game_running)
    {
        if(widgets::draw_button(stop_label, false, true))
        {
            deployment::stop_android_game();
        }
        ImGui::SetItemTooltipEx("%s", "Stop the game on the device.");
        ImGui::SameLine();
    }
    // The cached answer may be a moment old: the deploy itself checks again.
    if(widgets::draw_button(deploy_label, true, can_deploy) && start_deploy(ctx, params))
    {
        problems_.invalidate();
    }
    if(!can_deploy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", is_deploying() ? "A deploy is running." : "See what is missing above.");
    }
}

void deploy_panel::draw_steam_upload_button(rtti::context& ctx, const deploy_settings& params)
{
    const auto problem = is_deploying() ? std::string("A deploy is running.") : deployment::get_steam_upload_problem(ctx, params);
    if(widgets::draw_button(STEAM_UPLOAD_LABEL, false, problem.empty()))
    {
        ImGui::OpenPopup(STEAM_UPLOAD_POPUP);
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", problem.empty() ? "Upload the build in the profile's folder to Steam with steamcmd." : problem.c_str());
    }
    // Above the button, its right edges aligned: at the mouse it would reach past the window's bottom edge.
    const ImVec2 anchor(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y - ImGui::GetStyle().ItemSpacing.y);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing, ImVec2(1.0f, 1.0f));
    const auto consequence = describe_steam_upload(params);
    if(draw_confirmation(STEAM_UPLOAD_POPUP, fmt::format("Upload {} to Steam?", params.profile), consequence.c_str(), STEAM_UPLOAD_LABEL))
    {
        start_steam_upload(ctx, params);
    }
}

} // namespace unravel
