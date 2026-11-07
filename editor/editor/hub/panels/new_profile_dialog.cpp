#include "new_profile_dialog.h"
#include "deploy_panel/deploy_panel_widgets.h"
#include "profile_labels.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h>
#include <editor/hub/start_page/start_page_widgets.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/screen_card.h>
#include <editor/system/project_manager.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>

#include <algorithm>
#include <cfloat>
#include <iterator>

namespace unravel
{
namespace
{
namespace widgets = deploy_widgets;

constexpr const char* DIALOG_ID = "##new_profile_dialog";
constexpr const char* CREATE_LABEL = ICON_MDI_PLUS " Create Profile";
constexpr const char* CANCEL_LABEL = "Cancel";
constexpr ImGuiWindowFlags DIALOG_FLAGS = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
// Sizes are in units of the font size, like the Deploy window's.
constexpr float DIALOG_WIDTH = 40.0f;
constexpr float DIALOG_ROUNDING = 0.75f;
constexpr float TITLE_SCALE = 1.3f;
constexpr float CLOSE_INSET = 0.6f;
constexpr float SECTION_GAP = 1.1f;
constexpr float LABEL_GAP = 0.3f;
constexpr float CARD_HEIGHT = 7.0f;
constexpr float CARD_PADDING = 0.8f;
constexpr float CARD_ROUNDING = 0.5f;
constexpr float CARD_ICON_SCALE = 1.7f;
constexpr float CARD_LINE_GAP = 0.35f;
constexpr int CARD_SUMMARY_LINES = 2;
/// The lines the note under Start From takes at least: its longest one's, so the dialog keeps its height while the
/// platform changes.
constexpr int SOURCE_NOTE_LINES = 2;
/// Above the footer, under the Start From note, which keeps a line of room when it is short.
constexpr float FOOTER_GAP = 0.6f;
/// How far a suggested name counts up (Android 2, Android 3, ...) before it gives up and leaves the name empty.
constexpr int MAX_SUGGESTED_NUMBER = 1000;

/// What a profile of the platform builds, as its card says it.
auto get_platform_summary(target_platform platform) -> const char*
{
    switch(platform)
    {
        case target_platform::android:
            return "An .apk for phones and tablets.";
        case target_platform::web:
            return "A folder for a web server, played in browsers.";
        case target_platform::desktop:
        default:
            return "A player for the system this editor runs on.";
    }
}

/// What a new profile of the platform changes from the project settings (make_profile).
auto get_starting_values_note(target_platform platform) -> const char*
{
    switch(platform)
    {
        case target_platform::android:
            return "It starts from the project settings with smaller textures, lighter particles, and the effects a "
                   "phone cannot afford turned off.";
        case target_platform::web:
            return "It starts from the project settings with global illumination turned off.";
        case target_platform::desktop:
        default:
            return "It starts from the project settings as they are.";
    }
}

/// A Start From entry that copies `profile`: "<platform icon> Copy of <name>".
auto get_copy_label(const settings_profile& profile) -> std::string
{
    return fmt::format("{} Copy of {}", profile_labels::get_platform_icon(profile.platform), profile.name);
}

/// The platform's name, numbered when a profile has it already: Android, Android 2, ...
auto make_suggested_name(const deployment::project_profiles& profiles, target_platform platform) -> std::string
{
    const std::string base = profile_labels::get_platform_name(platform);
    std::string name = base;
    for(int number = 2; !profiles.check_new_name(name).is_ok(); ++number)
    {
        if(number > MAX_SUGGESTED_NUMBER)
        {
            return {};
        }
        name = fmt::format("{} {}", base, number);
    }
    return name;
}

template<std::size_t Capacity>
void set_text(std::array<char, Capacity>& field, const std::string& text)
{
    field.fill('\0');
    std::copy_n(text.begin(), std::min(text.size(), field.size() - 1), field.begin());
}

void draw_field_label(const char* label)
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted(label);
    ImGui::PopFont();
    widgets::draw_gap(LABEL_GAP);
}

/// Pads a note under a field, begun at `start_y`, to the height of a text of `lines` lines.
void pad_note(float start_y, int lines)
{
    const float wanted = static_cast<float>(lines) * ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
    const float missing = wanted - (ImGui::GetCursorPosY() - start_y) - ImGui::GetStyle().ItemSpacing.y;
    if(missing > 0.0f)
    {
        ImGui::Dummy(ImVec2(0.0f, missing));
    }
}

void draw_header()
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(TITLE_SCALE);
    widgets::draw_icon_line(ICON_MDI_PLUS_BOX_OUTLINE, widgets::get_accent_u32(), "New Profile", ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    widgets::draw_muted_text("A profile builds the game for one platform, with the settings it changes from the project's.");
}

/// The close button in the top right corner of the dialog.
auto draw_close_button() -> bool
{
    const float inset = widgets::to_pixels(CLOSE_INSET);
    const float side = ImGui::GetFrameHeight();
    const ImVec2 window_min = ImGui::GetWindowPos();
    const ImVec2 min(window_min.x + ImGui::GetWindowWidth() - inset - side, window_min.y + inset);
    return container_widgets::draw_icon_button({"##new_profile_close", ICON_MDI_CLOSE, "Close"}, ImRect(min, min + ImVec2(side, side)));
}

/// Whether Escape was pressed this frame. The window under the dialog closes on Escape too, so this frame the key is
/// the dialog's alone. Called before the fields, which would take the key first.
auto take_escape() -> bool
{
    if(!ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        return false;
    }
    ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##new_profile_escape"), ImGuiInputFlags_LockThisFrame);
    return true;
}

/// Whether a picker of the dialog is open: called at the end of the dialog, after its pickers ended.
auto is_picker_open() -> bool
{
    return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
}

/// A platform as a card: its icon, its name and what it builds, lit when chosen. @return Whether it was pressed.
auto draw_platform_card(target_platform platform, const ImVec2& size, bool is_selected) -> bool
{
    ImGui::PushID(static_cast<int>(platform));
    const bool is_pressed = ImGui::InvisibleButton("##platform_card", size);
    ImGui::PopID();
    const bool is_hovered = ImGui::IsItemHovered();
    if(is_hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    auto* draw_list = ImGui::GetWindowDrawList();
    start_page_widgets::draw_card_frame(draw_list, rect, widgets::to_pixels(CARD_ROUNDING), is_hovered, is_selected);
    const float padding = widgets::to_pixels(CARD_PADDING);
    const float font_size = ImGui::GetFontSize();
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 accent = widgets::get_accent_u32();
    ImVec2 pos = rect.Min + ImVec2(padding, padding);
    draw_list->AddText(ImGui::GetFont(), font_size * CARD_ICON_SCALE, pos, is_selected ? accent : text_color, profile_labels::get_platform_icon(platform));
    if(is_selected)
    {
        draw_list->AddText(ImVec2(rect.Max.x - padding - font_size, rect.Min.y + padding), accent, ICON_MDI_CHECK_CIRCLE);
    }
    pos.y += font_size * (CARD_ICON_SCALE + CARD_LINE_GAP);
    draw_list->AddText(ImGui::GetFont(ImGui::Font::Bold), font_size, pos, text_color, profile_labels::get_platform_name(platform));
    pos.y += font_size * (1.0f + CARD_LINE_GAP);
    start_page_widgets::draw_text_wrapped(draw_list,
                                          pos,
                                          rect.Max.x - padding,
                                          widgets::get_muted_color(),
                                          get_platform_summary(platform),
                                          CARD_SUMMARY_LINES);
    return is_pressed;
}
} // namespace

void new_profile_dialog::open(target_platform platform)
{
    platform_ = platform;
    name_.fill('\0');
    is_name_typed_ = false;
    source_.clear();
    error_.clear();
    is_open_requested_ = true;
    had_open_picker_ = false;
}

auto new_profile_dialog::draw(rtti::context& ctx) -> std::string
{
    if(is_open_requested_)
    {
        is_open_requested_ = false;
        choose_platform(ctx, platform_);
        ImGui::OpenPopup(DIALOG_ID);
    }
    const float width = widgets::to_pixels(DIALOG_WIDTH);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, screen_card::get_padding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, widgets::to_pixels(DIALOG_ROUNDING));
    const bool is_open = ImGui::BeginPopupModal(DIALOG_ID, nullptr, DIALOG_FLAGS);
    // Only the dialog itself: the pickers opened from it keep the theme's padding.
    ImGui::PopStyleVar(2);
    if(!is_open)
    {
        return {};
    }
    const bool is_escape_pressed = take_escape();
    draw_header();
    widgets::draw_gap(SECTION_GAP);
    draw_platform_cards(ctx);
    widgets::draw_gap(SECTION_GAP);
    const bool is_entered = draw_name_field(ctx);
    widgets::draw_gap(SECTION_GAP);
    draw_source_field(ctx);
    widgets::draw_gap(FOOTER_GAP);
    const std::string name(name_.data());
    const bool can_create = !name.empty() && ctx.get_cached<project_manager>().get_profiles().check_new_name(name).is_ok();
    std::string created;
    if(draw_footer(can_create) || (is_entered && can_create))
    {
        created = create(ctx);
    }
    // An Escape that closed a picker (ImGui closes it before the frame's widgets) leaves the dialog open.
    if(draw_close_button() || (is_escape_pressed && !had_open_picker_))
    {
        ImGui::CloseCurrentPopup();
    }
    had_open_picker_ = is_picker_open();
    ImGui::EndPopup();
    return created;
}

void new_profile_dialog::draw_platform_cards(rtti::context& ctx)
{
    draw_field_label("Platform");
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const auto count = static_cast<float>(std::size(profile_labels::PLATFORMS));
    const ImVec2 size((ImGui::GetContentRegionAvail().x - spacing * (count - 1.0f)) / count, widgets::to_pixels(CARD_HEIGHT));
    for(const auto platform : profile_labels::PLATFORMS)
    {
        if(platform != profile_labels::PLATFORMS[0])
        {
            ImGui::SameLine();
        }
        if(draw_platform_card(platform, size, platform == platform_) && platform != platform_)
        {
            choose_platform(ctx, platform);
        }
    }
}

auto new_profile_dialog::draw_name_field(rtti::context& ctx) -> bool
{
    draw_field_label("Name");
    if(ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    widgets::push_field_colors();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool is_entered = ImGui::InputTextWithHint("##new_profile_name",
                                                     "Such as Android or Demo",
                                                     name_.data(),
                                                     name_.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    widgets::pop_field_colors();
    if(ImGui::IsItemEdited())
    {
        is_name_typed_ = true;
        error_.clear();
    }
    const std::string name(name_.data());
    const auto checked = name.empty() ? deployment::result{} : ctx.get_cached<project_manager>().get_profiles().check_new_name(name);
    const auto& problem = !error_.empty() ? error_ : checked.error;
    widgets::draw_gap(LABEL_GAP);
    if(problem.empty())
    {
        widgets::draw_muted_text("Its builds go into a folder of this name in the deploy folder.");
    }
    else
    {
        widgets::draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE, widgets::WARNING_COLOR, problem.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
    }
    return is_entered;
}

void new_profile_dialog::draw_source_field(rtti::context& ctx)
{
    draw_field_label("Start From");
    const auto& profiles = ctx.get_cached<project_manager>().get_profiles().get_all();
    const bool has_copies = std::any_of(profiles.begin(),
                                        profiles.end(),
                                        [this](const settings_profile& profile)
                                        {
                                            return profile.platform == platform_;
                                        });
    const std::string defaults_label =
        fmt::format("{} Default {}", profile_labels::get_platform_icon(platform_), profile_labels::get_platform_name(platform_));
    const auto source = std::find_if(profiles.begin(),
                                     profiles.end(),
                                     [this](const settings_profile& profile)
                                     {
                                         return profile.name == source_;
                                     });
    const std::string source_label = source == profiles.end() ? defaults_label : get_copy_label(*source);
    widgets::push_field_colors();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(!has_copies);
    if(ImGui::BeginCombo("##new_profile_source", source_label.c_str()))
    {
        if(ImGui::Selectable(defaults_label.c_str(), source_.empty()))
        {
            source_.clear();
        }
        for(const auto& profile : profiles)
        {
            if(profile.platform == platform_ && ImGui::Selectable(get_copy_label(profile).c_str(), profile.name == source_))
            {
                source_ = profile.name;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    widgets::pop_field_colors();
    widgets::draw_gap(LABEL_GAP);
    const float note_y = ImGui::GetCursorPosY();
    if(source_.empty())
    {
        widgets::draw_muted_text(get_starting_values_note(platform_));
    }
    else
    {
        widgets::draw_muted_text(fmt::format("It starts as a copy of {}, with every setting that profile changes.", source_).c_str());
    }
    pad_note(note_y, SOURCE_NOTE_LINES);
}

auto new_profile_dialog::draw_footer(bool can_create) -> bool
{
    ImGui::Separator();
    widgets::draw_gap(widgets::LINE_GAP);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttons_width = widgets::calc_button_width(CANCEL_LABEL) + spacing + widgets::calc_button_width(CREATE_LABEL);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - buttons_width));
    if(widgets::draw_button(CANCEL_LABEL, false, true))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const bool is_pressed = widgets::draw_button(CREATE_LABEL, true, can_create);
    if(!can_create && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", "Name the profile first.");
    }
    return is_pressed;
}

void new_profile_dialog::choose_platform(rtti::context& ctx, target_platform platform)
{
    platform_ = platform;
    source_.clear();
    if(!is_name_typed_)
    {
        set_text(name_, make_suggested_name(ctx.get_cached<project_manager>().get_profiles(), platform));
    }
}

auto new_profile_dialog::create(rtti::context& ctx) -> std::string
{
    const std::string name(name_.data());
    const auto created = ctx.get_cached<project_manager>().create_profile(ctx, name, platform_, source_);
    if(!created.is_ok())
    {
        error_ = created.error;
        return {};
    }
    ImGui::CloseCurrentPopup();
    return name;
}

} // namespace unravel
