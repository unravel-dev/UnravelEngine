#pragma once
#include <engine/engine_export.h>

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Types.h>
#include <hpp/string_view.hpp>
#include <math/math.h>

#include <string>

namespace Rml
{
class Element;
class ElementDocument;
} // namespace Rml

namespace unravel
{

/**
 * @brief Brings @p document's layout up to date when anything changed it since it was last read
 * (see mark_ui_layout_stale): reads of geometry after a change see the new layout.
 */
void update_ui_layout_if_stale(Rml::ElementDocument& document);

/**
 * @brief The element's box in its document's pixels: x, y of the top left, width, height.
 */
auto get_ui_element_rect(Rml::Element& element, Rml::BoxArea area = Rml::BoxArea::Border) -> math::vec4;

/**
 * @brief How get_ui_element_text reads an element's text.
 */
struct ui_text_options
{
    /// Put around the text of every descendant laid out as a block (a block, a flex or a table box),
    /// so that "<div>A</div><div>B</div>" reads "A B" rather than "AB"; inline elements join their
    /// neighbours unchanged. Empty joins everything.
    std::string block_separator;
    /// Leaves out descendants that are not shown (display none or visibility hidden).
    bool visible_only = false;
};

/**
 * @brief The text of the element and of its descendants, in document order.
 */
auto get_ui_element_text(const Rml::Element& element, const ui_text_options& options = {}) -> std::string;

/**
 * @brief Whether the element is shown by its own style: display not 'none' and visibility 'visible'.
 */
auto is_ui_element_styled_visible(Rml::Element& element) -> bool;

/**
 * @brief The topmost element of @p document under @p point (document pixels), with the layout
 * current; null when the point is over no element of this document.
 */
auto get_ui_element_at(Rml::ElementDocument& document, const math::vec2& point) -> Rml::Element*;

/**
 * @brief Whether a handler for @p event_type makes its element take pointer presses: click,
 * dblclick, mousedown, mouseup and the drag events. Hover events (mouseover, ...) do not.
 */
auto is_ui_pointer_press_event(hpp::string_view event_type) -> bool;

/**
 * @brief Whether a pointer at @p point (its context's pixels) is on the UI, which then keeps
 * presses from the game.
 *
 * It is when something paints there: text, an image, a background, a border, a decorator or a
 * backdrop filter of an element that takes the pointer (not 'pointer-events: none'), inside its
 * rounded corners. It is also when @p target, the element under the point, or an ancestor takes
 * presses: a form control, a draggable or scrolling element, or an element with a pointer press
 * handler in its markup ('onclick', 'data-event-click', ...) or from a script. Empty space is not
 * the UI's, even inside a container that spans the whole document, and neither are the handlers of
 * the document itself, which see every press in it.
 * @return false when @p target is null or in no document (the context's root).
 */
auto is_ui_content_at(Rml::Element* target, Rml::Vector2f point) -> bool;

} // namespace unravel
