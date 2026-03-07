#include "utils.h"
#include "imgui/imgui.h"
#include <chrono>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ImGui
{

namespace
{

enum class size_fit
{
    shrink_to_fit,
    stretch_to_fit,
    auto_fit
};

enum class dimension_fit
{
    x,
    y,
    uniform,
    non_uniform
};

ImVec2 fit_item(float item_w, float item_h, float area_w, float area_h, size_fit sz_fit, dimension_fit dim_fit)
{
    float xscale = 1.0f;
    float yscale = 1.0f;

    item_w = std::max(item_w, 1.0f);
    item_h = std::max(item_h, 1.0f);

    switch(sz_fit)
    {
        case size_fit::shrink_to_fit:
        {
            if(item_w > area_w)
            {
                xscale = std::min(xscale, float(area_w) / item_w);
            }
            if(item_h > area_h)
            {
                yscale = std::min(yscale, float(area_h) / item_h);
            }
        }
        break;

        case size_fit::stretch_to_fit:
        {
            if(item_w < area_w)
            {
                xscale = std::max(xscale, float(area_w) / item_w);
            }
            if(item_h < area_h)
            {
                yscale = std::max(yscale, float(area_h) / item_h);
            }
        }
        break;

        case size_fit::auto_fit:
        {
            if(item_w > area_w)
            {
                xscale = std::min(xscale, float(area_w) / item_w);
            }
            else
            {
                xscale = std::max(xscale, float(area_w) / item_w);
            }

            if(item_h > area_h)
            {
                yscale = std::min(yscale, float(area_h) / item_h);
            }
            else
            {
                yscale = std::max(yscale, float(area_h) / item_h);
            }
        }
    }

    switch(dim_fit)
    {
        case dimension_fit::x:
            yscale = 1.0f;
            break;

        case dimension_fit::y:
            xscale = 1.0f;
            break;

        case dimension_fit::uniform:
        {
            float uniform_scale = std::min(xscale, yscale);
            xscale = uniform_scale;
            yscale = uniform_scale;
        }
        break;
        case dimension_fit::non_uniform:
            break;
    }

    return {xscale, yscale};
}

// Draw a small "grip" icon inside rect (unstyled handle)
void draw_grip(ImDrawList* dl, ImRect r, ImU32 col)
{
    // draw 2 columns * 3 rows of dots
    const float w = r.GetWidth();
    const float h = r.GetHeight();
    const float cx = r.Min.x + w * 0.5f;
    const float cy = r.Min.y + h * 0.5f;

    const float dx = w * 0.18f;
    const float dy = h * 0.18f;
    const float rad = (r.GetHeight()) * 0.07f;

    for(int row = -1; row <= 1; ++row)
    {
        for(int colx = -1; colx <= 1; colx += 2)
        {
            ImVec2 p(cx + colx * dx, cy + row * dy);
            dl->AddCircleFilled(p, rad, col);
        }
    }
}

bool IsItemDisabled()
{
    return ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
}

ImRect RectExpanded(const ImRect& rect, float x, float y)
{
    ImRect result = rect;
    result.Min.x -= x;
    result.Min.y -= y;
    result.Max.x += x;
    result.Max.y += y;
    return result;
}

struct ImGuiDataTypeInfo
{
    size_t Size;
    const char* PrintFmt; // Unused
    const char* ScanFmt;
};

static const ImGuiDataTypeInfo typeinfos[] = {
    {sizeof(char), "%d", "%d"}, // ImGuiDataType_S8
    {sizeof(unsigned char), "%u", "%u"},
    {sizeof(short), "%d", "%d"}, // ImGuiDataType_S16
    {sizeof(unsigned short), "%u", "%u"},
    {sizeof(int), "%d", "%d"}, // ImGuiDataType_S32
    {sizeof(unsigned int), "%u", "%u"},
#ifdef _MSC_VER
    {sizeof(ImS64), "%I64d", "%I64d"}, // ImGuiDataType_S64
    {sizeof(ImU64), "%I64u", "%I64u"},
#else
    {sizeof(ImS64), "%lld", "%lld"}, // ImGuiDataType_S64
    {sizeof(ImU64), "%llu", "%llu"},
#endif
    {sizeof(float), "%f", "%f"},   // ImGuiDataType_Float (float are promoted to double in va_arg)
    {sizeof(double), "%f", "%lf"}, // ImGuiDataType_Double
    {sizeof(bool), "bool", "%d"    },  // ImGuiDataType_Bool
    { 0, "char*","%s"    },  // ImGuiDataType_String

};
IM_STATIC_ASSERT(IM_ARRAYSIZE(typeinfos) == ImGuiDataType_COUNT);


} // namespace

auto GetDataPrintFormat(ImGuiDataType data_type) -> const char*
{
    return typeinfos[data_type].PrintFmt;
}

bool DragMultiFormatScalarN(const char* label,
                            ImGuiDataType data_type,
                            void* p_data,
                            int components,
                            float v_speed,
                            const void* p_min,
                            const void* p_max,
                            const char** format,
                            ImGuiSliderFlags flags)
{
    ImGuiWindow* window = GetCurrentWindow();
    if(window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    bool value_changed = false;
    BeginGroup();
    PushID(label);
    PushMultiItemsWidths(components, CalcItemWidth());
    size_t type_size = typeinfos[data_type].Size;
    for(int i = 0; i < components; i++)
    {
        PushID(i);
        if(i > 0)
            SameLine(0, g.Style.ItemInnerSpacing.x);
        value_changed |= DragScalar("", data_type, p_data, v_speed, p_min, p_max, format[i], flags);
        DrawItemActivityOutline();

        PopID();
        PopItemWidth();
        p_data = (void*)((char*)p_data + type_size);
    }
    PopID();

    const char* label_end = FindRenderedTextEnd(label);
    if(label != label_end)
    {
        SameLine(0, g.Style.ItemInnerSpacing.x);
        TextEx(label, label_end);
    }

    EndGroup();
    return value_changed;
}

bool DragVecN(const char* label,
              ImGuiDataType data_type,
              void* p_data,
              int components,
              float v_speed,
              const void* p_min,
              const void* p_max,
              const void* p_default_data,
              const char* format,
              ImGuiSliderFlags flags)
{
    ImGuiWindow* window = GetCurrentWindow();
    if(window->SkipItems)
        return false;

    std::array<const char*, 4> labels = {{"X", "Y", "Z", "W"}};
    std::array<ImColor, 4> colors = {
        {ImColor(125, 0, 0), ImColor(0, 125, 0), ImColor(0, 0, 125), ImColor(0, 125, 125)}};

    ImGuiContext& g = *GImGui;
    bool value_changed = false;
    BeginGroup();
    PushID(label);

    auto w = CalcItemWidth();
    for(int i = 0; i < components; ++i)
    {
        const ImVec2 label_size = CalcTextSize(labels[i], NULL, true);
        float padded_size = label_size.x + GetStyle().FramePadding.x * 2.0f;
        w -= padded_size;
    }
    w -= GetStyle().ItemInnerSpacing.x * components;

    PushMultiItemsWidths(components, w);
    size_t type_size = typeinfos[data_type].Size;
    for(int i = 0; i < components; i++)
    {
        PushID(i);
        if(i > 0)
            SameLine(0, g.Style.ItemInnerSpacing.x);

        PushStyleColor(ImGuiCol_Button, colors[i].Value);
        if(Button(labels[i]))
        {
            value_changed = true;
            if(p_default_data)
            {
                memcpy(p_data, p_default_data, type_size);
            }

            MarkItemEdited(ImGui::GetItemID());
        }
        PopStyleColor();
        SameLine(0.0f, GetStyle().ItemInnerSpacing.x);

        value_changed |= DragScalar("", data_type, p_data, v_speed, p_min, p_max, format, flags);
        DrawItemActivityOutline();

        PopID();
        PopItemWidth();
        p_data = (void*)((char*)p_data + type_size);

        if(p_default_data)
        {
            p_default_data = (void*)((char*)p_default_data + type_size);
        }
    }
    PopID();

    const char* label_end = FindRenderedTextEnd(label);
    if(label != label_end)
    {
        SameLine(0, g.Style.ItemInnerSpacing.x);
        TextEx(label, label_end);
    }

    EndGroup();
    return value_changed;
}

void AlignedItem(float align, float totalWidth, float itemWidth, const std::function<void()>& itemDrawFn)
{
    float offset = totalWidth - itemWidth;
    float leftOffset = offset * align;

    SetCursorPosX(GetCursorPosX() + leftOffset);
    itemDrawFn();
}


void AlignedItemV(float align, float totalHeight, float itemHeight, const std::function<void()>& itemDrawFn)
{
    float offset = totalHeight - itemHeight;
    float topOffset = offset * align;

    SetCursorPosY(GetCursorPosY() + topOffset);
    itemDrawFn();
}

std::string GetKeyCombinationName(const ImGuiKeyCombination& keys)
{
    std::string result{};
    for(size_t i = 0; i < keys.size(); ++i)
    {
        const auto& key = keys[i];
        result += GetKeyName(key);

        if(i + 1 < keys.size())
        {
            result += " + ";
        }
    }
    return result;
}

bool IsCombinationKeyPressed(const ImGuiKeyCombination& keys, bool repeat)
{
    bool has_non_modifier_key = false;

    for(size_t i = 0; i < keys.size(); ++i)
    {
        if(!IsKeyDown(keys[i]))
        {
            return false;
        }

        if(keys[i] < ImGuiKey_LeftCtrl || keys[i] > ImGuiKey_RightSuper)
        {
            has_non_modifier_key = true;
        }
    }

    auto& io = ImGui::GetIO();
    auto repeat_delay = io.KeyRepeatDelay;

    io.KeyRepeatDelay = 0.8f;


    for(size_t i = 0; i < keys.size(); ++i)
    {
        //bool is_repeat = repeat && !(keys[i] >= ImGuiKey_LeftCtrl && keys[i] <= ImGuiKey_RightSuper);

        bool modifier_key = keys[i] >= ImGuiKey_LeftCtrl && keys[i] <= ImGuiKey_RightSuper;

        bool skip_modifier_key = has_non_modifier_key && modifier_key;
        if(skip_modifier_key)
        {
            continue;
        }

        if(IsKeyPressed(keys[i], repeat))
        {
            return true;
        }
    }

    io.KeyRepeatDelay = repeat_delay;

    return false;
}

bool IsItemCombinationKeyPressed(const ImGuiKeyCombination& keys)
{
    if(IsWindowFocused())
    {
        // if(!IsAnyItemActive())
        {
            if(IsCombinationKeyPressed(keys))
            {
                return true;
            }
        }
    }

    return false;
}

bool IsItemDoubleClicked(ImGuiMouseButton mouse_button)
{
    return IsMouseDoubleClicked(mouse_button) && IsItemHovered(ImGuiHoveredFlags_None);
}

bool IsItemReleased(ImGuiMouseButton mouse_button)
{
    return IsMouseReleased(mouse_button) && IsItemHovered(ImGuiHoveredFlags_None);
}

bool IsItemKeyPressed(ImGuiKey key, bool repeat)
{
    bool result = false;
    if(IsWindowFocused())
    {
        if(!IsAnyItemActive())
        {
            if(IsKeyPressed(key, repeat))
            {
                result = true;
            }
        }
    }

    return result;
}
bool IsItemKeyReleased(ImGuiKey key)
{
    bool result = false;
    if(IsWindowFocused())
    {
        if(!IsAnyItemActive())
        {
            if(IsKeyReleased(key))
            {
                result = true;
            }
        }
    }

    return result;
}

bool IsItemFocusChanged()
{
    if(!ImGui::IsWindowFocused())
    {
        return false;
    }
    // Track the last focused item ID to detect changes
    static ImGuiID last_focused_id = 0;
    static ImGuiID last_window_id = 0;

    
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiContext* context = ImGui::GetCurrentContext();
    
    // Reset tracking when window changes to avoid false positives
    if (last_window_id != window->ID)
    {
        last_window_id = window->ID;
        last_focused_id = window->NavLastIds[context->NavLayer];
    }
    
    // Get current item ID
    ImGuiID current_id = ImGui::GetItemID();
    bool is_focused = ImGui::IsItemFocused();
    
    // Check if focus has changed and was triggered by navigation
    if (is_focused && last_focused_id != current_id)
    {
        // Only return true if this was triggered by keyboard/gamepad navigation
        bool is_nav_request = (context->NavJustMovedToId == current_id);
        
        // Update the last focused ID
        last_focused_id = current_id;
        return is_nav_request;
    }
    
    return false;
}

void RenderFocusFrame(ImVec2 p_min, ImVec2 p_max, ImU32 color, float thickness)
{
    ImGuiNavRenderCursorFlags flags = ImGuiNavRenderCursorFlags_None;

    ImGuiContext& g = *GetCurrentContext();
    ImGuiWindow* window = GetCurrentWindow();

    ImRect bb(p_min, p_max);

    float rounding = (flags & ImGuiNavRenderCursorFlags_NoRounding) ? 0.0f : g.Style.FrameRounding;
    ImRect display_rect = bb;

    window->DrawList->AddRect(display_rect.Min, display_rect.Max, color, rounding, 0, thickness);
}

void SetItemFocusFrame(ImU32 color, float thickness)
{
    RenderFocusFrame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), color, thickness);
}

void SameLineInner()
{
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
}

void RenderFrameEx(ImVec2 p_min, ImVec2 p_max, float rounding, float thickness)
{
    ImGuiWindow* window = GetCurrentWindow();

    if(rounding < 0)
    {
        rounding = ImGui::GetStyle().FrameRounding;
    }

    window->DrawList->AddRect(p_min + ImVec2(1, 1),
                              p_max + ImVec2(1, 1),
                              GetColorU32(ImGuiCol_BorderShadow),
                              rounding,
                              0,
                              thickness);
    window->DrawList->AddRect(p_min, p_max, GetColorU32(ImGuiCol_Border), rounding, 0, thickness);
}

void Spinner(float diameter, float thickness, int num_segments, float speed, ImU32 color)
{
    auto window = GetCurrentWindow();
    if(window->SkipItems)
    {
        return;
    }

    auto& g = *ImGui::GetCurrentContext();
    auto pos = window->DC.CursorPos;

    auto padding = ImGui::GetStyle().FramePadding;

    pos += padding;
    ImVec2 size{diameter + thickness, diameter + thickness};
    const ImRect bb{pos, pos + size};
    ItemSize(bb);
    if(!ItemAdd(bb, 0))
    {
        return;
    }
    diameter -= thickness * 0.5f;

    float radius = diameter * 0.5f;
    auto time = static_cast<float>(g.Time) * speed;
    window->DrawList->PathClear();
    int start = static_cast<int>(abs(ImSin(time) * (num_segments - 5)));
    const float a_min = IM_PI * 2.0f * float(start) / float(num_segments);
    const float a_max = IM_PI * 2.0f * float(num_segments - 3) / float(num_segments);
    auto centre = pos;
    centre.x += radius;
    centre.y += radius;
    for(auto i = 0; i < num_segments; i++)
    {
        const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
        window->DrawList->PathLineTo(
            {centre.x + ImCos(a + time * 8) * radius, centre.y + ImSin(a + time * 8) * radius});
    }
    window->DrawList->PathStroke(GetColorU32(color), false, thickness);
}

void ImageWithAspect(ImTextureID texture,
                     ImVec2 texture_size,
                     ImVec2 size,
                     ImVec2 align,
                     const ImVec2& uv0,
                     const ImVec2& uv1)
{
    auto scale =
        fit_item(texture_size.x, texture_size.y, size.x, size.y, size_fit::shrink_to_fit, dimension_fit::uniform);

    texture_size = texture_size * scale;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size.y - texture_size.y) * align.y);

    AlignedItem(align.x,
                size.x,
                texture_size.x,
                [&]()
                {
                    Image(texture, texture_size, uv0, uv1);
                });
}

bool ContentButtonItem(const ContentItem& item)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems)
        return false;

    ImVec2 size = item.image_size;
    if(size.x <= 0 && size.y <= 0)
    {
        size.x = size.y = ImGui::GetTextLineHeightWithSpacing();
    }
    else
    {
        if(size.x <= 0)
            size.x = size.y;
        else if(size.y <= 0)
            size.y = size.x;
    }

    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiStyle& style = g.Style;

    const ImGuiID id = window->GetID(item.name);

    if(item.name_font)
    {
        ImGui::PushFont(item.name_font, item.name_font->LegacySize);
    }
    ImVec2 textSize{};

    if(item.name)
    {
        textSize = ImGui::CalcTextSize(item.name, nullptr, true);
    }
    if(item.name_font)
    {
        ImGui::PopFont();
    }

    if(item.type_font)
    {
        ImGui::PushFont(item.type_font, item.type_font->LegacySize);
    }
    ImVec2 typeSize{};

    if(item.type)
    {
        typeSize = ImGui::CalcTextSize(item.type, nullptr, true);
    }
    if(item.type_font)
    {
        ImGui::PopFont();
    }
    ImVec2 textPadding(6.0f, style.ItemInnerSpacing.y * 2.0f);

    ImVec2 padding = {0.0f, 0.0f};

    if(textSize.x < 1.0f)
    {
        padding = {};
        textPadding = {};
        textSize.y = {};
    }

    if(typeSize.x < 1.0f)
    {
        typeSize.y = {};
    }

    ImVec2 totalSize(size.x, size.y + textSize.y + typeSize.y + textPadding.y);

    ImRect bb(window->DC.CursorPos, window->DC.CursorPos + totalSize + padding * 2);
    ImVec2 start = window->DC.CursorPos + padding;

    ImRect image_bb(start, start + size);
    image_bb.Expand(-2.0f);

    ItemSize(bb);
    if(!ItemAdd(bb, id))
        return false;

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    // Render
    const ImU32 col = GetColorU32((hovered && held) ? ImGuiCol_ButtonActive
                                  : hovered         ? ImGuiCol_ButtonHovered
                                                    : ImGuiCol_Button);

    // Fit the texture in the bounding box.
    auto imgSz = item.texture_size;
    const auto fittingBoxSize = ImVec2(image_bb.GetWidth(), image_bb.GetHeight());

    auto scale =
        fit_item(imgSz.x, imgSz.y, fittingBoxSize.x, fittingBoxSize.y, size_fit::shrink_to_fit, dimension_fit::uniform);
    imgSz *= scale;

    image_bb.Min.x += (fittingBoxSize.x - imgSz.x) * 0.5f;
    image_bb.Min.y += (fittingBoxSize.y - imgSz.y) * 0.5f;

    image_bb.Max = image_bb.Min + imgSz;

    RenderFrame(bb.Min, bb.Max, col, true, ImClamp((float)ImMin(padding.x, padding.y), 0.0f, style.FrameRounding));
    if(item.bg_col.w > 0.0f)
        window->DrawList->AddRectFilled(image_bb.Min, image_bb.Max, GetColorU32(item.bg_col), style.FrameRounding);

    window->DrawList->AddImageRounded(item.texId,
                                      image_bb.Min,
                                      image_bb.Max,
                                      item.uv0,
                                      item.uv1,
                                      GetColorU32(item.tint_col),
                                      style.FrameRounding);

    if(textSize.x > 0)
    {
        start.x += textPadding.x;
        totalSize.x -= 2.0f * textPadding.x;

        start.y += fittingBoxSize.y + style.ItemInnerSpacing.y;

        auto originalStart = start;
        if(totalSize.x > textSize.x)
        {
            start.x += (totalSize.x - textSize.x) * 0.5f;
        }

        if(item.name_font)
        {
            ImGui::PushFont(item.name_font, item.name_font->LegacySize);
        }

        auto end = start + ImVec2(totalSize.x, textSize.y);

        auto elipsis_max = end.x;
        ImGui::RenderTextEllipsis(window->DrawList,
                                  start,
                                  end,
                                  elipsis_max,
                                  item.name,
                                  nullptr,
                                  &textSize);

        if(item.name_font)
        {
            ImGui::PopFont();
        }

        if(item.type_font)
        {
            ImGui::PushFont(item.type_font, item.type_font->LegacySize);
        }

        start = originalStart;
        start.y += textSize.y + style.ItemInnerSpacing.y;

        if(totalSize.x > typeSize.x)
        {
            start.x += (totalSize.x - typeSize.x) * 0.5f;
        }
        
        end = start + ImVec2(totalSize.x, typeSize.y);
        elipsis_max = end.x;

        ImGui::RenderTextEllipsis(window->DrawList,
                                  start,
                                  end,
                                  elipsis_max,
                                  item.type,
                                  nullptr,
                                  &typeSize);

        if(item.type_font)
        {
            ImGui::PopFont();
        }
    }
    return pressed;
}

ImVec2 CalcItemSize(const char* label, ImVec2 size_arg)
{
    const auto& style = GetStyle();
    const auto label_size = CalcTextSize(label, NULL, true);
    auto size =
        CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);
    return size;
}

void ItemBrowser(float item_width, size_t items_count, const std::function<void(int)>& callback)
{
    const auto& style = GetStyle();

    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));

    auto avail = GetContentRegionAvail().x;
    // add one extra item spacing because we are adding it for every item, but should not for the last one on the line
    avail += style.ItemSpacing.x;
    auto item_size = item_width + style.ItemSpacing.x;
    auto items_per_line_exact = avail / item_size;
    auto items_per_line_floor = ImMax(1.0f, ImFloor(items_per_line_exact));
    auto items_per_line = ImMin(size_t(items_per_line_floor), items_count);
    auto extra = ((items_per_line_exact - items_per_line_floor) * item_size) / ImMax(1.0f, items_per_line_floor - 1);

    if(float(items_count) < items_per_line_exact)
    {
        extra = {};
    }
    auto lines = items_per_line > 0 ? int(ImCeil(float(items_count) / float(items_per_line))) : 0;
    ImGuiListClipper clipper;
    clipper.Begin(lines);

    while(clipper.Step())
    {
        for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            auto start = size_t(i) * items_per_line;
            auto end = start + ImMin(items_count - start, items_per_line);
            for(size_t j = start; j < end; ++j)
            {
                PushID(int(j));

                callback(j);

                PopID();

                if(j != end - 1)
                {
                    SameLine(0.0f, style.ItemSpacing.x + extra);
                }
            }
        }
    }
    PopStyleVar();
}

bool ImageMenuItem(ImTextureID texture, const char* tooltip, bool selected, bool enabled)
{
    ImVec4 bg_color(0, 0, 0, 0);

    ImVec2 size(GetTextLineHeight(), GetTextLineHeight());
    bool ret = false;

    {
        ImVec4 tintColor(1.0f, 1.0f, 1.0f, 1.0f);

        if(!enabled)
        {
            PushItemFlag(ImGuiItemFlags_Disabled, true);
            tintColor = ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
        }

        if(ImageButton(std::to_string(uintptr_t(texture)).c_str(), texture, size, ImVec2(0, 0), ImVec2(1, 1), bg_color, tintColor))
        {
            ret = true;
        }

        if(!enabled)
        {
            PopItemFlag();
        }
    }
    if(tooltip && IsItemHovered())
    {
        SetTooltip("%s", tooltip);
    }

    if(selected)
    {
        ImVec2 rectMin = GetItemRectMin();
        ImVec2 rectMax = GetItemRectMax();
        RenderFocusFrame(rectMin, rectMax, ImColor(ImVec4(1.0f, 0.6f, 0.0f, 1.0f)));
    }

    return ret;
}

WindowTimeBlock::WindowTimeBlock(ImFont* font)
{
    start_ = clock_t::now();
    font_ = font;
}

WindowTimeBlock::~WindowTimeBlock()
{
    using duration_t = std::chrono::duration<float, std::milli>;

    auto end = clock_t::now();
    auto dur = std::chrono::duration_cast<duration_t>(end - start_);

    char text[32];
    ImFormatString(text, IM_ARRAYSIZE(text), "%.3fms", dur.count());

    ImGui::PushFont(font_, font_->LegacySize);
    auto textSize = ImGui::CalcTextSize(text);

    auto windowPos = ImGui::GetWindowPos();
    auto windowSize = ImGui::GetWindowSize();

    auto textPos = windowPos + windowSize - textSize - ImGui::GetStyle().WindowPadding;
    ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), text);
    ImGui::PopFont();
}

bool IsDragDropPossibleTargetForType(const char* type)
{
    auto testPaylopad = ImGui::GetDragDropPayload();
    {
        if(testPaylopad && testPaylopad->IsDataType(type))
        {
            return true;
        }
    }

    return false;
}

void DrawItemActivityOutline(OutlineFlags flags, ImColor colourHighlight, float rounding)
{
    if(IsItemDisabled())
        return;

    auto* drawList = ImGui::GetWindowDrawList();
    ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    rect = RectExpanded(rect, -0.5f, -0.5f);

    if(rounding < 0.0f)
    {
        rounding = ImGui::GetStyle().FrameRounding;
    }
    if((flags & OutlineFlags_WhenActive) && ImGui::IsItemActive() || (flags & OutlineFlags_WhenCalled))
    {
        if(flags & OutlineFlags_HighlightActive)
        {
            drawList->AddRect(rect.Min, rect.Max, colourHighlight, rounding, 0, 1.5f);
        }
        else
        {
            drawList->AddRect(rect.Min, rect.Max, ImColor(60, 60, 60), rounding, 0, 1.5f);
        }
    }
    else if((flags & OutlineFlags_WhenHovered) && ImGui::IsItemHovered() && !ImGui::IsItemActive())
    {
        drawList->AddRect(rect.Min, rect.Max, ImColor(60, 60, 60), rounding, 0, 1.5f);
    }
    else if((flags & OutlineFlags_WhenInactive) && !ImGui::IsItemHovered() && !ImGui::IsItemActive())
    {
        drawList->AddRect(rect.Min, rect.Max, ImColor(50, 50, 50), rounding, 0, 1.0f);
    }
}

void DrawFilterWithHint(ImGuiTextFilter& filter, const char* hint_text, float width)
{
    // Start an input text with filter
    ImGui::PushID(&filter);
    ImGui::SetNextItemWidth(width);

    if(ImGui::InputText("##Filter", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf), ImGuiInputTextFlags_AutoSelectAll))
    {
        filter.Build();
    }
    ImGui::PopID();

    // Check if the filter text is empty
    if(filter.InputBuf[0] == '\0' && !ImGui::IsItemActive())
    {
        auto offset = ImGui::GetStyle().FramePadding.x;
        // Draw the hint text
        ImVec2 pos = ImGui::GetItemRectMin();
        pos.x += offset;
        ImVec2 size = ImGui::GetItemRectSize();
        size.x -= 2.0f * offset;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Set hint text color
        ImGui::RenderTextClipped(pos,
                                 ImVec2(pos.x + size.x, pos.y + size.y),
                                 hint_text,
                                 nullptr,
                                 nullptr,
                                 ImVec2(0.0f, 0.5f));
        ImGui::PopStyleColor();
    }
}

void WrapMousePos(int axises_mask, const ImVec2& wrap_rect_min, const ImVec2& wrap_rect_max)
{
    ImRect wrap_rect(wrap_rect_min, wrap_rect_max);
    ImGuiContext& g = *GImGui;
    IM_ASSERT(axises_mask == 1 || axises_mask == 2 || axises_mask == (1 | 2));
    ImVec2 p_mouse = g.IO.MousePos;
    for(int axis = 0; axis < 2; axis++)
    {
        if((axises_mask & (1 << axis)) == 0)
            continue;
        float size = wrap_rect.Max[axis] - wrap_rect.Min[axis];
        if(p_mouse[axis] >= wrap_rect.Max[axis])
            p_mouse[axis] = wrap_rect.Min[axis] + 1.0f;
        else if(p_mouse[axis] <= wrap_rect.Min[axis])
            p_mouse[axis] = wrap_rect.Max[axis] - 1.0f;
    }
    if(p_mouse.x != g.IO.MousePos.x || p_mouse.y != g.IO.MousePos.y)
        TeleportMousePos(p_mouse);
}

void WrapMousePos(int axises_mask)
{
    ImGuiContext& g = *GetCurrentContext();
#ifdef IMGUI_HAS_DOCK
    if(g.IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        const ImGuiPlatformMonitor* monitor = GetViewportPlatformMonitor(g.MouseViewport);
        WrapMousePos(axises_mask, monitor->MainPos, monitor->MainPos + monitor->MainSize - ImVec2(1, 1));
    }
    else
#endif
    {
        ImGuiViewport* viewport = GetMainViewport();
        WrapMousePos(axises_mask, viewport->Pos, viewport->Pos + viewport->Size - ImVec2(1, 1));
    }
}

void WrapMousePos()
{
    WrapMousePos(1 << ImGuiAxis_X | 1 << ImGuiAxis_Y);
}

void ActiveItemWrapMousePos()
{
    ImGuiContext& g = *GetCurrentContext();
    ImGuiID id = GetItemID();

    if (IsItemActive() && (!GetInputTextState(id) || g.InputTextDeactivatedState.ID == id))
    {
        WrapMousePos(1 << ImGuiAxis_X);
    }
}

void ActiveItemWrapMousePos(const ImVec2& wrap_rect_min, const ImVec2& wrap_rect_max)
{
    ImGuiContext& g = *GetCurrentContext();
    ImGuiID id = GetItemID();

    if (IsItemActive() && (!GetInputTextState(id) || g.InputTextDeactivatedState.ID == id))
    {
        WrapMousePos(1 << ImGuiAxis_X, wrap_rect_min, wrap_rect_max);
    }
}


void SetNextWindowViewportToCurrent()
{
    auto win = GetCurrentWindow();
    if(win)
    {
        SetNextWindowViewport(win->ViewportId);
    }
}

int PlotEx(ImGuiPlotType plot_type, const char* label, ImRange (*values_getter)(void* data, int idx), void* data, int values_count, int values_offset, const char* overlay_text, float scale_min, float scale_max, const ImVec2& size_arg)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return -1;

    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const ImVec2 frame_size = CalcItemSize(size_arg, CalcItemWidth(), label_size.y + style.FramePadding.y * 2.0f);

    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + frame_size);
    const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
    const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0));
    ItemSize(total_bb, style.FramePadding.y);
    if (!ItemAdd(total_bb, id, &frame_bb, ImGuiItemFlags_NoNav))
        return -1;
    bool hovered;
    ButtonBehavior(frame_bb, id, &hovered, NULL);

    // Determine scale from values if not specified
    if (scale_min == FLT_MAX || scale_max == FLT_MAX)
    {
        float v_min = FLT_MAX;
        float v_max = -FLT_MAX;
        for (int i = 0; i < values_count; i++)
        {
            const auto range = values_getter(data, i);
            // Check for NaN values in both min and max
            if (range.min != range.min || range.max != range.max)
                continue;
            v_min = ImMin(v_min, ImMin(range.min, range.max));
            v_max = ImMax(v_max, ImMax(range.min, range.max));
        }
        if (scale_min == FLT_MAX)
            scale_min = v_min;
        if (scale_max == FLT_MAX)
            scale_max = v_max;
    }

    RenderFrame(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

    const int values_count_min = (plot_type == ImGuiPlotType_Lines) ? 2 : 1;
    int idx_hovered = -1;
    if (values_count >= values_count_min)
    {
        int res_w = ImMin((int)frame_size.x, values_count) + ((plot_type == ImGuiPlotType_Lines) ? -1 : 0);
        int item_count = values_count + ((plot_type == ImGuiPlotType_Lines) ? -1 : 0);

        // Tooltip on hover
        if (hovered && inner_bb.Contains(g.IO.MousePos))
        {
            const float t = ImClamp((g.IO.MousePos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x), 0.0f, 0.9999f);
            const int v_idx = (int)(t * item_count);
            IM_ASSERT(v_idx >= 0 && v_idx < values_count);

            const auto range0 = values_getter(data, (v_idx + values_offset) % values_count);
            if (plot_type == ImGuiPlotType_Lines)
            {
                const auto range1 = values_getter(data, (v_idx + 1 + values_offset) % values_count);
                SetTooltip("%d: [%8.4g, %8.4g]\n%d: [%8.4g, %8.4g]", 
                            v_idx, range0.min, range0.max, 
                            v_idx + 1, range1.min, range1.max);
            }
            else if (plot_type == ImGuiPlotType_Histogram)
            {
                SetTooltip("%d: [%8.4g, %8.4g]", v_idx, range0.min, range0.max);
            }
            idx_hovered = v_idx;
        }

        const float t_step = 1.0f / (float)res_w;
        const float inv_scale = (scale_min == scale_max) ? 0.0f : (1.0f / (scale_max - scale_min));

        auto range0 = values_getter(data, (0 + values_offset) % values_count);
        float t0 = 0.0f;
        // For ranges, we need both min and max positions
        ImVec2 tp0_min = ImVec2( t0, 1.0f - ImSaturate((range0.min - scale_min) * inv_scale) );
        ImVec2 tp0_max = ImVec2( t0, 1.0f - ImSaturate((range0.max - scale_min) * inv_scale) );
        float histogram_zero_line_t = (scale_min * scale_max < 0.0f) ? (1 + scale_min * inv_scale) : (scale_min < 0.0f ? 0.0f : 1.0f);   // Where does the zero line stands

        const ImU32 col_base = GetColorU32((plot_type == ImGuiPlotType_Lines) ? ImGuiCol_PlotLines : ImGuiCol_PlotHistogram);
        const ImU32 col_hovered = GetColorU32((plot_type == ImGuiPlotType_Lines) ? ImGuiCol_PlotLinesHovered : ImGuiCol_PlotHistogramHovered);

        for (int n = 0; n < res_w; n++)
        {
            const float t1 = t0 + t_step;
            const int v1_idx = (int)(t0 * item_count + 0.5f);
            IM_ASSERT(v1_idx >= 0 && v1_idx < values_count);
            const auto range1 = values_getter(data, (v1_idx + values_offset + 1) % values_count);
            const ImVec2 tp1_min = ImVec2( t1, 1.0f - ImSaturate((range1.min - scale_min) * inv_scale) );
            const ImVec2 tp1_max = ImVec2( t1, 1.0f - ImSaturate((range1.max - scale_min) * inv_scale) );

            // NB: Draw calls are merged together by the DrawList system. Still, we should render our batch are lower level to save a bit of CPU.
            if (plot_type == ImGuiPlotType_Lines)
            {
                // For lines, draw both min and max lines
                ImVec2 pos0_min = ImLerp(inner_bb.Min, inner_bb.Max, tp0_min);
                ImVec2 pos1_min = ImLerp(inner_bb.Min, inner_bb.Max, tp1_min);
                ImVec2 pos0_max = ImLerp(inner_bb.Min, inner_bb.Max, tp0_max);
                ImVec2 pos1_max = ImLerp(inner_bb.Min, inner_bb.Max, tp1_max);
                
                ImU32 color = idx_hovered == v1_idx ? col_hovered : col_base;
                window->DrawList->AddLine(pos0_min, pos1_min, color);
                window->DrawList->AddLine(pos0_max, pos1_max, color);
            }
            else if (plot_type == ImGuiPlotType_Histogram)
            {
                // For histogram, draw rectangle from min to max of the range
                ImVec2 pos_min = ImLerp(inner_bb.Min, inner_bb.Max, tp0_min);
                ImVec2 pos_max = ImLerp(inner_bb.Min, inner_bb.Max, tp0_max);
                
                // Create rectangle from min to max of the range
                ImVec2 rect_min = ImVec2(pos_min.x, ImMax(pos_min.y, pos_max.y));  // Top of rectangle (smaller y value)
                ImVec2 rect_max = ImVec2(pos_min.x + t_step * (inner_bb.Max.x - inner_bb.Min.x), ImMin(pos_min.y, pos_max.y));  // Bottom of rectangle (larger y value)
                
                if (rect_max.x >= rect_min.x + 2.0f)
                    rect_max.x -= 1.0f;
                
                window->DrawList->AddRectFilled(rect_min, rect_max, idx_hovered == v1_idx ? col_hovered : col_base);
            }

            t0 = t1;
            tp0_min = tp1_min;
            tp0_max = tp1_max;
        }
    }

    // Text overlay
    if (overlay_text)
        RenderTextClipped(ImVec2(frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y), frame_bb.Max, overlay_text, NULL, NULL, ImVec2(0.5f, 0.0f));

    if (label_size.x > 0.0f)
        RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);

    // Return hovered index or -1 if none are hovered.
    // This is currently not exposed in the public API because we need a larger redesign of the whole thing, but in the short-term we are making it available in PlotEx().
    return idx_hovered;
}

bool ReorderableList(
    const char* label,
    int item_count,
    const std::function<void(int index)>& draw_item,
    const std::function<void(int from, int insert_before)>& on_reorder)
{
    bool changed = false;
    const char* kPayload = "REORDER_LIST_ITEM_IDX";

    ImGui::TextUnformatted(label);
    ImGui::BeginChild(label, ImVec2(0, 240), true);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float row_h  = ImGui::GetFrameHeight();
    const float grip_w = row_h;

    int pending_from = -1;
    int pending_insert_before = -1;

    // ---- Single insertion preview state (chosen winner) ----
    bool  have_preview = false;
    float preview_x0 = 0.f, preview_x1 = 0.f, preview_y = 0.f;
    int   preview_insert_before = -1;
    int   preview_from = -1;
    float best_dist = FLT_MAX;

    const float mouseY = ImGui::GetIO().MousePos.y;

    ImGuiStyle& style = ImGui::GetStyle();

    // Create a drop zone above the first item
    if(item_count > 0)
    {
        ImVec2 first_row_start = ImGui::GetCursorScreenPos();
        
        // Create a drop zone above the first item
        ImRect firstDropRect;
        firstDropRect.Min = first_row_start;
        firstDropRect.Min.y -= style.ItemSpacing.y * 0.5f; // Half spacing above
        firstDropRect.Max = ImVec2(first_row_start.x + ImGui::GetContentRegionAvail().x, first_row_start.y + style.ItemSpacing.y * 0.5f);
        
        ImGui::SetCursorScreenPos(firstDropRect.Min);
        ImGui::InvisibleButton("##dropzone_first", firstDropRect.GetSize());
        ImGui::SetCursorScreenPos(first_row_start); // Restore cursor
        
        if(ImGui::BeginDragDropTarget())
        {
            if(const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(kPayload, ImGuiDragDropFlags_AcceptPeekOnly))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int from = *(const int*)payload->Data;
                
                // Insert before first item (index 0)
                const int insert_before = 0;
                const float y = first_row_start.y - style.ItemSpacing.y * 0.5f;
                
                const float dist = fabsf(mouseY - y);
                if(dist < best_dist && preview_insert_before != insert_before)
                {
                    best_dist = dist;
                    have_preview = true;
                    preview_from = from;
                    preview_insert_before = insert_before;
                    preview_x0 = firstDropRect.Min.x;
                    preview_x1 = firstDropRect.Max.x;
                    preview_y = y;
                }
                
                if(payload->IsDelivery())
                {
                    pending_from = from;
                    pending_insert_before = insert_before;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    for(int i = 0; i < item_count; ++i)
    {
        ImGui::PushID(i);

        // Save the row start position in SCREEN coordinates
        ImVec2 row_start = ImGui::GetCursorScreenPos();

        // --- Draw your row UI (unchanged) ---
        ImVec2 start = row_start;

        ImGui::BeginGroup();
        ImGui::InvisibleButton("##grip", ImVec2(grip_w, row_h));
        // ImRect gripRect(start, ImVec2(start.x + grip_w, start.y + row_h));
        ImRect gripRect;
        gripRect.Min = GetItemRectMin();
        gripRect.Max = GetItemRectMax();

        {
            ImU32 col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            if(ImGui::IsItemHovered()) col = ImGui::GetColorU32(ImGuiCol_Text);
            draw_grip(dl, gripRect, col);
        }

        if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload(kPayload, &i, sizeof(int));
            ImGui::Text("Item %d", i);
            ImGui::EndDragDropSource();
        }

        ImGui::SameLine();

        // Draw the item using the callback
        draw_item(i);
        ImGui::EndGroup();

        // Rect of the drawn UI (selectable is last item => gives good width)
        ImRect uiRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

        // ---- Create a drop zone that includes the vertical spacing gap ----
        // Expand down by ItemSpacing.y so the gap is not a dead zone.
        ImRect dropRect = uiRect;
        dropRect.Min.x = row_start.x;                  // ensure it covers from row start
        dropRect.Min.y = row_start.y;
        dropRect.Max.y += style.ItemSpacing.y;         // include the gap below

        // Place an InvisibleButton exactly over that dropRect.
        // We must set cursor to dropRect.Min before calling it.
        ImGui::SetCursorScreenPos(dropRect.Min);
        ImGui::InvisibleButton("##dropzone", dropRect.GetSize());

        // Restore cursor: continue layout below the row + spacing
        // (InvisibleButton moved cursor; put it back to where it should be)
        ImGui::SetCursorScreenPos(ImVec2(row_start.x, dropRect.Max.y));

        // Now use the dropzone as the target + rect reference
        if(ImGui::BeginDragDropTarget())
        {
            if(const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(kPayload, ImGuiDragDropFlags_AcceptPeekOnly))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int from = *(const int*)payload->Data;

                float halfGap = style.ItemSpacing.y * 0.5f;

                if(i == 0 || i == item_count - 1)
                {
                    halfGap = 0.0f;
                }
                // Decide insert position (same as before)
                const float midY = (dropRect.Min.y + dropRect.Max.y) * 0.5f;
                const bool  topHalf = (mouseY < midY);
                const int   insert_before = topHalf ? i : (i + 1);

                // NEW: stable line Y centered in the gap
                const float y = topHalf ? (uiRect.Min.y - halfGap) : (uiRect.Max.y + halfGap);

                const float dist = fabsf(mouseY - y);
                if(dist < best_dist && preview_insert_before != insert_before)
                {
                    best_dist = dist;
                    have_preview = true;
                    preview_from = from;
                    preview_insert_before = insert_before;
                    preview_x0 = uiRect.Min.x;
                    preview_x1 = uiRect.Max.x;
                    preview_y  = y;
                }

                if(payload->IsDelivery())
                {
                    pending_from = from;
                    pending_insert_before = insert_before;
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
    }

    // Draw exactly ONE preview line (winner)
    if(have_preview)
    {
        const float pad = 6.0f;
        dl->AddLine(
            ImVec2(preview_x0 + pad, preview_y),
            ImVec2(preview_x1 - pad, preview_y),
            ImGui::GetColorU32(ImGuiCol_DragDropTarget),
            2.0f
        );
    }

    // Execute pending move after loop
    if(pending_from != -1 && pending_insert_before != -1)
    {
        on_reorder(pending_from, pending_insert_before);
        changed = true;
    }

    ImGui::EndChild();
    return changed;
}

bool KnobSliderScalar(const char* label,
                      ImGuiDataType data_type,
                      void* p_data,
                      const void* p_min,
                      const void* p_max,
                      const char* format,
                      ImGuiSliderFlags flags)
{
    ImGuiWindow* window = GetCurrentWindow();
    if(window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const float w = CalcItemWidth();

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const float frame_h = label_size.y + style.FramePadding.y * 2.0f;
    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, frame_h));
    const ImRect total_bb(frame_bb.Min,
                          frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f,
                                                0.0f));

    const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    ItemSize(total_bb, style.FramePadding.y);
    if(!ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
        return false;

    if(format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
    bool temp_input_is_active = temp_input_allowed && TempInputIsActive(id);
    if(!temp_input_is_active)
    {
        const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
        const bool make_active = (clicked || g.NavActivateId == id);
        if(make_active && clicked)
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        if(make_active && temp_input_allowed)
            if((clicked && g.IO.KeyCtrl) ||
               (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input_is_active = true;
        if(make_active)
            memcpy(&g.ActiveIdValueOnActivation, p_data, DataTypeGetInfo(data_type)->Size);
        if(make_active && !temp_input_is_active)
        {
            SetActiveID(id, window);
            SetFocusID(id, window);
            FocusWindow(window);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }
    }

    if(temp_input_is_active)
    {
        const bool clamp_enabled = (flags & ImGuiSliderFlags_ClampOnInput) != 0;
        return TempInputScalar(frame_bb, id, label, data_type, p_data, format,
                               clamp_enabled ? p_min : NULL, clamp_enabled ? p_max : NULL);
    }

    // Slider behavior (computes grab_bb in frame_bb space)
    ImRect grab_bb;
    const bool value_changed =
        SliderBehavior(frame_bb, id, data_type, p_data, p_min, p_max, format, flags, &grab_bb);
    if(value_changed)
        MarkItemEdited(id);

    // -- Custom visuals: thin track + round knob --
    const float knob_radius = frame_h * 0.35f;
    const float track_h = ImMax(2.0f, frame_h * 0.14f);
    const float track_y = frame_bb.GetCenter().y;
    const float track_rounding = track_h * 0.5f;
    const float knob_x_min = frame_bb.Min.x + knob_radius;
    const float knob_x_max = frame_bb.Max.x - knob_radius;
    float t = 0.0f;
    if(grab_bb.Max.x > grab_bb.Min.x)
    {
        const float grab_padding = 2.0f;
        const float grab_sz = grab_bb.GetWidth();
        const float usable_min = frame_bb.Min.x + grab_padding + grab_sz * 0.5f;
        const float usable_max = frame_bb.Max.x - grab_padding - grab_sz * 0.5f;
        if(usable_max > usable_min)
            t = ImClamp((grab_bb.GetCenter().x - usable_min) / (usable_max - usable_min), 0.0f, 1.0f);
    }
    const float knob_x = ImLerp(knob_x_min, knob_x_max, t);
    const ImU32 track_bg_col = GetColorU32(ImGuiCol_FrameBg, 0.7f);
    const ImU32 track_fill_col = GetColorU32(g.ActiveId == id  ? ImGuiCol_SliderGrabActive
                                             : hovered         ? ImGuiCol_SliderGrabActive
                                                               : ImGuiCol_SliderGrab);
    const ImU32 knob_col = GetColorU32(g.ActiveId == id ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab);
    window->DrawList->AddRectFilled(ImVec2(frame_bb.Min.x, track_y - track_h * 0.5f),
                                    ImVec2(frame_bb.Max.x, track_y + track_h * 0.5f),
                                    track_bg_col, track_rounding);
    if(knob_x > frame_bb.Min.x + track_rounding)
    {
        window->DrawList->AddRectFilled(ImVec2(frame_bb.Min.x, track_y - track_h * 0.5f),
                                        ImVec2(knob_x, track_y + track_h * 0.5f),
                                        track_fill_col, track_rounding);
    }
    window->DrawList->AddCircleFilled(ImVec2(knob_x, track_y), knob_radius, knob_col);

    // Value text (centered over the track)
    char value_buf[64];
    const char* value_buf_end =
        value_buf + DataTypeFormatString(value_buf, IM_COUNTOF(value_buf), data_type, p_data, format);
    RenderTextClipped(frame_bb.Min, frame_bb.Max, value_buf, value_buf_end, NULL, ImVec2(0.5f, 0.5f));

    // Label
    if(label_size.x > 0.0f)
        RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y),
                   label);

    RenderNavCursor(frame_bb, id);

    return value_changed;
}

void OpenInShell(const char* url)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if(g.PlatformIO.Platform_OpenInShellFn != nullptr)
    {
        g.PlatformIO.Platform_OpenInShellFn(&g, url);
    }
}

bool CollapsingSection(const char* label, ImGuiTreeNodeFlags flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if(window->SkipItems)
        return false;

    ImGuiStyle& style = g.Style;
    const bool is_leaf = (flags & ImGuiTreeNodeFlags_Leaf) != 0;

    const ImGuiID id = window->GetID(label);
    ImGuiStorage* storage = GetStateStorage();
    const bool default_open = (flags & ImGuiTreeNodeFlags_DefaultOpen) != 0;
    bool is_open = is_leaf ? true : storage->GetBool(id, default_open);

    const char* label_end = FindRenderedTextEnd(label);
    const ImVec2 label_size = CalcTextSize(label, label_end, false);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 padding = style.SeparatorTextPadding;
    const float separator_thickness = style.SeparatorTextBorderSize;

    // Leaf nodes omit the arrow; collapsible nodes prefix arrow + inner spacing
    const float arrow_w = is_leaf ? 0.0f : g.FontSize + style.ItemInnerSpacing.x;
    const float combined_w = arrow_w + label_size.x;

    const ImVec2 min_size(combined_w + padding.x * 2.0f,
                          ImMax(label_size.y + padding.y * 2.0f, separator_thickness));
    const ImRect bb(pos, ImVec2(window->WorkRect.Max.x, pos.y + min_size.y));
    const float text_baseline_y =
        ImTrunc((bb.GetHeight() - label_size.y) * style.SeparatorTextAlign.y + 0.99999f);

    ItemSize(min_size, text_baseline_y);
    if(!ItemAdd(bb, id))
        return is_open;

    bool hovered = false;
    if(!is_leaf)
    {
        bool held;
        const bool clicked = ButtonBehavior(bb, id, &hovered, &held);
        if(clicked)
        {
            is_open = !is_open;
            storage->SetBool(id, is_open);
        }
        if(hovered)
            SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // Horizontal position of the block, aligned per SeparatorTextAlign.x
    const float label_avail_w = ImMax(0.0f, (bb.Max.x - pos.x) - padding.x * 2.0f);
    const float block_x =
        pos.x + padding.x +
        ImMax(0.0f, (label_avail_w - combined_w) * style.SeparatorTextAlign.x);

    const ImVec2 label_pos(block_x + arrow_w, pos.y + text_baseline_y);

    // Separator lines flanking the block
    const float seps_y = ImTrunc((bb.Min.y + bb.Max.y) * 0.5f + 0.99999f);
    const float sep1_x2 = block_x - style.ItemSpacing.x;
    const float sep2_x1 = block_x + combined_w + style.ItemSpacing.x;
    const ImU32 sep_col =
        GetColorU32(hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
    if(separator_thickness > 0.0f)
    {
        if(sep1_x2 > pos.x)
            window->DrawList->AddLine(ImVec2(pos.x, seps_y), ImVec2(sep1_x2, seps_y),
                                      sep_col, separator_thickness);
        if(bb.Max.x > sep2_x1)
            window->DrawList->AddLine(ImVec2(sep2_x1, seps_y), ImVec2(bb.Max.x, seps_y),
                                      sep_col, separator_thickness);
    }

    const ImU32 text_col = GetColorU32(ImGuiCol_Text);
    if(!is_leaf)
    {
        const ImVec2 arrow_pos(block_x,
                               pos.y + text_baseline_y + (label_size.y - g.FontSize) * 0.5f);
        RenderArrow(window->DrawList, arrow_pos, text_col,
                    is_open ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);
    }

    if(label_size.x > 0.0f)
        RenderTextEllipsis(window->DrawList, label_pos,
                           ImVec2(bb.Max.x, bb.Max.y + style.ItemSpacing.y),
                           bb.Max.x, label, label_end, &label_size);

    return is_open;
}

} // namespace ImGui
