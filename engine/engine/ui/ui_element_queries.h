#pragma once
#include <engine/engine_export.h>

#include <RmlUi/Core/Box.h>
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

} // namespace unravel
