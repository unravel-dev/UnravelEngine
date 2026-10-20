#include "deploy_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "deploy_panel_widgets.h"

#include <editor/deploy/deploy_android.h>
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
// The empty Patch Against Release.
constexpr const char* DEPLOY_FULL_BUILD_LABEL = "None - full build";

void draw_header()
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(DEPLOY_TITLE_SCALE);
    widgets::draw_icon_line(ICON_MDI_PACKAGE_VARIANT, widgets::get_accent_u32(), "Deploy Project", ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    widgets::draw_muted_text("Builds the game into a folder, for this computer or an Android device.");
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

/// The delete button of a kept release, which asks first. True once the deletion is confirmed.
auto draw_release_remove_button(const std::string& name, bool is_patched, bool is_enabled) -> bool
{
    const float side = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, min + ImVec2(side, side));
    ImGui::ItemSize(rect);
    ImGui::BeginDisabled(!is_enabled);
    if(container_widgets::draw_icon_button({"##remove_release", ICON_MDI_DELETE_OUTLINE, "Delete this release", true}, rect))
    {
        ImGui::OpenPopup("##remove_release_confirm");
    }
    ImGui::EndDisabled();
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

/// Two columns: what every platform shares on the left - the product, where builds go, how files are
/// stored - and the platform, with only its own settings, on the right.
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
        draw_platform_tabs(ctx);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

/// The settings every platform shares: the product, the deploy folder and running the game, packaging.
void deploy_panel::draw_common_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();
    const auto& params = pm.get_deploy_settings();
    widgets::draw_section_title("Product");
    if(inspect(ctx, settings.app).edit_finished)
    {
        pm.save_project_settings(ctx);
    }
    if(inspect(ctx, settings.standalone).edit_finished)
    {
        pm.save_project_settings(ctx);
    }
    widgets::draw_gap(widgets::SECTION_GAP);
    widgets::draw_section_title("Output");
    inspect_deploy_properties(ctx, {"deploy_location"_hs});
    if(params.deploy_location.empty())
    {
        draw_missing_location_notice(ctx);
    }
    else
    {
        widgets::draw_muted_text(fmt::format("{} builds go to {}.",
                                             params.deploy_target == deploy_platform::android ? "Android" : "Desktop",
                                             describe_folder(get_output_location(params)))
                                     .c_str());
    }
    inspect_deploy_properties(ctx, {"deploy_and_run"_hs});
    if(params.deploy_and_run)
    {
        inspect_deploy_properties(ctx, {"deploy_run_arguments"_hs});
    }
    widgets::draw_gap(widgets::SECTION_GAP);
    widgets::draw_section_title("Packaging");
    inspect_deploy_properties(ctx, {"deploy_packed_assets"_hs, "deploy_cooked_assets"_hs, "deploy_debug_symbols"_hs});
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
    ImGui::SetItemTooltipEx("Deploy into %s in the project folder, which the deploy creates. Each platform builds "
                            "into a folder of its own in it.",
                            DEFAULT_DEPLOY_LOCATION);
}

/// A tab per platform; the selected one is what the deploy builds for, and shows that platform's settings.
void deploy_panel::draw_platform_tabs(rtti::context& ctx)
{
    struct platform_tab
    {
        deploy_platform platform;
        const char* label;
        const char* tooltip;
        tab_drawer draw;
    };
    static constexpr platform_tab tabs[] = {
        {deploy_platform::desktop, ICON_MDI_MONITOR " Desktop", "Build the game for this computer.", &deploy_panel::draw_desktop_section},
        {deploy_platform::android, ICON_MDI_ANDROID " Android", "Build an Android package from the editor's Android player.", &deploy_panel::draw_android_section},
    };
    auto& pm = ctx.get_cached<project_manager>();
    auto& params = pm.get_deploy_settings();
    widgets::draw_section_title("Platform");
    // The setting can change elsewhere (MCP): its tab is selected until the tab bar shows it, and clicks on
    // the tab still shown meanwhile change nothing.
    const bool is_selecting = !shown_platform_ || *shown_platform_ != params.deploy_target;
    if(!ImGui::BeginTabBar("##deploy_platforms"))
    {
        return;
    }
    for(const auto& tab : tabs)
    {
        const ImGuiTabItemFlags flags = is_selecting && tab.platform == params.deploy_target ? ImGuiTabItemFlags_SetSelected : 0;
        const bool is_open = ImGui::BeginTabItem(tab.label, nullptr, flags);
        ImGui::SetItemTooltipEx("%s", tab.tooltip);
        if(!is_open)
        {
            continue;
        }
        if(tab.platform != params.deploy_target && !is_selecting)
        {
            params.deploy_target = tab.platform;
            pm.save_deploy_settings();
            problems_.invalidate();
        }
        if(tab.platform == params.deploy_target)
        {
            shown_platform_ = tab.platform;
        }
        widgets::draw_gap(widgets::LINE_GAP);
        (this->*tab.draw)(ctx);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void deploy_panel::draw_desktop_section(rtti::context& ctx)
{
    widgets::draw_muted_text("The player for this computer, with the game's data beside it. Run After Deploy starts it.");
    widgets::draw_gap(widgets::LINE_GAP);
    inspect_deploy_properties(ctx, {"deploy_dependencies"_hs});
}

void deploy_panel::draw_android_section(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& params = pm.get_deploy_settings();
    widgets::draw_muted_text("An Android package (.apk). Run After Deploy installs it on the device and starts it there.");
    widgets::draw_gap(widgets::LINE_GAP);
    if(!has_listed_android_devices_)
    {
        has_listed_android_devices_ = true;
        deployment::refresh_android_devices(ctx);
    }
    if(draw_android_device_picker(ctx))
    {
        problems_.invalidate();
    }
    draw_android_device_actions(ctx);
    inspect_deploy_properties(ctx, {"android_application_id"_hs});
    if(params.android_application_id.empty())
    {
        widgets::draw_muted_text(
            fmt::format("Uses {} from the product settings.", deployment::get_android_application_id(ctx, params)).c_str());
    }
    inspect_deploy_properties(ctx, {"android_version_code"_hs, "android_screen_orientation"_hs, "android_icon"_hs});
    if(!params.android_icon)
    {
        widgets::draw_muted_text("Without an image, the icon is the engine's logo.");
    }
    inspect_deploy_properties(ctx, {"android_icon_background"_hs, "android_release"_hs});
    if(params.android_release)
    {
        inspect_deploy_properties(ctx, {"android_keystore"_hs, "android_key_alias"_hs});
    }
}

auto deploy_panel::draw_android_device_picker(rtti::context& ctx) -> bool
{
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
    std::string preview = "Choose a device";
    if(chosen != devices.end())
    {
        preview = chosen->get_label();
        // A second connection to the phone was folded into this entry: the entry's own serial is used.
        if(chosen->serial != params.android_device)
        {
            params.android_device = chosen->serial;
            is_changed = true;
        }
    }
    else if(!params.android_device.empty())
    {
        preview = ICON_MDI_ALERT " " + params.android_device;
    }
    entt::meta_any object = entt::forward_as_meta(params);
    property_layout layout(entt::resolve<deploy_settings>().data("android_device"_hs), object);
    // A square button, so the width kept for it beside the list is exactly what it takes.
    const ImVec2 refresh_size(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::GetContentRegionAvail().x - refresh_size.x - ImGui::GetStyle().ItemSpacing.x));
    if(ImGui::BeginCombo("##android_device", preview.c_str()))
    {
        if(devices.empty())
        {
            ImGui::TextDisabled("%s", "No device found. Connect one and refresh the list.");
        }
        for(const auto& device : devices)
        {
            const auto label = device.is_ready() ? device.get_label() : fmt::format("{} - {}", device.get_label(), device.state);
            if(ImGui::Selectable(label.c_str(), device.serial == params.android_device))
            {
                params.android_device = device.serial;
                is_changed = true;
            }
        }
        ImGui::EndCombo();
    }
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
    return is_changed;
}

void deploy_panel::draw_android_device_actions(rtti::context& ctx)
{
    auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
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
        has_package ? describe("Install the package in the deploy folder on the device and start it, without a new "
                               "build. Its log follows in the Console.")
                    : std::string("No package yet. Deploy for Android first.");
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
    content_view_.draw(ctx);
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
    const auto& release = pm.get_deploy_settings().deploy_patch_release;
    widgets::draw_section_title("Patching");
    if(draw_patch_release_picker(ctx))
    {
        pm.save_deploy_settings();
    }
    widgets::draw_gap(widgets::LINE_GAP);
    widgets::draw_muted_text(release.empty()
                                 ? "Every pack is written whole."
                                 : fmt::format("The packs of {} ship unchanged, and content_p1.pak holds every change since.", release)
                                       .c_str());
    if(pm.get_deploy_settings().deploy_target == deploy_platform::android)
    {
        widgets::draw_muted_text("Patches and releases are for desktop deploys. An Android package always carries every "
                                 "pack whole.");
    }
    widgets::draw_gap(widgets::SECTION_GAP);
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

/// The kept releases with their size, Keep as Release for the build in the deploy folder, and a delete
/// button per release.
void deploy_panel::draw_kept_releases(rtti::context& ctx)
{
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    fs::error_code ec;
    // Releases are desktop builds: their packs ship beside the player, and patches update them.
    const bool has_folder = fs::is_directory(get_platform_location(params, deploy_platform::desktop), ec);
    if(widgets::draw_section_title_button("Kept Releases",
                                          ICON_MDI_TAG_PLUS_OUTLINE " Keep as Release",
                                          "Keep the build in the deploy folder as a release named by the product version,\n"
                                          "so later deploys can ship patches for it.",
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
            const bool is_patched = release.name == params.deploy_patch_release;
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
    if(!release_message_.empty())
    {
        widgets::draw_gap(widgets::LINE_GAP);
        widgets::draw_icon_line(is_release_message_error_ ? ICON_MDI_ALERT_CIRCLE_OUTLINE : ICON_MDI_TAG_OUTLINE,
                                is_release_message_error_ ? widgets::WARNING_COLOR : widgets::get_accent_u32(),
                                release_message_.c_str(),
                                ImGui::GetColorU32(ImGuiCol_Text));
    }
}

/// Keeps the build in the deploy folder as a release named by the product version.
void deploy_panel::keep_release(rtti::context& ctx)
{
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    const auto name = deployment::get_default_release_name(ctx);
    const auto result = deployment::keep_project_release(params, name);
    is_release_message_error_ = !result.is_ok();
    release_message_ = is_release_message_error_
                           ? result.error
                           : fmt::format("Kept release {}. Pick it in Patch Against Release to ship patches for it.", name);
    refresh_releases();
}

void deploy_panel::remove_release(rtti::context& ctx, const std::string& name)
{
    const auto result = deployment::remove_project_release(ctx, name);
    is_release_message_error_ = !result.is_ok();
    release_message_ = is_release_message_error_ ? result.error : fmt::format("Deleted release {}.", name);
    refresh_releases();
}

void deploy_panel::refresh_releases()
{
    releases_.clear();
    for(auto& name : deployment::list_project_releases())
    {
        const auto size = get_folder_size(deployment::get_project_release_folder(name));
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
    }
    widgets::draw_gap(widgets::LINE_GAP);
}

void deploy_panel::draw_status(rtti::context& ctx)
{
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
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

    // The steps shown are of the last deploy, which may have gone to another platform's folder.
    const std::string location = describe_folder(deploy_jobs_.empty() ? get_output_location(params) : deployed_location_);
    if(deploy_jobs_.empty())
    {
        widgets::draw_icon_line(ICON_MDI_CHECK_CIRCLE_OUTLINE,
                                widgets::get_accent_u32(),
                                fmt::format("Ready to deploy to {}", location).c_str(),
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
    std::string summary = fmt::format("Deployed to {}", location);
    if(is_in_progress)
    {
        summary = fmt::format("Deploying to {}", location);
    }
    else if(has_failed)
    {
        summary = fmt::format("Deploy to {} failed", location);
    }
    // One bar says what the deploy does and how far it is; the steps follow in a row under it.
    widgets::draw_progress_bar(get_progress(), summary.c_str(), fmt::format("{} of {} steps", done_count, step_count).c_str());
    widgets::draw_gap(widgets::LINE_GAP);
    draw_step_row(deploy_jobs_);
}

/// Close at the left end; Open Folder and Deploy at the right end.
void deploy_panel::draw_actions(rtti::context& ctx)
{
    auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    const bool can_deploy = !is_deploying() && problems_.get(ctx, params).empty();
    fs::error_code ec;
    const bool has_folder = fs::is_directory(get_output_location(params), ec);

    const char* open_label = ICON_MDI_FOLDER_OPEN_OUTLINE " Open Folder";
    const char* deploy_label = params.deploy_and_run ? ICON_MDI_PLAY " Deploy and Run" : ICON_MDI_PACKAGE_VARIANT " Deploy";
    const char* stop_label = ICON_MDI_STOP " Stop Game";
    const bool is_game_running = deployment::is_android_game_running();
    float buttons_width =
        widgets::calc_button_width(open_label) + ImGui::GetStyle().ItemSpacing.x + widgets::calc_button_width(deploy_label);
    if(is_game_running)
    {
        buttons_width += widgets::calc_button_width(stop_label) + ImGui::GetStyle().ItemSpacing.x;
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
    ImGui::SetItemTooltipEx("%s", "Show the deploy folder in the file browser.");
    ImGui::SameLine();
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

} // namespace unravel
