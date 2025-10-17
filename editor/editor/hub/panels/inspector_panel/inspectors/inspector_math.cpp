#include "inspector_math.h"
#include "entt/meta/meta.hpp"
#include "imgui_widgets/tooltips.h"
#include "inspector_basetypes.h"
#include "inspectors.h"
#include "entt/meta/resolve.hpp"
#include "imgui/imgui.h"
#include "math/gradient.h"
#include <imgui/imgui_internal.h>
#include <imgui_widgets/imgradient.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>


namespace unravel
{
namespace
{
    
    
    namespace utils
    {
    
    template<typename T>
    auto get_gradient_element_color(const typename math::gradient<T>::point_t& element,
                                             float alphaMult = 1.0f) -> ImU32
    {
        return ImGui::ColorConvertFloat4ToU32({1.0f, 1.0f, 1.0f, alphaMult});
    }
    
    template<>
    inline auto get_gradient_element_color<math::color>(const typename math::gradient<math::color>::point_t& element,
                                                                float alphaMult) -> ImU32
    {
        return ImGui::ColorConvertFloat4ToU32({element.element.value.x,
                                               element.element.value.y,
                                               element.element.value.z,
                                               (element.element.value.w) * alphaMult});
    }

    template<typename T>
    void draw_gradient_background(ImDrawList* drawList,
                                  const math::gradient<T>& gradient,
                                  ImVec2 barOriginPos,
                                  ImVec2 size)
    {
        
        ImGui::Dummy(size);

        ImGui::RenderFrame(barOriginPos, barOriginPos + size, ImGui::GetColorU32(ImGuiCol_FrameBg), true, ImGui::GetStyle().FrameRounding);
    
    }

    template<>
    void draw_gradient_background<math::color>(ImDrawList* drawList,
                                  const math::gradient<math::color>& gradient,
                                  ImVec2 barOriginPos,
                                  ImVec2 size)
    {
        
        ImGui::Dummy(size);
    
        const float gridStep = size.y / 2.0f;
    
        ImGui::RenderColorRectWithAlphaCheckerboard(drawList,
                                                    barOriginPos,
                                                    barOriginPos + size,
                                                    IM_COL32(50, 50, 50, 128), gridStep, ImVec2(0, 0));
    
    }
    
    
    template<typename T>
    void draw_gradient_combined_element(ImDrawList* drawList,
                                                 const math::gradient<T>& gradient,
                                                 const T& default_value,
                                                 const std::vector<float>& xkeys,
                                                 const std::vector<size_t>& ind,
                                                 ImVec2 barOriginPos,
                                                 ImVec2 size)
    {
    
    }
    
    template<>
    inline void draw_gradient_combined_element<math::color>(ImDrawList* drawList,
                                                 const math::gradient<math::color>& gradient,
                                                 const math::color& deault_value,
                                                 const std::vector<float>& xkeys,
                                                 const std::vector<size_t>& ind,
                                                 ImVec2 barOriginPos, ImVec2 size)
    {
        if(ind.size() == 1)
        {
            auto i = ind.front();
            auto c1 = deault_value;
    
            if(gradient.is_valid())
            {
                c1 = gradient.sample(xkeys[i]);
            }
            const uint32_t colorAU32 = c1;
    
            static constexpr auto rounding{1.f};
            drawList->AddRectFilled(ImVec2(barOriginPos.x + xkeys[i] * size.x, barOriginPos.y),
                                    ImVec2(barOriginPos.x + xkeys[i + 1] * size.x, barOriginPos.y + size.y),
                                    colorAU32,
                                    rounding);
        }
        else if(ind.size() == 2)
        {
    
            auto c1 = deault_value;
            auto c2 = deault_value;
    
            if(gradient.is_valid())
            {
                c1 = gradient.sample(xkeys[ind[0]]);
                c2 = gradient.sample(xkeys[ind[1]]);
            }
    
            const uint32_t colorAU32 = c1;
            const uint32_t colorBU32 = c2;
    
            drawList->AddRectFilledMultiColor(
                ImVec2(barOriginPos.x + xkeys[ind[0]] * size.x, barOriginPos.y),
                ImVec2(barOriginPos.x + xkeys[ind[1]] * size.x, barOriginPos.y + size.y),
                colorAU32,
                colorBU32,
                colorBU32,
                colorAU32);
    
        }
    }
    
    template<typename T>
    bool draw_gradient_impl(const std::string& title, math::gradient<T>& gradient,
                                const std::function<bool(T&)>& edit_element,
                                const T& default_value)
    {
        struct TemporaryState
        {
            ImGuiID activeId{};
            int selectedIndex = -1;
            int draggingIndex = -1;
        };
    
        enum class DrawMarkerMode
        {
            Selected,
            Unselected,
            None,
        };
    
        auto DrawMarker = [](const ImVec2& pmin, const ImVec2& pmax, const ImU32& color, DrawMarkerMode mode)
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
        };
    
    
        auto SortMarkers = [&](typename math::gradient<T>::points_t& a,
                               int32_t& selectedIndex, int32_t& draggingIndex)
        {
            struct SortedMarker
            {
                size_t index;
                typename math::gradient<T>::point_t marker;
            };
    
            std::vector<SortedMarker> sortedMarker;
    
            for(size_t i = 0; i < a.size(); i++)
            {
                sortedMarker.emplace_back(SortedMarker{i, a[i]});
            }
    
            std::sort(sortedMarker.begin(),
                      sortedMarker.end(),
                      [](const SortedMarker& a, const SortedMarker& b)
                      {
                          return a.marker < b.marker;
                      });
    
            for(size_t i = 0; i < a.size(); i++)
            {
                a[i] = sortedMarker[i].marker;
            }
    
            if(selectedIndex != -1)
            {
                for(size_t i = 0; i < a.size(); i++)
                {
                    if(selectedIndex >= 0 && sortedMarker[i].index == size_t(selectedIndex))
                    {
                        selectedIndex = i;
                        break;
                    }
                }
            }
    
            if(draggingIndex != -1)
            {
                for(size_t i = 0; i < a.size(); i++)
                {
                    if(draggingIndex >= 0 && sortedMarker[i].index == size_t(draggingIndex))
                    {
                        draggingIndex = i;
                        break;
                    }
                }
            }
        };
    
    
    
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
    
        auto UpdateMarker = [&](TemporaryState& temporaryState,
                                typename math::gradient<T>::points_t& points,
                                const char* keyStr,
                                ImVec2 originPos,
                                float width,
                                float markerWidth,
                                float markerHeight,
                                MarkerDirection markerDir) -> UpdateMarkerResult
        {
            UpdateMarkerResult ret;
            ret.isChanged = false;
            ret.isHovered = false;
    
            float markerOffset = markerWidth * 0.5f;
            for(size_t i = 0; i < points.size(); i++)
            {
                const auto x = (int)(points[i].progress * width);
    
                DrawMarkerMode mode;
                if(temporaryState.selectedIndex >= 0 && size_t(temporaryState.selectedIndex) == i)
                {
                    mode = DrawMarkerMode::Selected;
                }
                else
                {
                    mode = DrawMarkerMode::Unselected;
                }
    
                if(markerDir == MarkerDirection::ToLower)
                {
                    DrawMarker({originPos.x + x - markerOffset, originPos.y + markerHeight},
                               {originPos.x + x + markerOffset, originPos.y + 0},
                               get_gradient_element_color<T>(points[i]),
                               mode);
                }
                else
                {
                    DrawMarker({originPos.x + x - markerOffset, originPos.y + 0},
                               {originPos.x + x + markerOffset, originPos.y + markerHeight},
                               get_gradient_element_color<T>(points[i]),
                               mode);
                }
    
                float pick_padding = 4.0f;
                ImGui::SetCursorScreenPos({originPos.x + x - (markerOffset + pick_padding), originPos.y});

                ImGui::InvisibleButton((keyStr + std::to_string(i)).c_str(), {markerWidth + pick_padding * 2.0f, markerHeight + pick_padding * 2.0f});
    
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
    
                if(temporaryState.draggingIndex >= 0 && size_t(temporaryState.draggingIndex) == i && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f))
                {
                    const auto diff = ImGui::GetIO().MouseDelta.x / width;
                    points[i].progress += diff;
                    points[i].progress = std::max(std::min(points[i].progress, 1.0f), 0.0f);
    
                    ret.isChanged |= diff != 0.0f;
                }
            }
    
            return ret;
        };
    
    
        bool changed = false;
    
        static TemporaryState temporaryState{};
        auto widgetId = ImGui::GetID(title.c_str());
        ImGui::PushID(widgetId);
        TemporaryState tempState{};
        tempState.activeId = widgetId;
    
        if(widgetId == temporaryState.activeId)
        {
            tempState = temporaryState;
        }
    
        auto drawList = ImGui::GetWindowDrawList();
    
        const float width = int(ImGui::CalcItemWidth());
        const auto barHeight = ImGui::GetFrameHeight();
        const auto markerWidth = 16;
        const auto markerHeight = 16;
    
        const auto barOriginPos = ImGui::GetCursorScreenPos();
        draw_gradient_background(drawList, gradient, barOriginPos, {width, barHeight});
    
        {
            std::vector<float> xkeys;
            xkeys.reserve(16);
    
            const auto& points = gradient.get_points();
            for(size_t i = 0; i < points.size(); i++)
            {
                xkeys.emplace_back(points[i].progress);
            }
    
            xkeys.emplace_back(0.0f);
            xkeys.emplace_back(1.0f);
    
            auto result = std::unique(xkeys.begin(), xkeys.end());
            xkeys.erase(result, xkeys.end());
    
            std::sort(xkeys.begin(), xkeys.end());
    
            if(gradient.get_interpolation_mode() == math::gradient_interpolation_mode_t::linear)
            {
                for(size_t i = 0; i < xkeys.size() - 1; i++)
                {
                    std::vector<size_t> ind = {i, i+1};
                    draw_gradient_combined_element<T>(drawList,
                                                               gradient,
                                                               default_value,
                                                               xkeys,
                                                               ind,
                                                               barOriginPos,
                                                               ImVec2(width, barHeight));
    
                }
            }
            else
            {
                for(size_t i = 0; i < xkeys.size() - 1; i++)
                {
                    std::vector<size_t> ind = {i};
    
                    draw_gradient_combined_element<T>(drawList,
                                                               gradient,
                                                               default_value,
                                                               xkeys,
                                                               ind,
                                                               barOriginPos,
                                                               ImVec2(width, barHeight));
    
                }
            }
        }
    
        {
            auto originPosBelowBar = ImGui::GetCursorScreenPos();
    
            auto points = gradient.get_points();
            const auto resultColor = UpdateMarker(tempState,
                                                  points,
                                                  "c",
                                                  originPosBelowBar,
                                                  width,
                                                  markerWidth,
                                                  markerHeight,
                                                  MarkerDirection::ToUpper);
    
            changed |= resultColor.isChanged;
    
            if(tempState.draggingIndex != -1)
            {
                SortMarkers(points, tempState.selectedIndex, tempState.draggingIndex);
            }
    
    
            if(resultColor.isChanged)
            {
                gradient.set_points(points);
            }
    
    
            ImGui::SetCursorScreenPos(barOriginPos);
    
            ImGui::InvisibleButton("MarkerArea", {width, static_cast<float>(markerHeight * 1.5f + barHeight)});
    
            if(ImGui::IsItemHovered())
            {
                const float x = (ImGui::GetIO().MousePos.x - (barOriginPos.x));
                const float xn = x / width;
    
                auto element = typename math::gradient<T>::point_t{xn, default_value};
    
                if(gradient.is_valid())
                {
                    element.element = gradient.sample(xn);
                }
    
                if(!resultColor.isHovered)
                {
                    auto c = get_gradient_element_color<T>(element, 0.5f);
                    DrawMarker({originPosBelowBar.x + x - markerWidth * 0.5f, originPosBelowBar.y + 0},
                               {originPosBelowBar.x + x + markerWidth * 0.5f, originPosBelowBar.y + markerHeight},
                               c,
                               DrawMarkerMode::None);
                }
    
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    auto index = gradient.add_point(element.element, xn);
                    changed |= index >= 0;
                    tempState.selectedIndex = index;
                }
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            if(ImGui::Button(ICON_MDI_WINDOW_CLOSE))
            {
                gradient.set_points({});
                changed = true;
            }
            ImGui::SetItemTooltipEx("%s", "Clear the gradient's elements");
        }
    
        int indexToRemove = -1;
        auto points = gradient.get_points();
        for(size_t i = 0; i < points.size(); ++i)
        {
            ImGui::PushID(i);
            ImGui::BeginGroup();
            if(ImGui::Button(ICON_MDI_WINDOW_CLOSE))
            {
                indexToRemove = i;
    
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    
            changed |= edit_element(points[i].element);
    
            ImGui::EndGroup();
            if(tempState.selectedIndex >= 0 && i == size_t(tempState.selectedIndex))
            {
                ImGui::SetItemFocusFrame();
            }
    
            ImGui::PopID();
        }
    
        if(changed)
        {
            gradient.set_points(points);
        }
    
    
        if(indexToRemove != -1)
        {
            gradient.remove_point(indexToRemove);
            tempState = {};
            changed = true;
        }
    
        if(tempState.selectedIndex != -1)
        {
            temporaryState = tempState;
        }
    
        ImGui::PopID();
    
        return changed;
    }
    
    inline void draw_title(const std::string& title)
    {
        if(title.empty())
        {
            return;
        }
        ImGui::TextUnformatted(title.c_str());
    }
    
    template<typename T>
    auto draw_interpolation_mode(math::gradient<T>& gradient) -> bool
    {
        ImGui::PushID("InterpolationMode");
        size_t interpolation_index = size_t(gradient.get_interpolation_mode());
        std::vector<std::string> interpolation_modes = {"Linear", "Constant"};
    
        bool changed = false;

        if(ImGui::BeginCombo("##", interpolation_modes[interpolation_index].c_str()))
        {
            for(size_t i = 0; i < interpolation_modes.size(); i++)
            {
                if(ImGui::Selectable(interpolation_modes[i].c_str()))
                {
                    interpolation_index = i;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        
    
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted("Interpolation");
    
        if(changed)
        {
            gradient.set_interpolation_mode(math::gradient_interpolation_mode_t(interpolation_index));
        }
        ImGui::PopID();
        return changed;
    }
    
    template<typename T>
    bool draw_gradient(const std::string& title, math::gradient<T>& gradient,
                                const std::function<bool(T&)>& edit_element,
                                const T& default_value)
    {
        bool changed = false;
    
        ImGui::PushID(title.c_str());
        ImGui::BeginGroup();
        changed |= draw_interpolation_mode(gradient);
        changed |= draw_gradient_impl<T>(title, gradient, edit_element, default_value);
        ImGui::EndGroup();
        ImGui::PopID();
    
        return changed;
    }

} // namespace utils


float DRAG_SPEED = 0.01f;

auto quat_to_vec4(math::quat q) -> math::vec4
{
    math::vec4 v;
    v.x = q.x;
    v.y = q.y;
    v.z = q.z;
    v.w = q.w;
    return v;
}
auto vec4_to_quat(math::vec4 v) -> math::quat
{
    math::quat q;
    q.x = v.x;
    q.y = v.y;
    q.z = v.z;
    q.w = v.w;
    return q;
}

bool DragFloat2(math::vec2& data, const var_info& info, std::array<const char*, 2> formats = {{"X:%.2f", "Y:%.2f"}})
{
    bool result = ImGui::DragMultiFormatScalarN("##",
                                                ImGuiDataType_Float,
                                                math::value_ptr(data),
                                                2,
                                                DRAG_SPEED,
                                                nullptr,
                                                nullptr,
                                                formats.data());
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragFloat3(math::vec3& data,
                const var_info& info,
                std::array<const char*, 3> formats = {{"X:%.3f", "Y:%.3f", "Z:%.3f"}})
{
    bool result = ImGui::DragMultiFormatScalarN("##",
                                                ImGuiDataType_Float,
                                                math::value_ptr(data),
                                                3,
                                                DRAG_SPEED,
                                                nullptr,
                                                nullptr,
                                                formats.data());
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragFloat4(math::vec4& data,
                const var_info& info,
                std::array<const char*, 4> formats = {{"X:%.3f", "Y:%.3f", "Z:%.3f", "W:%.3f"}})
{
    bool result = ImGui::DragMultiFormatScalarN("##",
                                                ImGuiDataType_Float,
                                                math::value_ptr(data),
                                                4,
                                                DRAG_SPEED,
                                                nullptr,
                                                nullptr,
                                                formats.data());
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragVec2(math::vec2& data, const var_info& info, const math::vec2* reset = nullptr, const char* format = "%.3f")
{
    bool result = ImGui::DragVecN("##",
                                  ImGuiDataType_Float,
                                  math::value_ptr(data),
                                  data.length(),
                                  DRAG_SPEED,
                                  nullptr,
                                  nullptr,
                                  reset ? math::value_ptr(*reset) : nullptr,
                                  format);
    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragVec3(math::vec3& data, const var_info& info, const math::vec3* reset = nullptr, const char* format = "%.3f")
{
    bool result = ImGui::DragVecN("##",
                                  ImGuiDataType_Float,
                                  math::value_ptr(data),
                                  data.length(),
                                  DRAG_SPEED,
                                  nullptr,
                                  nullptr,
                                  reset ? math::value_ptr(*reset) : nullptr,
                                  format);

    ImGui::ActiveItemWrapMousePos();

    return result;
}

bool DragVec4(math::vec4& data, const var_info& info, const math::vec4* reset = nullptr, const char* format = "%.3f")
{
    bool result = ImGui::DragVecN("##",
                                  ImGuiDataType_Float,
                                  math::value_ptr(data),
                                  data.length(),
                                  DRAG_SPEED,
                                  nullptr,
                                  nullptr,
                                  reset ? math::value_ptr(*reset) : nullptr,
                                  format);

    ImGui::ActiveItemWrapMousePos();

    return result;
}

} // namespace

auto inspector_bvec2::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::bvec2&>();
    inspect_result result{};

    enum bflags
    {
        none = 0,
        x = 1 << 0,
        y = 1 << 1,
    };

    int flags = 0;
    flags |= data.x ? bflags::x : 0;
    flags |= data.y ? bflags::y : 0;

    bool mod = false;
    ImGui::BeginGroup();
    mod |= ImGui::CheckboxFlags("X", &flags, bflags::x);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Y", &flags, bflags::y);
    ImGui::EndGroup();
    if(mod)
    {
        data.x = flags & bflags::x;
        data.y = flags & bflags::y;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_bvec3::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::bvec3&>();
    inspect_result result{};

    enum bflags
    {
        none = 0,
        x = 1 << 0,
        y = 1 << 1,
        z = 1 << 2,
    };

    int flags = 0;
    flags |= data.x ? bflags::x : 0;
    flags |= data.y ? bflags::y : 0;
    flags |= data.z ? bflags::z : 0;

    bool mod = false;
    ImGui::BeginGroup();
    mod |= ImGui::CheckboxFlags("X", &flags, bflags::x);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Y", &flags, bflags::y);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Z", &flags, bflags::z);
    ImGui::EndGroup();

    if(mod)
    {
        data.x = flags & bflags::x;
        data.y = flags & bflags::y;
        data.z = flags & bflags::z;
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_bvec4::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::bvec4&>();
    inspect_result result{};

    enum bflags
    {
        none = 0,
        x = 1 << 0,
        y = 1 << 1,
        z = 1 << 2,
        w = 1 << 3,
    };

    int flags = 0;
    flags |= data.x ? bflags::x : 0;
    flags |= data.y ? bflags::y : 0;
    flags |= data.z ? bflags::z : 0;
    flags |= data.w ? bflags::w : 0;

    bool mod = false;
    ImGui::BeginGroup();
    mod |= ImGui::CheckboxFlags("X", &flags, bflags::x);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Y", &flags, bflags::y);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("Z", &flags, bflags::z);
    ImGui::SameLine();
    mod |= ImGui::CheckboxFlags("W", &flags, bflags::w);
    ImGui::EndGroup();

    if(mod)
    {
        data.x = flags & bflags::x;
        data.y = flags & bflags::y;
        data.z = flags & bflags::z;
        data.w = flags & bflags::w;

        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec2::inspect(rtti::context& ctx,
                             entt::meta_any& var,
                             const meta_any_proxy& var_proxy,
                             const var_info& info,
                             const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::vec2&>();
    inspect_result result{};

    static const auto reset = math::zero<math::vec2>();

    if(DragVec2(data, info, &reset))
    {
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec3::inspect(rtti::context& ctx,
                             entt::meta_any& var,
                             const meta_any_proxy& var_proxy,
                             const var_info& info,
                             const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::vec3&>();
    inspect_result result{};

    static const auto reset = math::zero<math::vec3>();

    if(DragVec3(data, info, &reset))
    {
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_vec4::inspect(rtti::context& ctx,
                             entt::meta_any& var,
                             const meta_any_proxy& var_proxy,
                             const var_info& info,
                             const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::vec4&>();
    inspect_result result{};

    static const auto reset = math::zero<math::vec4>();

    if(DragVec4(data, info, &reset))
    {
        result.changed = true;
    }
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    return result;
}

auto inspector_color::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::color&>();
    inspect_result result{};

    if(ImGui::ColorEdit4("##",
                         math::value_ptr(data.value),
                         ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
    {
        result.changed = true;
    }

    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_gradient::inspect(rtti::context& ctx,
                                 entt::meta_any& var,
                                 const meta_any_proxy& var_proxy,
                                 const var_info& info,
                                 const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::gradient<math::color>&>();
    inspect_result result{};


    std::function<bool(math::color&)> draw_element = [](math::color& c)
    {

        
        ImColor imColor(c.value.x, c.value.y, c.value.z, c.value.w);
        if(ImGui::ColorEdit4("##", &imColor.Value.x,
                            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
        {
            c.value.x = imColor.Value.x;
            c.value.y = imColor.Value.y;
            c.value.z = imColor.Value.z;
            c.value.w = imColor.Value.w;
            return true;
        }
        return false;
    };

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);

    if(utils::draw_gradient("##", data, draw_element, math::color::white()))
    {
        result.changed = true;

    }
    ImGui::PopItemWidth();

    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_gradient_frange::inspect(rtti::context& ctx,
                                 entt::meta_any& var,
                                 const meta_any_proxy& var_proxy,
                                 const var_info& info,
                                 const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::gradient<frange_t>&>();
    inspect_result result{};


    std::function<bool(frange_t&)> draw_element = [&](frange_t& c) -> bool
    {
        inspector_range_float inspector;
        entt::meta_any var_proxy_any = entt::forward_as_meta(c);
        auto var_proxy_proxy = make_proxy(var_proxy_any);
        return inspector.inspect(ctx, var_proxy_any, var_proxy_proxy, info, custom).changed;
    };

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);

    if(utils::draw_gradient("##", data, draw_element, frange_t(0.0f, 1.0f)))
    {
        result.changed = true;

    }
    ImGui::PopItemWidth();

    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_quaternion::inspect(rtti::context& ctx,
                                   entt::meta_any& var,
                                   const meta_any_proxy& var_proxy,
                                   const var_info& info,
                                   const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::quat&>();
    inspect_result result{};

    auto val = math::degrees(math::eulerAngles(data));

    static const auto reset = math::zero<math::vec3>();

    // auto val = quat_to_vec4(data);
    if(DragVec3(val, info, &reset, "%.2f°"))
    {
        // data = vec4_to_quat(val);
        data = math::quat(math::radians(val));
        result.changed = true;
    }
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_bbox::inspect(rtti::context& ctx,
                                   entt::meta_any& var,
                                   const meta_any_proxy& var_proxy,
                                   const var_info& info,
                                   const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::bbox&>();
    inspect_result result{};


    static const auto reset = math::zero<math::vec3>();

    ImGui::BeginGroup();
    ImGui::PushID("Min");
    if(DragVec3(data.min, info, &reset, "Min %.2f"))
    {  
        result.changed = true;
    }
    ImGui::PopID();
    ImGui::PushID("Max");
    if(DragVec3(data.max, info, &reset, "Max %.2f"))
    {  
        result.changed = true;
    }
    ImGui::PopID();
    ImGui::EndGroup();
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DrawItemActivityOutline();

    return result;
}


void inspector_transform::before_inspect(const entt::meta_data& prop)
{
    layout_ = std::make_unique<property_layout>();
    layout_->set_data(prop, false);
    open_ = layout_->push_tree_layout(ImGuiTreeNodeFlags_SpanFullWidth);
}

auto inspector_transform::inspect(rtti::context& ctx,
                                  entt::meta_any& var,
                                  const meta_any_proxy& var_proxy,
                                  const var_info& info,
                                  const entt::meta_custom& custom) -> inspect_result
{
    if(!open_)
    {
        return {};
    }
    inspect_result result{};

    auto& data = var.cast<math::transform&>();
    auto position = data.get_translation();
    auto rotation = data.get_rotation();
    auto scale = data.get_scale();
    auto skew = data.get_skew();
    //    auto perspective = data.get_perspective();

    auto type = entt::resolve<math::transform>();
 
    static math::vec3 euler_angles(0.0f, 0.0f, 0.0f);

    math::quat old_quat(math::radians(euler_angles));

    float dot_product = math::dot(old_quat, rotation);
    bool equal = (dot_product > (1.0f - math::epsilon<float>()));
    if(!equal && (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGuizmo::IsUsing()))
    {
        euler_angles = data.get_rotation_euler_degrees();
    }



    ImGui::PushID("Position");
    {
        auto prop = type.data("position"_hs);
        auto prop_name = entt::get_name(prop);
        auto prop_pretty_name = entt::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(prop_name, prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name, "");
        layout.push_layout(false);

        ImGui::SameLine();

        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::GetFrameHeight(),
            [&]()
            {
                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    data.reset_position();
                    result.changed = true;
                    result.edit_finished = true;
                    
                    auto prop_proxy = make_property_proxy(var_proxy, prop);
                    add_property_action(ctx, override_ctx, result, prop_proxy, position, data.get_position(), prop.custom());
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });
        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            static const auto reset = math::zero<math::vec3>();
            if(DragVec3(position, info, &reset))
            {
                data.set_position(position);
                result.changed |= true;

                auto prop_proxy = make_property_proxy(var_proxy, prop);
                add_property_action(ctx, override_ctx, result, prop_proxy, position, data.get_position(), prop.custom());
            }
            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Rotation");
    {
        auto prop = type.data("rotation"_hs);
        auto prop_name = entt::get_name(prop);
        auto prop_pretty_name = entt::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(prop_name, prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name, "");
        layout.push_layout(false);

        ImGui::SameLine();

        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::GetFrameHeight(),
            [&]()
            {
                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    data.reset_rotation();
                    result.changed = true;
                    result.edit_finished = true;
                    
                    auto prop_proxy = make_property_proxy(var_proxy, prop);
                    add_property_action(ctx, override_ctx, result, prop_proxy, rotation, data.get_rotation(), prop.custom());
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });
        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            auto old_euler = euler_angles;
            static const auto reset = math::zero<math::vec3>();
            if(DragVec3(euler_angles, info, &reset, "%.2f°"))
            {
                data.rotate_local(math::radians(euler_angles - old_euler));
                result.changed |= true;

                auto prop_proxy = make_property_proxy(var_proxy, prop);
                add_property_action(ctx, override_ctx, result, prop_proxy, rotation, data.get_rotation(), prop.custom());
            }
            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        ImGui::PopItemWidth();

        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Scale");
    {
        auto prop = type.data("scale"_hs);
        auto prop_name = entt::get_name(prop);
        auto prop_pretty_name = entt::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(prop_name, prop_pretty_name);

        property_layout layout;
        layout.set_data(prop_pretty_name, "");
        layout.push_layout(false);

        ImGui::SameLine();
        static bool locked_scale = false;
        auto label = locked_scale ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT;
        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::CalcItemSize(label).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x,
            [&]()
            {
                if(ImGui::Button(label))
                {
                    locked_scale = !locked_scale;
                }

                ImGui::SetItemTooltipEx("Enable/Disable Constrained Proportions");

                ImGui::SameLine();

                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    data.reset_scale();
                    result.changed = true;
                    result.edit_finished = true;
                    
                    auto prop_proxy = make_property_proxy(var_proxy, prop);
                    add_property_action(ctx, override_ctx, result, prop_proxy, scale, data.get_scale(), prop.custom());
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });

        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            static const auto reset = math::one<math::vec3>();
            auto before_scale = scale;
            if(DragVec3(scale, info, &reset))
            {
                auto delta = scale - before_scale;

                if(locked_scale)
                {
                    before_scale += math::vec3(delta.x);
                    before_scale += math::vec3(delta.y);
                    before_scale += math::vec3(delta.z);
                    scale = before_scale;
                }

                data.set_scale(scale);
                result.changed |= true;

                auto prop_proxy = make_property_proxy(var_proxy, prop);
                add_property_action(ctx, override_ctx, result, prop_proxy, scale, data.get_scale(), prop.custom());
            }

            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        override_ctx.pop_segment();
    }
    ImGui::PopID();

    ImGui::PushID("Skew");
    {
        auto prop = type.data("skew"_hs);
        auto prop_name = entt::get_name(prop);
        auto prop_pretty_name = entt::get_pretty_name(prop);

        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.push_segment(prop_name, prop_pretty_name);
        
        property_layout layout;
        layout.set_data(prop_pretty_name, "");
        layout.push_layout(false);

        ImGui::SameLine();

        ImGui::AlignedItem(
            1.0f,
            ImGui::GetContentRegionAvail().x,
            ImGui::GetFrameHeight(),
            [&]()
            {
                if(ImGui::Button(ICON_MDI_UNDO_VARIANT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                {
                    data.reset_skew();
                    result.changed = true;
                    result.edit_finished = true;
                    
                    auto prop_proxy = make_property_proxy(var_proxy, prop);
                    add_property_action(ctx, override_ctx, result, prop_proxy, skew, data.get_skew(), prop.custom());
                }
                result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SetItemTooltipEx("Reset %s", prop_pretty_name.c_str());
            });
        layout.prepare_for_item();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        {
            static const auto reset = math::zero<math::vec3>();
            if(DragVec3(skew, info, &reset))
            {
                data.set_skew(skew);
                result.changed |= true;
                auto prop_proxy = make_property_proxy(var_proxy, prop);
                add_property_action(ctx, override_ctx, result, prop_proxy, skew, data.get_skew(), prop.custom());
            }

            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopItemWidth();

        override_ctx.pop_segment();
    }
    ImGui::PopID();

    return result;
}
} // namespace unravel
