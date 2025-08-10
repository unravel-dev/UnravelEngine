#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <imgui/imgui.h>
#include <string>
#include <vector>
#include "imspinner.h"

enum ImGuiMouseCursorEx_
{
    ImGuiMouseCursor_Help = ImGuiMouseCursor_COUNT,
    ImGuiMouseCursor_Cross,
    ImGuiMouseCursorEx_COUNT
};

using ImGuiKeyCombination = std::vector<ImGuiKey>;
namespace ImGui
{

IMGUI_API bool IsDragDropPossibleTargetForType(const char* type);

IMGUI_API bool DragMultiFormatScalarN(const char* label,
                                      ImGuiDataType data_type,
                                      void* p_data,
                                      int components,
                                      float v_speed = 1.0f,
                                      const void* p_min = NULL,
                                      const void* p_max = NULL,
                                      const char** format = NULL,
                                      ImGuiSliderFlags flags = 0);

IMGUI_API bool DragVecN(const char* label,
                        ImGuiDataType data_type,
                        void* p_data,
                        int components,
                        float v_speed = 1.0f,
                        const void* p_min = NULL,
                        const void* p_max = NULL,
                        const void* p_default_data = NULL,
                        const char* format = NULL,
                        ImGuiSliderFlags flags = 0);

IMGUI_API void AlignedItem(float align, float totalWidth, float itemWidth, const std::function<void()>& itemDrawFn);
IMGUI_API void AlignedItemV(float align, float totalHeight, float itemHeight, const std::function<void()>& itemDrawFn);

IMGUI_API ImVec2 CalcItemSize(const char* label, ImVec2 size_arg = ImVec2(0, 0));

IMGUI_API std::string GetKeyCombinationName(const ImGuiKeyCombination& keys);
IMGUI_API bool IsItemCombinationKeyPressed(const ImGuiKeyCombination& keys);

IMGUI_API bool IsCombinationKeyPressed(const ImGuiKeyCombination& keys);

IMGUI_API bool IsItemDoubleClicked(ImGuiMouseButton mouse_button = 0);
IMGUI_API bool IsItemReleased(ImGuiMouseButton mouse_button = 0);
IMGUI_API bool IsItemKeyPressed(ImGuiKey key,
                                bool repeat = true); // was key pressed (went from !Down to Down)? if repeat=true, uses
                                                     // io.KeyRepeatDelay / KeyRepeatRate
IMGUI_API bool IsItemKeyReleased(ImGuiKey key);

// Returns true when the item's focus state has changed in the current frame
// Useful for detecting navigation via keyboard/gamepad
IMGUI_API bool IsItemFocusChanged();

IMGUI_API void RenderFocusFrame(ImVec2 p_min, ImVec2 p_max, ImU32 color = GetColorU32(ImGuiCol_NavCursor), float thickness = 2.0f);
IMGUI_API void RenderFrameEx(ImVec2 p_min, ImVec2 p_max, float rounding = -1.0f, float thickness = 1.0f);

IMGUI_API void SetItemFocusFrame(ImU32 color = GetColorU32(ImGuiCol_NavCursor));
IMGUI_API void SameLineInner();
IMGUI_API void Spinner(float diameter,
                       float thickness,
                       int num_segments,
                       float speed,
                       ImU32 color = GetColorU32(ImGuiCol_NavCursor));

IMGUI_API void ImageWithAspect(ImTextureID texture,
                               ImVec2 texture_size,
                               ImVec2 size,
                               ImVec2 align = ImVec2(0.5f, 0.0f),
                               const ImVec2& uv0 = ImVec2(0, 0),
                               const ImVec2& uv1 = ImVec2(1, 1));

IMGUI_API void SetNextWindowViewportToCurrent();

struct IMGUI_API ContentItem
{
    ImTextureID texId{};
    ImVec2 texture_size{};
    ImVec2 image_size{};
    const char* name{""};
    ImFont* name_font{};
    const char* type{""};
    ImFont* type_font{};

    ImVec2 uv0 = ImVec2(0, 0);
    ImVec2 uv1 = ImVec2(1, 1);
    int frame_padding = -1;
    ImVec4 bg_col = ImVec4(0, 0, 0, 0);
    ImVec4 tint_col = ImVec4(1, 1, 1, 1);
};

IMGUI_API bool ContentButtonItem(const ContentItem& item);

IMGUI_API bool ImageMenuItem(ImTextureID texture, const char* tooltip, bool selected = false, bool enabled = true);


template<size_t BufferSize = 64>
auto CreateInputTextBuffer(const std::string& source) -> std::array<char, BufferSize>
{
    std::array<char, BufferSize> input_buff{};

    auto name_sz = std::min(source.size(), input_buff.size() - 1);
    std::memcpy(input_buff.data(), source.c_str(), name_sz);
    return input_buff;
}


template<size_t BufferSize = 64>
bool InputTextWidget(const std::string& label,
                     std::array<char, BufferSize>& buffer,
                     bool multiline = false,
                     ImGuiInputTextFlags flags = 0)
{

    if(multiline)
    {
        if(ImGui::InputTextMultiline(label.c_str(), buffer.data(), buffer.size(), ImVec2(0, 0), flags))
        {
            return true;
        }
    }
    else
    {
        if(ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), flags))
        {
            return true;
        }
    }

    return false;
}

template<size_t BuffSize = 64>
bool InputTextWidget(const std::string& label,
                     std::string& source,
                     bool multiline = false,
                     ImGuiInputTextFlags flags = 0)
{

    auto buffer = CreateInputTextBuffer<BuffSize>(source);

    if(InputTextWidget(label, buffer, multiline, flags))
    {
        source = buffer.data();
        return true;
    }
    return false;
}

template<typename T>
constexpr inline auto GetDataType() -> ImGuiDataType
{
    if(std::is_same<T, float>::value)
        return ImGuiDataType_Float;
    if(std::is_same<T, double>::value)
        return ImGuiDataType_Double;
    if(std::is_same<T, int8_t>::value)
        return ImGuiDataType_S8;
    if(std::is_same<T, int16_t>::value)
        return ImGuiDataType_S16;
    if(std::is_same<T, int32_t>::value)
        return ImGuiDataType_S32;
    if(std::is_same<T, int64_t>::value)
        return ImGuiDataType_S64;
    if(std::is_same<T, uint8_t>::value)
        return ImGuiDataType_U8;
    if(std::is_same<T, uint16_t>::value)
        return ImGuiDataType_U16;
    if(std::is_same<T, uint32_t>::value)
        return ImGuiDataType_U32;
    if(std::is_same<T, uint64_t>::value)
        return ImGuiDataType_U64;
    return ImGuiDataType_COUNT;
}

IMGUI_API const char* GetDataPrintFormat(ImGuiDataType data_type);

template<typename T>
constexpr inline auto GetDataPrintFormat() -> const char*
{
    return GetDataPrintFormat(GetDataType<T>());
}

template<typename T>
IMGUI_API bool DragScalarT(const char* label,
                           T* p_data,
                           float v_speed = 1.0f,
                           T p_min = {},
                           T p_max = {},
                           const char* format = NULL,
                           ImGuiSliderFlags flags = 0)
{
    return DragScalar(label, GetDataType<T>(), p_data, v_speed, &p_min, &p_max, format, flags);
}

template<typename T>
IMGUI_API bool SliderScalarT(const char* label,
                             T* p_data,
                             T p_min,
                             T p_max,
                             const char* format = NULL,
                             ImGuiSliderFlags flags = 0)
{
    return SliderScalar(label, GetDataType<T>(), p_data, &p_min, &p_max, format, flags);
}

IMGUI_API void ItemBrowser(float item_width, size_t items_count, const std::function<void(int index)>& callback);

struct IMGUI_API WindowTimeBlock
{
    WindowTimeBlock(ImFont* font = nullptr);
    ~WindowTimeBlock();

private:
    using clock_t = std::chrono::high_resolution_clock;
    clock_t::time_point start_{};
    ImFont* font_{};
};

typedef int OutlineFlags;
enum OutlineFlags_
{
    OutlineFlags_None            =      0,   // draw no activity outline
    OutlineFlags_WhenHovered     = 1 << 1,   // draw an outline when item is hovered
    OutlineFlags_WhenActive      = 1 << 2,   // draw an outline when item is active
    OutlineFlags_WhenInactive    = 1 << 3,   // draw an outline when item is inactive
    OutlineFlags_HighlightActive = 1 << 4,   // when active, the outline is in highlight colour
    OutlineFlags_NoHighlightActive = OutlineFlags_WhenHovered | OutlineFlags_WhenActive | OutlineFlags_WhenInactive,
    OutlineFlags_NoOutlineInactive = OutlineFlags_WhenHovered | OutlineFlags_WhenActive | OutlineFlags_HighlightActive,
    OutlineFlags_All = OutlineFlags_WhenHovered | OutlineFlags_WhenActive | OutlineFlags_WhenInactive | OutlineFlags_HighlightActive,
};

IMGUI_API void DrawItemActivityOutline(OutlineFlags flags = OutlineFlags_NoOutlineInactive,
                             ImColor colourHighlight = IM_COL32(236, 158, 36, 255),
                             float rounding = -1.0f);

IMGUI_API void DrawFilterWithHint(ImGuiTextFilter& filter, const char* hint_text, float width);

// When multi-viewports are disabled: wrap in main viewport.
// When multi-viewports are enabled: wrap in monitor.
// FIXME: Experimental: not sure how this behaves with multi-monitor and monitor coordinates gaps.
IMGUI_API void WrapMousePos(int axises_mask);
IMGUI_API void WrapMousePos(int axises_mask, const ImVec2& wrap_rect_min, const ImVec2& wrap_rect_max);
IMGUI_API void WrapMousePos();
IMGUI_API void ActiveItemWrapMousePos();
IMGUI_API void ActiveItemWrapMousePos(const ImVec2& wrap_rect_min, const ImVec2& wrap_rect_max);


IMGUI_API bool BeginPopupContextWindowEx(const char* str_id = nullptr, ImGuiPopupFlags popup_flags = 1);
} // namespace ImGui
