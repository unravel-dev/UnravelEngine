#include "ui_element_queries.h"

#include "ui_element_handles.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FontMetrics.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/RenderBox.h>
#include <RmlUi/Core/StyleTypes.h>

#include <algorithm>
#include <array>
#include <unordered_map>

namespace unravel
{

void update_ui_layout_if_stale(Rml::ElementDocument& document)
{
    static std::unordered_map<ui_element_handle, uint64_t> updated_at_serial;
    auto& updated_at = updated_at_serial[get_ui_element_handle(&document)];
    const auto serial = get_ui_layout_serial();
    if(updated_at != serial)
    {
        document.UpdateDocument();
        updated_at = serial;
    }
}

auto get_ui_element_rect(Rml::Element& element, Rml::BoxArea area) -> math::vec4
{
    if(auto* document = element.GetOwnerDocument())
    {
        update_ui_layout_if_stale(*document);
    }
    const auto offset = element.GetAbsoluteOffset(area);
    const auto size = element.GetBox().GetSize(area);
    return {offset.x, offset.y, size.x, size.y};
}

namespace
{
auto is_inline_display(Rml::Style::Display display) -> bool
{
    return display == Rml::Style::Display::Inline || display == Rml::Style::Display::InlineBlock ||
           display == Rml::Style::Display::InlineFlex;
}
} // namespace

auto get_ui_element_text(const Rml::Element& element, const ui_text_options& options) -> std::string
{
    std::string text;
    for(int i = 0; i < element.GetNumChildren(); ++i)
    {
        auto* child = element.GetChild(i);
        if(auto* text_node = rmlui_dynamic_cast<Rml::ElementText*>(child))
        {
            text += text_node->GetText();
            continue;
        }
        const auto& computed = child->GetComputedValues();
        if(options.visible_only &&
           (computed.display() == Rml::Style::Display::None || computed.visibility() != Rml::Style::Visibility::Visible))
        {
            continue;
        }
        const bool is_block = !options.block_separator.empty() && !is_inline_display(computed.display());
        if(is_block)
        {
            text += options.block_separator;
        }
        text += get_ui_element_text(*child, options);
        if(is_block)
        {
            text += options.block_separator;
        }
    }
    return text;
}

auto is_ui_element_styled_visible(Rml::Element& element) -> bool
{
    // A value resolved from var() lives in storage the next GetProperty call reuses: read each one at once.
    const auto* display = element.GetProperty(Rml::PropertyId::Display);
    const bool is_displayed = !display || display->Get<int>() != static_cast<int>(Rml::Style::Display::None);
    const auto* visibility = element.GetProperty(Rml::PropertyId::Visibility);
    const bool is_visible = !visibility || visibility->Get<int>() == static_cast<int>(Rml::Style::Visibility::Visible);
    return is_displayed && is_visible;
}

auto get_ui_element_at(Rml::ElementDocument& document, const math::vec2& point) -> Rml::Element*
{
    auto* context = document.GetContext();
    if(!context)
    {
        return nullptr;
    }
    update_ui_layout_if_stale(document);
    auto* element = context->GetElementAtPoint({point.x, point.y});
    // The context's root, or an element of another document of the same context, is not in this document.
    return element && element->GetOwnerDocument() == &document ? element : nullptr;
}

namespace
{
constexpr std::array<hpp::string_view, 7> POINTER_PRESS_EVENTS = {"click",
                                                                  "dblclick",
                                                                  "mousedown",
                                                                  "mouseup",
                                                                  "dragstart",
                                                                  "drag",
                                                                  "dragend"};
/// Elements that take presses whatever they paint.
constexpr std::array<hpp::string_view, 6> INTERACTIVE_TAGS = {"input", "button", "select", "textarea", "handle", "tab"};
/// Elements that paint their whole box with content of their own.
constexpr std::array<hpp::string_view, 4> IMAGE_TAGS = {"img", "progress", "svg", "lottie"};
/// Attribute prefixes of event handlers in the markup: inline ones and data binding ones.
constexpr std::array<hpp::string_view, 2> HANDLER_ATTRIBUTE_PREFIXES = {"on", "data-event-"};
/// How far a scroll range may exceed the client size before the element scrolls, as RmlUi rounds.
constexpr float SCROLL_TOLERANCE = 0.5f;

enum corner : size_t
{
    top_left,
    top_right,
    bottom_right,
    bottom_left,
};

template<size_t N>
auto contains_name(const std::array<hpp::string_view, N>& names, hpp::string_view name) -> bool
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

auto is_point_in_rect(Rml::Vector2f point, Rml::Vector2f position, Rml::Vector2f size) -> bool
{
    return point.x >= position.x && point.y >= position.y && point.x <= position.x + size.x &&
           point.y <= position.y + size.y;
}

/// Shrinks radii that do not fit their box by one factor, as CSS does, so adjacent corners never overlap.
auto fit_corner_radii(Rml::CornerSizes radii, Rml::Vector2f size) -> Rml::CornerSizes
{
    float scale = 1.0f;
    const auto fit = [&](float radius_sum, float length)
    {
        if(radius_sum > length)
        {
            scale = std::min(scale, length / radius_sum);
        }
    };
    fit(radii[top_left] + radii[top_right], size.x);
    fit(radii[bottom_left] + radii[bottom_right], size.x);
    fit(radii[top_left] + radii[bottom_left], size.y);
    fit(radii[top_right] + radii[bottom_right], size.y);
    for(float& radius : radii)
    {
        radius *= scale;
    }
    return radii;
}

/// Whether @p point, relative to a box's top left, is inside the box of @p size with rounded corners.
auto is_point_in_rounded_rect(Rml::Vector2f point, Rml::Vector2f size, const Rml::CornerSizes& radii) -> bool
{
    if(!is_point_in_rect(point, {0.0f, 0.0f}, size))
    {
        return false;
    }
    constexpr std::array<float, 4> outward_x = {-1.0f, 1.0f, 1.0f, -1.0f};
    constexpr std::array<float, 4> outward_y = {-1.0f, -1.0f, 1.0f, 1.0f};
    for(size_t i = 0; i < radii.size(); ++i)
    {
        const float radius = radii[i];
        const Rml::Vector2f center(outward_x[i] < 0.0f ? radius : size.x - radius,
                                   outward_y[i] < 0.0f ? radius : size.y - radius);
        const Rml::Vector2f offset = point - center;
        const bool is_in_corner = offset.x * outward_x[i] > 0.0f && offset.y * outward_y[i] > 0.0f;
        if(is_in_corner && offset.SquaredMagnitude() > radius * radius)
        {
            return false;
        }
    }
    return true;
}

/// Whether a visible side of the border covers @p point, which is in the border ring of @p box.
auto is_border_side_at(const Rml::ComputedValues& computed, const Rml::Box& box, Rml::Vector2f point) -> bool
{
    const Rml::Vector2f size = box.GetSize(Rml::BoxArea::Border);
    const float top = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Top);
    const float right = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Right);
    const float bottom = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Bottom);
    const float left = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Left);
    const bool shows_top = top > 0.0f && computed.border_top_color().alpha > 0;
    const bool shows_right = right > 0.0f && computed.border_right_color().alpha > 0;
    const bool shows_bottom = bottom > 0.0f && computed.border_bottom_color().alpha > 0;
    const bool shows_left = left > 0.0f && computed.border_left_color().alpha > 0;
    const bool is_on_top = point.y < top;
    const bool is_on_right = point.x > size.x - right;
    const bool is_on_bottom = point.y > size.y - bottom;
    const bool is_on_left = point.x < left;
    if(is_on_top || is_on_right || is_on_bottom || is_on_left)
    {
        return (is_on_top && shows_top) || (is_on_right && shows_right) || (is_on_bottom && shows_bottom) ||
               (is_on_left && shows_left);
    }
    // Between a rounded corner's outer and inner curve: both sides that meet there draw it.
    const bool shows_vertical_side = point.x < size.x * 0.5f ? shows_left : shows_right;
    const bool shows_horizontal_side = point.y < size.y * 0.5f ? shows_top : shows_bottom;
    return shows_vertical_side || shows_horizontal_side;
}

struct element_paint
{
    /// A decorator, a backdrop filter or an image covers the whole box.
    bool fills_box = false;
    bool has_background = false;
    bool has_border = false;
};

auto get_element_paint(const Rml::Element& element, const Rml::ComputedValues& computed) -> element_paint
{
    element_paint paint;
    paint.fills_box = computed.has_decorator() || computed.has_backdrop_filter() ||
                      contains_name(IMAGE_TAGS, element.GetTagName());
    paint.has_background = computed.background_color().alpha > 0;
    paint.has_border = (computed.border_top_width() > 0.0f && computed.border_top_color().alpha > 0) ||
                       (computed.border_right_width() > 0.0f && computed.border_right_color().alpha > 0) ||
                       (computed.border_bottom_width() > 0.0f && computed.border_bottom_color().alpha > 0) ||
                       (computed.border_left_width() > 0.0f && computed.border_left_color().alpha > 0);
    return paint;
}

/// Whether one box of an element paints at @p point, relative to the box's border area.
auto is_box_painted_at(const Rml::ComputedValues& computed,
                       const element_paint& paint,
                       const Rml::Box& box,
                       Rml::Vector2f point) -> bool
{
    const Rml::Vector2f size = box.GetSize(Rml::BoxArea::Border);
    const Rml::CornerSizes radii = fit_corner_radii(computed.border_radius(), size);
    if(!is_point_in_rounded_rect(point, size, radii))
    {
        return false;
    }
    if(paint.fills_box)
    {
        return true;
    }
    const float top = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Top);
    const float right = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Right);
    const float bottom = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Bottom);
    const float left = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Left);
    Rml::CornerSizes inner_radii = radii;
    inner_radii[top_left] = std::max(radii[top_left] - std::max(left, top), 0.0f);
    inner_radii[top_right] = std::max(radii[top_right] - std::max(right, top), 0.0f);
    inner_radii[bottom_right] = std::max(radii[bottom_right] - std::max(right, bottom), 0.0f);
    inner_radii[bottom_left] = std::max(radii[bottom_left] - std::max(left, bottom), 0.0f);
    const Rml::Vector2f padding_position = box.GetPosition(Rml::BoxArea::Padding);
    const Rml::Vector2f padding_size = box.GetSize(Rml::BoxArea::Padding);
    if(is_point_in_rounded_rect(point - padding_position, padding_size, inner_radii))
    {
        return paint.has_background;
    }
    return paint.has_border && is_border_side_at(computed, box, point);
}

/// Whether @p element itself, not its children, paints at @p point, in its projected pixels.
auto is_element_painted_at(Rml::Element& element, Rml::Vector2f point) -> bool
{
    const auto& computed = element.GetComputedValues();
    if(computed.pointer_events() == Rml::Style::PointerEvents::None || computed.opacity() <= 0.0f)
    {
        return false;
    }
    const element_paint paint = get_element_paint(element, computed);
    if(!paint.fills_box && !paint.has_background && !paint.has_border)
    {
        return false;
    }
    const Rml::Vector2f origin = element.GetAbsoluteOffset(Rml::BoxArea::Border);
    for(int i = 0; i < element.GetNumBoxes(); ++i)
    {
        Rml::Vector2f box_offset;
        const Rml::Box& box = element.GetBox(i, box_offset);
        if(is_box_painted_at(computed, paint, box, point - origin - box_offset))
        {
            return true;
        }
    }
    return false;
}

auto is_blank(const Rml::String& text) -> bool
{
    return std::all_of(text.begin(),
                       text.end(),
                       [](char c)
                       {
                           return c == ' ' || c == '\t' || c == '\n' || c == '\r';
                       });
}

/// Whether a line of @p text covers @p point, in its projected pixels, between the font's ascent and descent.
auto is_text_painted_at(Rml::ElementText& text, Rml::Vector2f point) -> bool
{
    const auto& computed = text.GetComputedValues();
    if(computed.pointer_events() == Rml::Style::PointerEvents::None || computed.opacity() <= 0.0f)
    {
        return false;
    }
    if(computed.color().alpha == 0 && !computed.has_font_effect())
    {
        return false;
    }
    auto* font_engine = Rml::GetFontEngineInterface();
    const Rml::FontFaceHandle font = text.GetFontFaceHandle();
    if(!font_engine || !font)
    {
        return false;
    }
    const Rml::FontMetrics& metrics = font_engine->GetFontMetrics(font);
    const Rml::Vector2f origin = text.GetAbsoluteOffset();
    const auto& lines = text.GetLines();
    return std::any_of(lines.begin(),
                       lines.end(),
                       [&](const Rml::ElementText::Line& line)
                       {
                           const Rml::Vector2f baseline = origin + line.position;
                           return line.width > 0 && !is_blank(line.text) &&
                                  is_point_in_rect(point,
                                                   {baseline.x, baseline.y - metrics.ascent},
                                                   {static_cast<float>(line.width), metrics.ascent + metrics.descent});
                       });
}

/// Whether @p element keeps its children inside its padding box, so a point outside it misses them.
auto clips_children(const Rml::ComputedValues& computed) -> bool
{
    const Rml::Style::Clip::Type clip = computed.clip().GetType();
    const bool overflows_hidden = computed.overflow_x() != Rml::Style::Overflow::Visible ||
                                  computed.overflow_y() != Rml::Style::Overflow::Visible;
    return clip == Rml::Style::Clip::Type::Always || (clip != Rml::Style::Clip::Type::None && overflows_hidden);
}

/// Whether @p element or one of its descendants paints at @p point (context pixels).
auto is_subtree_painted_at(Rml::Element& element, Rml::Vector2f point) -> bool
{
    if(!element.IsVisible())
    {
        return false;
    }
    // A singular transform shows the element, and so its children, edge on: nothing of it is under the point.
    Rml::Vector2f projected = point;
    if(!element.Project(projected))
    {
        return false;
    }
    if(auto* text = rmlui_dynamic_cast<Rml::ElementText*>(&element))
    {
        return is_text_painted_at(*text, projected);
    }
    if(is_element_painted_at(element, projected))
    {
        return true;
    }
    if(clips_children(element.GetComputedValues()) &&
       !is_point_in_rect(projected,
                         element.GetAbsoluteOffset(Rml::BoxArea::Padding),
                         element.GetBox().GetSize(Rml::BoxArea::Padding)))
    {
        return false;
    }
    // Scrollbars are children outside the DOM; they take the pointer like the rest.
    constexpr bool include_non_dom_children = true;
    for(int i = 0; i < element.GetNumChildren(include_non_dom_children); ++i)
    {
        if(is_subtree_painted_at(*element.GetChild(i), point))
        {
            return true;
        }
    }
    return false;
}

auto is_scroll_container(Rml::Element& element) -> bool
{
    const auto& computed = element.GetComputedValues();
    const auto scrolls = [](Rml::Style::Overflow overflow)
    {
        return overflow == Rml::Style::Overflow::Auto || overflow == Rml::Style::Overflow::Scroll;
    };
    return (scrolls(computed.overflow_x()) &&
            element.GetScrollWidth() > element.GetClientWidth() + SCROLL_TOLERANCE) ||
           (scrolls(computed.overflow_y()) &&
            element.GetScrollHeight() > element.GetClientHeight() + SCROLL_TOLERANCE);
}

auto is_pointer_press_handler_attribute(hpp::string_view name) -> bool
{
    return std::any_of(HANDLER_ATTRIBUTE_PREFIXES.begin(),
                       HANDLER_ATTRIBUTE_PREFIXES.end(),
                       [&](hpp::string_view prefix)
                       {
                           return name.starts_with(prefix) && is_ui_pointer_press_event(name.substr(prefix.size()));
                       });
}

auto has_pointer_press_handler(Rml::Element& element) -> bool
{
    if(has_ui_script_pointer_listener(&element))
    {
        return true;
    }
    const auto& attributes = element.GetAttributes();
    return std::any_of(attributes.begin(),
                       attributes.end(),
                       [](const auto& attribute)
                       {
                           return is_pointer_press_handler_attribute(attribute.first);
                       });
}

auto takes_presses(Rml::Element& element) -> bool
{
    return contains_name(INTERACTIVE_TAGS, element.GetTagName()) ||
           element.GetComputedValues().drag() != Rml::Style::Drag::None || is_scroll_container(element) ||
           has_pointer_press_handler(element);
}
} // namespace

auto is_ui_pointer_press_event(hpp::string_view event_type) -> bool
{
    return contains_name(POINTER_PRESS_EVENTS, event_type);
}

auto is_ui_content_at(Rml::Element* target, Rml::Vector2f point) -> bool
{
    Rml::ElementDocument* document = target ? target->GetOwnerDocument() : nullptr;
    Rml::Context* context = document ? document->GetContext() : nullptr;
    if(!context)
    {
        return false;
    }
    for(Rml::Element* element = target; element != document; element = element->GetParentNode())
    {
        if(takes_presses(*element))
        {
            return true;
        }
    }
    // The whole context: an element below a transparent target, in this document or another, may paint there.
    return is_subtree_painted_at(*context->GetRootElement(), point);
}

} // namespace unravel
