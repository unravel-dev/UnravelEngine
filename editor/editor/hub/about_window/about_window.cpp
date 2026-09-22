#include "about_window.h"

#include <editor/hub/engine_links.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>
#include <engine/assets/asset_manager.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>
#include <version/version.h>

#include <array>
#include <string>

namespace unravel
{
namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, like the start page and the loading screen, so the window
// follows the UI scale.
constexpr float ABOUT_WIDTH = 30.0f;
constexpr float ABOUT_ROUNDING = 0.75f;
constexpr float ABOUT_LOGO_SIZE = 4.4f;
constexpr float ABOUT_LOGO_GAP = 1.0f;
constexpr float ABOUT_TITLE_SCALE = 1.45f;
constexpr float ABOUT_SECTION_GAP = 1.1f;
constexpr float ABOUT_BOX_PADDING = 0.75f;
constexpr float ABOUT_BOX_ROUNDING = 0.5f;
constexpr float ABOUT_LABEL_WIDTH = 6.0f;
constexpr float ABOUT_BUTTON_HEIGHT = 2.2f;
constexpr float ABOUT_BUTTON_ROUNDING = 0.4f;
constexpr float ABOUT_CLOSE_INSET = 0.6f;
constexpr float ABOUT_MUTED_ALPHA = 0.55f;
constexpr double ABOUT_COPIED_SECONDS = 1.5;
constexpr ImU32 ABOUT_BOX_COLOR = IM_COL32(255, 255, 255, 10);
constexpr ImU32 ABOUT_BOX_BORDER_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 ABOUT_FLAT_HOVERED_COLOR = IM_COL32(255, 255, 255, 26);
constexpr ImU32 ABOUT_FLAT_ACTIVE_COLOR = IM_COL32(255, 255, 255, 46);

constexpr const char* ABOUT_POPUP_ID = "About Unravel Engine";
constexpr const char* ABOUT_LOGO_KEY = "editor:/data/icons/unravel_logo.png";
constexpr const char* ABOUT_PRODUCT_NAME = "Unravel Engine";
constexpr const char* ABOUT_TAGLINE = "Game engine and editor";
constexpr const char* ABOUT_LICENSE = "MIT License";
// Matches LICENSE.txt. "\xC2\xA9" is the copyright sign in UTF-8.
constexpr const char* ABOUT_COPYRIGHT = "Copyright \xC2\xA9 2025 unravel-dev";

#ifdef NDEBUG
constexpr const char* ABOUT_CONFIGURATION = "Release";
#else
constexpr const char* ABOUT_CONFIGURATION = "Debug";
#endif

struct about_link
{
    const char* label;
    const char* url;
};

constexpr std::array<about_link, 2> ABOUT_LINKS{{
    {ICON_MDI_GITHUB " Source code", engine_links::REPOSITORY},
    {ICON_MDI_BOOK_OPEN_VARIANT " Documentation", engine_links::SCRIPTING_API_DOCS},
}};

auto get_muted_text_color() -> ImVec4
{
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    color.w *= ABOUT_MUTED_ALPHA;
    return color;
}

/// The release the build belongs to, without the commits made since: the part that stays the same
/// from build to build.
auto get_release_version() -> std::string
{
    const version::engine_version current = version::get_current();
    return fmt::format("{}.{}.{}", current.major, current.minor, current.patch);
}

/// The exact build, for bug reports.
auto get_build_description() -> std::string
{
    return fmt::format("{} {} ({})", ABOUT_PRODUCT_NAME, version::get_full(), ABOUT_CONFIGURATION);
}

void draw_muted_text(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, get_muted_text_color());
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void draw_section_gap()
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(ABOUT_SECTION_GAP)));
}

/// An icon without a frame until the pointer is over it.
auto draw_flat_icon_button(const char* label, const ImVec4& icon_color) -> bool
{
    const float side = ImGui::GetFrameHeight();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ABOUT_FLAT_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ABOUT_FLAT_ACTIVE_COLOR);
    ImGui::PushStyleColor(ImGuiCol_Text, icon_color);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(ABOUT_BUTTON_ROUNDING));
    const bool is_pressed = ImGui::Button(label, ImVec2(side, side));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return is_pressed;
}

/// The close button in the top right corner, over whatever the content put there.
auto draw_close_button() -> bool
{
    const float inset = to_pixels(ABOUT_CLOSE_INSET);
    const ImVec2 window_min = ImGui::GetWindowPos();
    const float side = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(ImVec2(window_min.x + ImGui::GetWindowWidth() - inset - side, window_min.y + inset));
    const bool is_pressed = draw_flat_icon_button(ICON_MDI_CLOSE "##about_close", get_muted_text_color());
    ImGui::SetItemTooltipEx("%s", "Close");
    return is_pressed;
}

//-----------------------------------------------------------------------------
/// <summary>
/// Draws the rows, then a rounded box behind them that spans the width of the window.
/// </summary>
//-----------------------------------------------------------------------------
template<typename DrawRows>
void draw_boxed(DrawRows&& draw_rows)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 box_min = ImGui::GetCursorScreenPos();
    const float box_width = ImGui::GetContentRegionAvail().x;
    const float padding = to_pixels(ABOUT_BOX_PADDING);
    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(box_min + ImVec2(padding, padding));
    ImGui::BeginGroup();
    draw_rows(box_min.x + box_width - padding);
    ImGui::EndGroup();
    const ImVec2 box_max(box_min.x + box_width, ImGui::GetItemRectMax().y + padding);
    draw_list->ChannelsSetCurrent(0);
    const float rounding = to_pixels(ABOUT_BOX_ROUNDING);
    draw_list->AddRectFilled(box_min, box_max, ABOUT_BOX_COLOR, rounding);
    draw_list->AddRect(box_min, box_max, ABOUT_BOX_BORDER_COLOR, rounding);
    draw_list->ChannelsMerge();
    ImGui::SetCursorScreenPos(box_min);
    ImGui::Dummy(box_max - box_min);
}

/// A muted label and its value, on one row of the details box.
void draw_detail_row(const char* label, const char* value)
{
    const float row_x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    draw_muted_text(label);
    ImGui::SameLine(row_x + to_pixels(ABOUT_LABEL_WIDTH));
    ImGui::TextUnformatted(value);
}
} // namespace

void about_window::open()
{
    is_open_ = true;
}

void about_window::draw(rtti::context& ctx)
{
    if(!is_open_)
    {
        return;
    }
    if(!ImGui::IsPopupOpen(ABOUT_POPUP_ID))
    {
        ImGui::OpenPopup(ABOUT_POPUP_ID);
    }

    const float width = to_pixels(ABOUT_WIDTH);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, screen_card::get_padding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, to_pixels(ABOUT_ROUNDING));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    const bool is_visible = ImGui::BeginPopupModal(ABOUT_POPUP_ID, nullptr, flags);
    // Only the window itself: tooltips opened from it keep the theme's padding.
    ImGui::PopStyleVar(2);
    if(!is_visible)
    {
        is_open_ = false;
        return;
    }

    draw_header(ctx);
    draw_section_gap();
    draw_details();
    draw_section_gap();
    draw_links();
    draw_section_gap();
    draw_footer();

    if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C) && !ImGui::IsAnyItemActive())
    {
        copy_version();
    }
    const bool is_escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if(draw_close_button() || is_escape_pressed)
    {
        is_open_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void about_window::draw_header(rtti::context& ctx)
{
    if(!logo_)
    {
        logo_ = ctx.get_cached<asset_manager>().get_asset<gfx::texture>(ABOUT_LOGO_KEY);
    }
    const float logo_size = to_pixels(ABOUT_LOGO_SIZE);
    const ImVec2 header_min = ImGui::GetCursorScreenPos();
    ImGui::Image(ImGui::ToId(logo_), ImVec2(logo_size, logo_size));

    // The name and the tagline, centered on the logo.
    const float title_height = ImGui::GetFontSize() * ABOUT_TITLE_SCALE;
    const float text_height = title_height + ImGui::GetStyle().ItemSpacing.y + ImGui::GetTextLineHeight();
    const float text_x = header_min.x + logo_size + to_pixels(ABOUT_LOGO_GAP);
    ImGui::SetCursorScreenPos(ImVec2(text_x, header_min.y + ImMax(0.0f, (logo_size - text_height) * 0.5f)));
    ImGui::BeginGroup();
    ImGui::PushFont(ImGui::Font::Black);
    ImGui::PushWindowFontScale(ABOUT_TITLE_SCALE);
    ImGui::TextUnformatted(ABOUT_PRODUCT_NAME);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    draw_muted_text(ABOUT_TAGLINE);
    ImGui::EndGroup();

    ImGui::SetCursorScreenPos(header_min);
    ImGui::Dummy(ImVec2(0.0f, logo_size));
}

void about_window::draw_details()
{
    const std::string release_version = get_release_version();
    draw_boxed(
        [&](float right_x)
        {
            draw_detail_row("Version", release_version.c_str());
            ImGui::SetItemTooltipEx("%s", get_build_description().c_str());

            const bool is_copied =
                copied_time_ >= 0.0 && ImGui::GetTime() - copied_time_ < ABOUT_COPIED_SECONDS;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(right_x - ImGui::GetFrameHeight(), ImGui::GetCursorScreenPos().y));
            const char* copy_icon = is_copied ? ICON_MDI_CHECK "##about_copy" : ICON_MDI_CONTENT_COPY "##about_copy";
            const ImVec4 copy_color = is_copied ? imgui_style::get_accent_color() : get_muted_text_color();
            if(draw_flat_icon_button(copy_icon, copy_color))
            {
                copy_version();
            }
            ImGui::SetItemTooltipEx("%s", is_copied ? "Copied" : "Copy the version and build (Ctrl+C)");

            draw_detail_row("License", ABOUT_LICENSE);
        });
}

void about_window::draw_links()
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float link_count = static_cast<float>(ABOUT_LINKS.size());
    const float button_width = (ImGui::GetContentRegionAvail().x - spacing * (link_count - 1.0f)) / link_count;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(ABOUT_BUTTON_ROUNDING));
    for(std::size_t i = 0; i < ABOUT_LINKS.size(); ++i)
    {
        const about_link& link = ABOUT_LINKS[i];
        if(i > 0)
        {
            ImGui::SameLine();
        }
        if(ImGui::Button(link.label, ImVec2(button_width, to_pixels(ABOUT_BUTTON_HEIGHT))))
        {
            ImGui::OpenInShell(link.url);
        }
        ImGui::SetItemTooltipEx("%s", link.url);
    }
    ImGui::PopStyleVar();
}

void about_window::draw_footer()
{
    const float text_width = ImGui::CalcTextSize(ABOUT_COPYRIGHT).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (ImGui::GetContentRegionAvail().x - text_width) * 0.5f));
    draw_muted_text(ABOUT_COPYRIGHT);
}

void about_window::copy_version()
{
    ImGui::SetClipboardText(get_build_description().c_str());
    copied_time_ = ImGui::GetTime();
}

} // namespace unravel
