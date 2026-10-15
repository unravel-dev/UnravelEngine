#include "start_page.h"
#include "start_page_widgets.h"

#include <editor/hub/engine_links.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/screen_art.h>
#include <editor/imgui/screen_card.h>
#include <editor/system/project_manager.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <array>

namespace unravel
{
namespace widgets = start_page_widgets;

namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, see screen_card.
constexpr float SAMPLES_VIEW_SECTION_GAP = 1.4f;
constexpr float SAMPLES_VIEW_GRID_GAP = 1.3f;
constexpr float SAMPLES_VIEW_CARD_MIN_WIDTH = 22.0f;
constexpr int SAMPLES_VIEW_MAX_COLUMNS = 3;
constexpr float SAMPLES_VIEW_CARD_PADDING = 1.0f;
constexpr float SAMPLES_VIEW_LINE_GAP = 0.3f;
constexpr float SAMPLES_VIEW_BLOCK_GAP = 0.7f;
constexpr float SAMPLES_VIEW_TITLE_SCALE = 1.2f;
constexpr float SAMPLES_VIEW_EYEBROW_SCALE = 0.8f;
constexpr int SAMPLES_VIEW_DESCRIPTION_LINES = 3;
constexpr float SAMPLES_VIEW_TAG_GAP = 0.35f;
constexpr float SAMPLES_VIEW_ROUNDING = 0.6f;
constexpr float SAMPLES_VIEW_IMAGE_ASPECT = 9.0f / 16.0f;
constexpr float SAMPLES_VIEW_PILL_INSET = 0.7f;
constexpr float SAMPLES_VIEW_TAG_TEXT_ALPHA = 0.8f;
constexpr ImU32 SAMPLES_VIEW_INSTALLED_FILL_COLOR = IM_COL32(40, 120, 70, 235);
constexpr ImU32 SAMPLES_VIEW_TAG_COLOR = IM_COL32(255, 255, 255, 16);
constexpr const char* SAMPLES_VIEW_GITHUB_LABEL = ICON_MDI_GITHUB "  All on GitHub";

constexpr float LEARN_VIEW_ROW_HEIGHT = 4.6f;
constexpr float LEARN_VIEW_ROW_GAP = 0.6f;
constexpr float LEARN_VIEW_ROW_PADDING = 0.9f;
constexpr float LEARN_VIEW_TEXT_GAP = 1.1f;
constexpr float LEARN_VIEW_ICON_SCALE = 1.4f;
constexpr float LEARN_VIEW_ROUNDING = 0.6f;
constexpr float LEARN_VIEW_MAX_WIDTH = 64.0f;

struct learn_link
{
    const char* icon;
    const char* title;
    const char* description;
    const char* url;
};

constexpr std::array<learn_link, 4> LEARN_VIEW_LINKS{{
    {ICON_MDI_BOOK_OPEN_PAGE_VARIANT,
     "Engine API Reference",
     "The C++ API of the engine: its systems, components, rendering and assets.",
     engine_links::ENGINE_API_DOCS},
    {ICON_MDI_LANGUAGE_CSHARP,
     "Scripting API Reference",
     "The C# API the scripts of a project use: entities, physics, audio, UI and more.",
     engine_links::SCRIPTING_API_DOCS},
    {ICON_MDI_GITHUB,
     "Source Code",
     "The engine and the editor on GitHub: build them, read them, contribute to them.",
     engine_links::REPOSITORY},
    {ICON_MDI_BUG_OUTLINE,
     "Report an Issue",
     "Found a bug, or missing a feature? Tell us about it on GitHub.",
     engine_links::ISSUES},
}};

/// The height of everything a sample card shows under its picture.
auto calc_sample_info_height() -> float
{
    const float font_size = ImGui::GetFontSize();
    const float title_height = ImFloor(font_size * SAMPLES_VIEW_TITLE_SCALE);
    const float eyebrow_height = ImFloor(font_size * SAMPLES_VIEW_EYEBROW_SCALE);
    const float description_height = static_cast<float>(SAMPLES_VIEW_DESCRIPTION_LINES) * font_size;
    const float tags_height = widgets::calc_pill_size("Tag").y;
    return 2.0f * to_pixels(SAMPLES_VIEW_CARD_PADDING) + eyebrow_height + to_pixels(SAMPLES_VIEW_LINE_GAP) + title_height +
           to_pixels(SAMPLES_VIEW_BLOCK_GAP) + description_height + to_pixels(SAMPLES_VIEW_BLOCK_GAP) + tags_height +
           to_pixels(SAMPLES_VIEW_BLOCK_GAP) + widgets::get_button_height();
}

/// The tags in a row of pills, as many as fit.
void draw_sample_tags(ImDrawList* draw_list, const ImVec2& pos, float max_x, const sample_catalog::sample_info& sample)
{
    float x = pos.x;
    for(const char* tag : sample.tags)
    {
        if(tag == nullptr)
        {
            continue;
        }
        const ImVec2 size = widgets::calc_pill_size(tag);
        if(x + size.x > max_x)
        {
            return;
        }
        widgets::draw_pill(draw_list, ImVec2(x, pos.y), tag, SAMPLES_VIEW_TAG_COLOR, widgets::get_text_color(SAMPLES_VIEW_TAG_TEXT_ALPHA));
        x += size.x + to_pixels(SAMPLES_VIEW_TAG_GAP);
    }
}

void draw_samples_eyebrow(ImDrawList* draw_list, const ImVec2& pos, float max_x, const char* text)
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::PushWindowFontScale(SAMPLES_VIEW_EYEBROW_SCALE);
    widgets::draw_text_clipped(draw_list, pos, max_x, widgets::get_accent_color(1.0f), text);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
}

/// The two buttons of a sample: open the copy on disk, or get one. Returns true when the copy
/// on disk is to be opened.
auto draw_sample_actions(const sample_catalog::sample_info& sample, const fs::path& installed) -> bool
{
    if(!installed.empty())
    {
        const bool wants_open = widgets::draw_button(ICON_MDI_FOLDER_OPEN_OUTLINE "  Open", widgets::button_kind::primary);
        ImGui::SetItemTooltipEx("Open %s", installed.generic_string().c_str());
        ImGui::SameLine();
        if(widgets::draw_button(ICON_MDI_GITHUB "  GitHub", widgets::button_kind::secondary))
        {
            ImGui::OpenInShell(sample_catalog::get_repository_url(sample).c_str());
        }
        return wants_open;
    }
    if(widgets::draw_button(ICON_MDI_GITHUB "  Get on GitHub", widgets::button_kind::primary))
    {
        ImGui::OpenInShell(sample_catalog::get_repository_url(sample).c_str());
    }
    ImGui::SetItemTooltipEx("%s", "Clone it, then open its folder with Open...");
    ImGui::SameLine();
    if(widgets::draw_button(ICON_MDI_DOWNLOAD "  ZIP", widgets::button_kind::secondary))
    {
        ImGui::OpenInShell(sample_catalog::get_download_url(sample).c_str());
    }
    ImGui::SetItemTooltipEx("%s", "Download the sample as a ZIP archive in the browser");
    return false;
}
} // namespace

void start_page::draw_samples_view(rtti::context& ctx)
{
    const ImVec2 start = ImGui::GetCursorPos();
    const float avail_width = ImGui::GetContentRegionAvail().x;
    const float button_width = widgets::calc_button_width(SAMPLES_VIEW_GITHUB_LABEL);
    widgets::draw_page_title("Samples",
                             "Projects that show what the engine can do. Open one to look inside, or get it from GitHub.",
                             avail_width - button_width - ImGui::GetStyle().ItemSpacing.x);
    const float end_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(start.x + avail_width - button_width, start.y + ImMax(0.0f, (end_y - start.y - widgets::get_button_height()) * 0.5f)));
    if(widgets::draw_button(SAMPLES_VIEW_GITHUB_LABEL, widgets::button_kind::ghost))
    {
        ImGui::OpenInShell(engine_links::ORGANIZATION);
    }
    ImGui::SetCursorPos(ImVec2(start.x, ImMax(end_y, ImGui::GetCursorPosY())));
    ImGui::Dummy(ImVec2(0.0f, to_pixels(SAMPLES_VIEW_SECTION_GAP)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
    if(ImGui::BeginChild("##samples_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, flags))
    {
        const auto& samples = sample_catalog::get_samples();
        const float gap = to_pixels(SAMPLES_VIEW_GRID_GAP);
        const float content_width = ImGui::GetContentRegionAvail().x;
        const int fitting_columns = static_cast<int>((content_width + gap) / (to_pixels(SAMPLES_VIEW_CARD_MIN_WIDTH) + gap));
        const int columns = ImClamp(fitting_columns, 1, SAMPLES_VIEW_MAX_COLUMNS);
        const float card_width = ImFloor((content_width - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        const ImVec2 card_size(card_width, ImFloor(card_width * SAMPLES_VIEW_IMAGE_ASPECT) + calc_sample_info_height());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        for(size_t index = 0; index < samples.size(); ++index)
        {
            if(index % static_cast<size_t>(columns) != 0)
            {
                ImGui::SameLine();
            }
            draw_sample_card(ctx, samples[index], card_size);
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

void start_page::draw_sample_card(rtti::context& ctx, const sample_catalog::sample_info& sample, const ImVec2& size)
{
    auto& pm = ctx.get_cached<project_manager>();
    const fs::path installed = sample_catalog::find_installed(sample, pm.get_editor_settings().projects.recent_projects);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImRect rect(pos, pos + size);
    const float rounding = to_pixels(SAMPLES_VIEW_ROUNDING);
    const float image_height = ImFloor(size.x * SAMPLES_VIEW_IMAGE_ASPECT);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGui::PushID(sample.repository);
    ImGui::BeginGroup();
    const bool is_card_hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rect.Min, rect.Max);
    widgets::draw_card_frame(draw_list, rect, rounding, is_card_hovered, false);
    // The picture opens the copy on disk, or the page of the sample when there is none.
    const bool is_image_pressed = ImGui::InvisibleButton("##sample_image", ImVec2(size.x, image_height));
    ImGui::SetItemTooltipEx("%s", installed.empty() ? "Open its page on GitHub" : "Open the sample");
    const ImRect image_rect(pos + ImVec2(1.0f, 1.0f), ImVec2(rect.Max.x - 1.0f, pos.y + image_height));
    const gfx::texture::ptr image = get_sample_image(pm, sample);
    if(image)
    {
        screen_art::draw_image_cover(draw_list, image, image_rect, rounding - 1.0f, ImDrawFlags_RoundCornersTop);
    }
    else
    {
        screen_art::draw_placeholder(draw_list, image_rect, sample.title, rounding - 1.0f, ImDrawFlags_RoundCornersTop, sample.icon);
    }
    const float inset = to_pixels(SAMPLES_VIEW_PILL_INSET);
    if(sample.is_new)
    {
        widgets::draw_pill(draw_list, image_rect.Min + ImVec2(inset, inset), "NEW", widgets::get_accent_color(1.0f), IM_COL32_WHITE);
    }
    if(!installed.empty())
    {
        const char* installed_text = ICON_MDI_CHECK_CIRCLE " On this computer";
        const ImVec2 pill_size = widgets::calc_pill_size(installed_text);
        widgets::draw_pill(draw_list, ImVec2(image_rect.Max.x - inset - pill_size.x, image_rect.Min.y + inset), installed_text, SAMPLES_VIEW_INSTALLED_FILL_COLOR, IM_COL32_WHITE);
    }
    const float padding = to_pixels(SAMPLES_VIEW_CARD_PADDING);
    const float text_x = pos.x + padding;
    const float max_x = rect.Max.x - padding;
    float text_y = image_rect.Max.y + padding;
    draw_samples_eyebrow(draw_list, ImVec2(text_x, text_y), max_x, sample.topic);
    text_y += ImFloor(ImGui::GetFontSize() * SAMPLES_VIEW_EYEBROW_SCALE) + to_pixels(SAMPLES_VIEW_LINE_GAP);
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(SAMPLES_VIEW_TITLE_SCALE);
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, text_y), max_x, widgets::get_text_color(1.0f), sample.title);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    text_y += ImFloor(ImGui::GetFontSize() * SAMPLES_VIEW_TITLE_SCALE) + to_pixels(SAMPLES_VIEW_BLOCK_GAP);
    widgets::draw_text_wrapped(draw_list, ImVec2(text_x, text_y), max_x, widgets::get_muted_color(), sample.description, SAMPLES_VIEW_DESCRIPTION_LINES);
    text_y += static_cast<float>(SAMPLES_VIEW_DESCRIPTION_LINES) * ImGui::GetFontSize() + to_pixels(SAMPLES_VIEW_BLOCK_GAP);
    draw_sample_tags(draw_list, ImVec2(text_x, text_y), max_x, sample);
    ImGui::SetCursorScreenPos(ImVec2(text_x, rect.Max.y - padding - widgets::get_button_height()));
    const bool wants_open = draw_sample_actions(sample, installed);
    // The group spans the whole card, so the next one goes beside it.
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(size);
    ImGui::EndGroup();
    ImGui::PopID();
    if(is_image_pressed && installed.empty())
    {
        ImGui::OpenInShell(sample_catalog::get_repository_url(sample).c_str());
    }
    if((is_image_pressed && !installed.empty()) || wants_open)
    {
        open_project(ctx, installed.string());
    }
}

void start_page::draw_learn_view()
{
    widgets::draw_page_title("Learn", "The references of the engine and of its scripting, and where it lives.");
    ImGui::Dummy(ImVec2(0.0f, to_pixels(SAMPLES_VIEW_SECTION_GAP)));
    const float width = ImMin(ImGui::GetContentRegionAvail().x, to_pixels(LEARN_VIEW_MAX_WIDTH));
    const float row_height = to_pixels(LEARN_VIEW_ROW_HEIGHT);
    const float padding = to_pixels(LEARN_VIEW_ROW_PADDING);
    const float rounding = to_pixels(LEARN_VIEW_ROUNDING);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, to_pixels(LEARN_VIEW_ROW_GAP)));
    for(const learn_link& link : LEARN_VIEW_LINKS)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect rect(pos, pos + ImVec2(width, row_height));
        ImGui::PushID(link.url);
        const bool is_pressed = ImGui::InvisibleButton("##learn_link", rect.GetSize());
        ImGui::PopID();
        const bool is_hovered = ImGui::IsItemHovered();
        ImGui::SetItemTooltipEx("%s", link.url);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        widgets::draw_card_frame(draw_list, rect, rounding, is_hovered, false);
        const float tile_size = row_height - 2.0f * padding;
        const ImRect tile(pos + ImVec2(padding, padding), pos + ImVec2(padding + tile_size, padding + tile_size));
        screen_art::draw_gradient(draw_list, tile, widgets::get_accent_color(1.0f), rounding);
        ImFont* icon_font = ImGui::GetFont(ImGui::Font::Regular);
        const float icon_size = ImFloor(ImGui::GetFontSize() * LEARN_VIEW_ICON_SCALE);
        const ImVec2 icon_extent = icon_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, link.icon);
        draw_list->AddText(icon_font, icon_size, ImFloor(tile.GetCenter() - icon_extent * 0.5f), IM_COL32_WHITE, link.icon);
        const float text_x = tile.Max.x + to_pixels(LEARN_VIEW_TEXT_GAP);
        const float arrow_width = ImGui::CalcTextSize(ICON_MDI_OPEN_IN_NEW).x;
        const float max_x = rect.Max.x - padding - arrow_width - to_pixels(LEARN_VIEW_TEXT_GAP);
        const float top = ImFloor(rect.GetCenter().y - ImGui::GetFontSize() - to_pixels(SAMPLES_VIEW_LINE_GAP) * 0.5f);
        ImGui::PushFont(ImGui::Font::SemiBold);
        widgets::draw_text_clipped(draw_list, ImVec2(text_x, top), max_x, widgets::get_text_color(1.0f), link.title);
        ImGui::PopFont();
        const float bottom = top + ImGui::GetFontSize() + to_pixels(SAMPLES_VIEW_LINE_GAP);
        widgets::draw_text_clipped(draw_list, ImVec2(text_x, bottom), max_x, widgets::get_muted_color(), link.description);
        ImGui::PushFont(ImGui::Font::Regular);
        draw_list->AddText(ImVec2(rect.Max.x - padding - arrow_width, ImFloor(rect.GetCenter().y - ImGui::GetFontSize() * 0.5f)),
                           is_hovered ? widgets::get_text_color(1.0f) : widgets::get_muted_color(),
                           ICON_MDI_OPEN_IN_NEW);
        ImGui::PopFont();
        if(is_pressed)
        {
            ImGui::OpenInShell(link.url);
        }
    }
    ImGui::PopStyleVar();
}
} // namespace unravel
