#include "screen_art.h"

#include <array>
#include <cctype>
#include <vector>

namespace unravel::screen_art
{
namespace
{
// A project takes its color from its name, so it keeps it wherever it shows up.
constexpr std::array<ImU32, 8> SCREEN_ART_NAME_COLORS{IM_COL32(58, 121, 187, 255),
                                                     IM_COL32(76, 153, 120, 255),
                                                     IM_COL32(184, 120, 64, 255),
                                                     IM_COL32(150, 96, 170, 255),
                                                     IM_COL32(190, 84, 84, 255),
                                                     IM_COL32(70, 150, 160, 255),
                                                     IM_COL32(170, 150, 70, 255),
                                                     IM_COL32(110, 120, 140, 255)};
// How much lighter the top left and darker the bottom right of a gradient are than its color.
constexpr float SCREEN_ART_GRADIENT_LIGHTEN = 0.18f;
constexpr float SCREEN_ART_GRADIENT_DARKEN = 0.45f;
// The initial of a placeholder, in parts of the height of the rect.
constexpr float SCREEN_ART_INITIAL_SCALE = 0.42f;
constexpr ImU32 SCREEN_ART_INITIAL_COLOR = IM_COL32(255, 255, 255, 215);
constexpr ImU32 SCREEN_ART_INNER_BORDER_COLOR = IM_COL32(255, 255, 255, 22);
constexpr ImU32 SCREEN_ART_SPINNER_TRACK_COLOR = IM_COL32(255, 255, 255, 16);
constexpr float SCREEN_ART_SPINNER_ARC = IM_PI * 1.4f;
constexpr float SCREEN_ART_SPINNER_SPEED = 6.0f;

auto scale_color(ImU32 color, float factor) -> ImU32
{
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.x = ImSaturate(value.x * factor);
    value.y = ImSaturate(value.y * factor);
    value.z = ImSaturate(value.z * factor);
    return ImGui::ColorConvertFloat4ToU32(value);
}

auto is_word_character(char character) -> bool
{
    return std::isalnum(static_cast<unsigned char>(character)) != 0;
}

/// The first letter of every word of the name: words end at anything that is not a letter or a
/// digit, and where a lower case letter meets an upper case one ("SimpleSurvivor").
auto get_word_initials(const std::string& name) -> std::vector<char>
{
    std::vector<char> initials;
    char previous = 0;
    for(const char character : name)
    {
        const bool is_camel_start = std::islower(static_cast<unsigned char>(previous)) != 0 &&
                                    std::isupper(static_cast<unsigned char>(character)) != 0;
        if(is_word_character(character) && (!is_word_character(previous) || is_camel_start))
        {
            initials.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
        }
        previous = character;
    }
    return initials;
}

/// The initials of the first and of the last word of the name: "SimpleSurvivor" is "SS".
auto get_monogram(const std::string& name) -> std::string
{
    const std::vector<char> initials = get_word_initials(name);
    if(initials.empty())
    {
        return "?";
    }
    std::string monogram(1, initials.front());
    if(initials.size() > 1)
    {
        monogram += initials.back();
    }
    return monogram;
}
} // namespace

auto get_name_color(const std::string& name) -> ImU32
{
    return SCREEN_ART_NAME_COLORS[ImHashStr(name.c_str()) % SCREEN_ART_NAME_COLORS.size()];
}

void draw_image_cover(ImDrawList* draw_list,
                      const gfx::texture::ptr& texture,
                      const ImRect& rect,
                      float rounding,
                      ImDrawFlags corners,
                      ImU32 tint)
{
    if(!texture || rect.GetWidth() <= 0.0f || rect.GetHeight() <= 0.0f)
    {
        return;
    }
    const ImVec2 texture_size = ImGui::GetSize(texture);
    if(texture_size.x <= 0.0f || texture_size.y <= 0.0f)
    {
        return;
    }
    const float texture_aspect = texture_size.x / texture_size.y;
    const float rect_aspect = rect.GetWidth() / rect.GetHeight();
    ImVec2 uv_min(0.0f, 0.0f);
    ImVec2 uv_max(1.0f, 1.0f);
    if(texture_aspect > rect_aspect)
    {
        const float crop = (1.0f - rect_aspect / texture_aspect) * 0.5f;
        uv_min.x = crop;
        uv_max.x = 1.0f - crop;
    }
    else
    {
        const float crop = (1.0f - texture_aspect / rect_aspect) * 0.5f;
        uv_min.y = crop;
        uv_max.y = 1.0f - crop;
    }
    draw_list->AddImageRounded(ImGui::ToId(texture), rect.Min, rect.Max, uv_min, uv_max, tint, rounding, corners);
}

void draw_gradient(ImDrawList* draw_list, const ImRect& rect, ImU32 color, float rounding, ImDrawFlags corners)
{
    const int first_vertex = draw_list->VtxBuffer.Size;
    draw_list->AddRectFilled(rect.Min, rect.Max, IM_COL32_WHITE, rounding, corners);
    const int last_vertex = draw_list->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw_list,
                                                  first_vertex,
                                                  last_vertex,
                                                  rect.Min,
                                                  rect.Max,
                                                  scale_color(color, 1.0f + SCREEN_ART_GRADIENT_LIGHTEN),
                                                  scale_color(color, 1.0f - SCREEN_ART_GRADIENT_DARKEN));
}

void draw_placeholder(ImDrawList* draw_list,
                      const ImRect& rect,
                      const std::string& name,
                      float rounding,
                      ImDrawFlags corners,
                      const char* glyph)
{
    draw_gradient(draw_list, rect, get_name_color(name), rounding, corners);
    draw_list->AddRect(rect.Min, rect.Max, SCREEN_ART_INNER_BORDER_COLOR, rounding, corners);
    const std::string initial = glyph != nullptr ? std::string(glyph) : get_monogram(name);
    // The icon glyphs are merged into the bold weight, not into the black one.
    ImFont* font = ImGui::GetFont(glyph != nullptr ? ImGui::Font::Bold : ImGui::Font::Black);
    const float font_size = ImFloor(rect.GetHeight() * SCREEN_ART_INITIAL_SCALE);
    const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, initial.c_str());
    const ImVec2 text_pos(ImFloor(rect.GetCenter().x - text_size.x * 0.5f), ImFloor(rect.GetCenter().y - text_size.y * 0.5f));
    draw_list->AddText(font, font_size, text_pos, SCREEN_ART_INITIAL_COLOR, initial.c_str());
}

void draw_spinner(ImDrawList* draw_list, const ImVec2& center, float radius, float thickness, ImU32 color)
{
    const float start_angle = static_cast<float>(ImGui::GetTime()) * SCREEN_ART_SPINNER_SPEED;
    draw_list->AddCircle(center, radius, SCREEN_ART_SPINNER_TRACK_COLOR, 0, thickness);
    draw_list->PathArcTo(center, radius, start_angle, start_angle + SCREEN_ART_SPINNER_ARC);
    draw_list->PathStroke(color, ImDrawFlags_None, thickness);
}
} // namespace unravel::screen_art
