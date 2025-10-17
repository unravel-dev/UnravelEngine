#include "imgradient.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
struct ImGradientHDRTemporaryState
{
    uint32_t activeId{};

    int selectedIndex = -1;

    int draggingIndex = -1;
};

template<typename T>
int AddMarker(std::array<T, MarkerMax>& a, int32_t& count, T value)
{
    int result = -1;
    const auto lb = std::lower_bound(a.begin(),
                                     a.begin() + count,
                                     value,
                                     [&](const T& a, const T& b) -> bool
                                     {
                                         return a.Position < b.Position;
                                     });

    if(lb != a.end())
    {
        const auto ind = lb - a.begin();
        std::copy(a.begin() + ind, a.begin() + count, a.begin() + ind + 1);
        *(a.begin() + ind) = value;
        result = ind;
        count++;
    }

    return result;
}

enum class DrawMarkerMode
{
    Selected,
    Unselected,
    None,
};

void DrawMarker(const ImVec2& pmin, const ImVec2& pmax, const ImU32& color, DrawMarkerMode mode)
{
    auto drawList = ImGui::GetWindowDrawList();
    const auto w = static_cast<int32_t>(pmax.x - pmin.x);
    const auto h = static_cast<int32_t>(pmax.y - pmin.y);
    const auto sign = std::signbit(static_cast<float>(h)) ? -1 : 1;

    const auto margin = 2;
    const auto marginh = margin * sign;

    if(mode != DrawMarkerMode::None)
    {
        const auto outlineColor = mode == DrawMarkerMode::Selected
                                      ? ImGui::ColorConvertFloat4ToU32({0.0f, 0.0f, 1.0f, 1.0f})
                                      : ImGui::ColorConvertFloat4ToU32({0.2f, 0.2f, 0.2f, 1.0f});

        drawList->AddTriangleFilled({pmin.x + w / 2, pmin.y},
                                    {pmin.x + 0, pmin.y + h / 2},
                                    {pmin.x + w, pmin.y + h / 2},
                                    outlineColor);

        drawList->AddRectFilled({pmin.x + 0, pmin.y + h / 2}, {pmin.x + w, pmin.y + h}, outlineColor);
    }

    drawList->AddTriangleFilled({pmin.x + w / 2, pmin.y + marginh},
                                {pmin.x + 0 + margin, pmin.y + h / 2},
                                {pmin.x + w - margin, pmin.y + h / 2},
                                color);

    drawList->AddRectFilled({pmin.x + 0 + margin, pmin.y + h / 2}, {pmin.x + w - margin, pmin.y + h - marginh}, color);
}

template<typename T>
void SortMarkers(std::array<T, MarkerMax>& a, int32_t& count, int32_t& selectedIndex, int32_t& draggingIndex)
{
    struct SortedMarker
    {
        int index;
        T marker;
    };

    std::vector<SortedMarker> sortedMarker;

    for(int32_t i = 0; i < count; i++)
    {
        sortedMarker.emplace_back(SortedMarker{i, a[i]});
    }

    std::sort(sortedMarker.begin(),
              sortedMarker.end(),
              [](const SortedMarker& a, const SortedMarker& b)
              {
                  return a.marker.Position < b.marker.Position;
              });

    for(int32_t i = 0; i < count; i++)
    {
        a[i] = sortedMarker[i].marker;
    }

    if(selectedIndex != -1)
    {
        for(int32_t i = 0; i < count; i++)
        {
            if(sortedMarker[i].index == selectedIndex)
            {
                selectedIndex = i;
                break;
            }
        }
    }

    if(draggingIndex != -1)
    {
        for(int32_t i = 0; i < count; i++)
        {
            if(sortedMarker[i].index == draggingIndex)
            {
                draggingIndex = i;
                break;
            }
        }
    }
}

ImU32 GetMarkerColor(const ImGradientHDRState::Marker& marker)
{
    const auto c = marker.Value.Color;
    return ImGui::ColorConvertFloat4ToU32({c[0], c[1], c[2], c[3]});
}

enum class MarkerDirection
{
    ToUpper,
    ToLower,
};

struct UpdateMarkerResult
{
    bool isChanged;
    bool isHovered;
};

template<typename T>
UpdateMarkerResult UpdateMarker(std::array<T, MarkerMax>& markerArray,
                                int& markerCount,
                                ImGradientHDRTemporaryState& temporaryState,
                                const char* keyStr,
                                ImVec2 originPos,
                                float width,
                                float markerWidth,
                                float markerHeight,
                                MarkerDirection markerDir)
{
    UpdateMarkerResult ret;
    ret.isChanged = false;
    ret.isHovered = false;

    for(int i = 0; i < markerCount; i++)
    {
        const auto x = (int)(markerArray[i].Position * width);
        ImGui::SetCursorScreenPos({originPos.x + x - 5, originPos.y});

        DrawMarkerMode mode;
        if(temporaryState.selectedIndex == i)
        {
            mode = DrawMarkerMode::Selected;
        }
        else
        {
            mode = DrawMarkerMode::Unselected;
        }

        if(markerDir == MarkerDirection::ToLower)
        {
            DrawMarker({originPos.x + x - 5, originPos.y + markerHeight},
                       {originPos.x + x + 5, originPos.y + 0},
                       GetMarkerColor(markerArray[i]),
                       mode);
        }
        else
        {
            DrawMarker({originPos.x + x - 5, originPos.y + 0},
                       {originPos.x + x + 5, originPos.y + markerHeight},
                       GetMarkerColor(markerArray[i]),
                       mode);
        }

        ImGui::InvisibleButton((keyStr + std::to_string(i)).c_str(), {markerWidth, markerHeight});

        ret.isHovered |= ImGui::IsItemHovered();

        if(temporaryState.draggingIndex == -1 && ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            temporaryState.selectedIndex = i;
            temporaryState.draggingIndex = i;
        }

        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            temporaryState.draggingIndex = -1;
        }

        if(temporaryState.draggingIndex == i && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f))
        {
            const auto diff = ImGui::GetIO().MouseDelta.x / width;
            markerArray[i].Position += diff;
            markerArray[i].Position = std::max(std::min(markerArray[i].Position, 1.0f), 0.0f);

            ret.isChanged |= diff != 0.0f;
        }
    }

    return ret;
}

} // namespace

ImGradientHDRState::Marker* ImGradientHDRState::GetMarker(int32_t index)
{
    if(index < 0 || index >= MarkerCount)
    {
        return nullptr;
    }

    return &(Markers[index]);
}

auto ImGradientHDRState::AddMarker(float x, Element value) -> int
{
    if(MarkerCount >= MarkerMax)
    {
        return false;
    }

    x = std::max(std::min(x, 1.0f), 0.0f);

    const auto marker = Marker{x, value};
    return ::AddMarker(Markers, MarkerCount, marker);
}

bool ImGradientHDRState::RemoveMarker(int32_t index)
{
    if(index >= MarkerCount || index < 0)
    {
        return false;
    }

    std::copy(Markers.begin() + index + 1, Markers.end(), Markers.begin() + index);
    MarkerCount--;
    return true;
}

auto ImGradientHDRState::GetElement(float x) const -> Element
{
    if(MarkerCount == 0)
    {
        return Element{{1.0f, 1.0f, 1.0f, 1.0f}};
    }

    if(x < Markers[0].Position)
    {
        return Markers[0].Value;
    }

    if(Markers[MarkerCount - 1].Position <= x)
    {
        return Markers[MarkerCount - 1].Value;
    }

    auto key = Marker();
    key.Position = x;

    auto it = std::lower_bound(Markers.begin(),
                               Markers.begin() + MarkerCount,
                               key,
                               [](const Marker& a, const Marker& b)
                               {
                                   return a.Position < b.Position;
                               });
    auto ind = static_cast<int32_t>(std::distance(Markers.begin(), it));

    {
        if(Markers[ind].Position != x)
        {
            ind--;
        }

        if(Markers[ind].Position <= x && x <= Markers[ind + 1].Position)
        {
            const auto area = Markers[ind + 1].Position - Markers[ind].Position;
            if(area == 0)
            {
                return Markers[ind].Value;
            }

            const auto alpha = (x - Markers[ind].Position) / area;
            const auto r = Markers[ind + 1].Value.Color[0] * alpha + Markers[ind].Value.Color[0] * (1.0f - alpha);
            const auto g = Markers[ind + 1].Value.Color[1] * alpha + Markers[ind].Value.Color[1] * (1.0f - alpha);
            const auto b = Markers[ind + 1].Value.Color[2] * alpha + Markers[ind].Value.Color[2] * (1.0f - alpha);
            const auto a = Markers[ind + 1].Value.Color[3] * alpha + Markers[ind].Value.Color[3] * (1.0f - alpha);

            return Element{{r, g, b, a}};
        }
        else
        {
            assert(0);
        }
    }

    return Element{{1.0f, 1.0f, 1.0f, 1.0f}};
}

bool ImGradient(const char* title,
                ImGradientHDRState& state,
                const char* xtitle)
{
    bool changed = false;

    static ImGradientHDRTemporaryState temporaryState{};
    auto widgetId = ImGui::GetID(title);
    ImGui::PushID(widgetId);
    ImGradientHDRTemporaryState tempState{};
    tempState.activeId = widgetId;

    if(widgetId == temporaryState.activeId)
    {
        tempState = temporaryState;
    }

    ImGui::BeginGroup();
    ImGui::TextUnformatted(title);

    auto drawList = ImGui::GetWindowDrawList();

    const float width = int(ImGui::CalcItemWidth());
    const auto barHeight = ImGui::GetFrameHeight();
    const auto markerWidth = 10;
    const auto markerHeight = 15;

    const auto barOriginPos = ImGui::GetCursorScreenPos();

    ImGui::Dummy({width, barHeight});

    const float gridStep = barHeight / 2.0f;

    ImGui::RenderColorRectWithAlphaCheckerboard(drawList,
                                                barOriginPos,
                                                barOriginPos + ImVec2(width, barHeight),
                                                IM_COL32(50, 50, 50, 128), gridStep, ImVec2(0, 0));

    {
        std::vector<float> xkeys;
        xkeys.reserve(16);

        for(int32_t i = 0; i < state.MarkerCount; i++)
        {
            xkeys.emplace_back(state.Markers[i].Position);
        }

        xkeys.emplace_back(0.0f);
        xkeys.emplace_back(1.0f);

        auto result = std::unique(xkeys.begin(), xkeys.end());
        xkeys.erase(result, xkeys.end());

        std::sort(xkeys.begin(), xkeys.end());

        if(state.Linear)
        {
            for(size_t i = 0; i < xkeys.size() - 1; i++)
            {
                const auto c1 = state.GetElement(xkeys[i]).Color;
                const auto c2 = state.GetElement(xkeys[i + 1]).Color;

                const auto colorAU32 = ImGui::ColorConvertFloat4ToU32({c1[0], c1[1], c1[2], c1[3]});
                const auto colorBU32 = ImGui::ColorConvertFloat4ToU32({c2[0], c2[1], c2[2], c2[3]});

                drawList->AddRectFilledMultiColor(
                    ImVec2(barOriginPos.x + xkeys[i] * width, barOriginPos.y),
                    ImVec2(barOriginPos.x + xkeys[i + 1] * width, barOriginPos.y + barHeight),
                    colorAU32,
                    colorBU32,
                    colorBU32,
                    colorAU32);
            }
        }
        else
        {
            for(size_t i = 0; i < xkeys.size() - 1; i++)
            {
                const auto c1 = state.GetElement(xkeys[i]).Color;
                const auto colorAU32 = ImGui::ColorConvertFloat4ToU32({c1[0], c1[1], c1[2], c1[3]});

                static constexpr auto rounding{1.f};
                drawList->AddRectFilled(ImVec2(barOriginPos.x + xkeys[i] * width, barOriginPos.y),
                                        ImVec2(barOriginPos.x + xkeys[i + 1] * width, barOriginPos.y + barHeight),
                                        colorAU32,
                                        rounding,
                                        ImDrawFlags_Closed);
            }
        }
    }

    {
        auto originPosBelowBar = ImGui::GetCursorScreenPos();

        const auto resultColor = UpdateMarker(state.Markers,
                                              state.MarkerCount,
                                              tempState,
                                              "c",
                                              originPosBelowBar,
                                              width,
                                              markerWidth,
                                              markerHeight,
                                              MarkerDirection::ToUpper);

        changed |= resultColor.isChanged;

        if(tempState.draggingIndex != -1)
        {
            SortMarkers(state.Markers, state.MarkerCount, tempState.selectedIndex, tempState.draggingIndex);
        }

        ImGui::SetCursorScreenPos(barOriginPos);

        ImGui::InvisibleButton("MarkerArea", {width, static_cast<float>(markerHeight * 1.5f + barHeight)});

        if(ImGui::IsItemHovered())
        {
            const float x = (ImGui::GetIO().MousePos.x - (barOriginPos.x));
            const float xn = x / width;
            const auto element = state.GetElement(xn);

            if(!resultColor.isHovered && state.MarkerCount >= 0 && size_t(state.MarkerCount) < state.Markers.size())
            {
                auto c = element.Color;
                DrawMarker({originPosBelowBar.x + x - 5, originPosBelowBar.y + 0},
                           {originPosBelowBar.x + x + 5, originPosBelowBar.y + markerHeight},
                           ImGui::ColorConvertFloat4ToU32({c[0], c[1], c[2], 0.5f}),
                           DrawMarkerMode::None);
            }

            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                auto index = state.AddMarker(xn, element);
                changed |= index >= 0;
                tempState.selectedIndex = index;
            }
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if(ImGui::Button("Clear"))
        {
            state.MarkerCount = 0;
            changed = true;
        }
    }

    int indexToRemove = -1;
    for(int i = 0; i < state.MarkerCount; ++i)
    {
        ImGui::PushID(i);
        ImGui::BeginGroup();
        if(ImGui::Button(xtitle))
        {
            indexToRemove = i;

        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);


        auto& marker = state.Markers[i];
        changed |= ImGui::ColorEdit4("Color", marker.Value.Color.data());

        ImGui::EndGroup();
        if(i == tempState.selectedIndex)
        {
            ImGui::SetItemFocusFrame();
        }

        ImGui::PopID();
    }

    if(indexToRemove != -1)
    {
        state.RemoveMarker(indexToRemove);
        tempState = ImGradientHDRTemporaryState{};
        changed = true;
    }

    if(tempState.selectedIndex != -1)
    {
        temporaryState = tempState;
    }

    ImGui::EndGroup();
    ImGui::PopID();

    return changed;
}
