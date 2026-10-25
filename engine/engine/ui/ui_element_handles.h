#pragma once

#include <RmlUi/Core/Types.h>
#include <cstdint>

namespace Rml
{
class ElementDocument;
}

namespace unravel
{

/**
 * @brief What scripts hold instead of an RmlUi element pointer.
 *
 * A handle is a slot index plus a generation. It resolves to its element while the element lives
 * and to nothing afterwards, so a script that keeps a reference to an element a rebuild or a
 * reload destroyed can never reach freed memory, nor a new element that took the freed address
 * (RmlUi pools elements). An element has one handle for its whole life; 0 is no element.
 */
using ui_element_handle = std::intptr_t;

/**
 * @brief The handle of @p element, created when the element first crosses into scripts.
 * @return 0 for a null element.
 */
auto get_ui_element_handle(Rml::Element* element) -> ui_element_handle;

/**
 * @brief The element @p handle refers to, or null once it was destroyed (or for 0).
 */
auto resolve_ui_element_handle(ui_element_handle handle) -> Rml::Element*;

/**
 * @brief Takes the oldest handle whose element was destroyed since it was last asked.
 *
 * Scripts drain these once per frame to drop wrappers and event subscriptions of dead elements.
 * @return 0 when none is left.
 */
auto take_destroyed_ui_element_handle() -> ui_element_handle;

/**
 * @brief Keeps an element that is in no tree yet (created or cloned by a script) until a script
 * adds it to a tree, or until @p document unloads.
 * @return The element.
 */
auto adopt_orphan_ui_element(Rml::ElementDocument* document, Rml::ElementPtr element) -> Rml::Element*;

/**
 * @brief Gives up ownership of a kept orphan, to add it to a tree.
 * @return The element's owning pointer, or null when @p element is not a kept orphan.
 */
auto take_orphan_ui_element(Rml::Element* element) -> Rml::ElementPtr;

/**
 * @brief Records that a script started listening on @p element for a pointer press event (see
 * is_ui_pointer_press_event), once per event type and phase.
 */
void add_ui_script_pointer_listener(Rml::Element* element);

/**
 * @brief Undoes one add_ui_script_pointer_listener. An element's destruction drops all of them.
 */
void remove_ui_script_pointer_listener(Rml::Element* element);

/**
 * @brief Whether a script listens on @p element for a pointer press event.
 */
auto has_ui_script_pointer_listener(Rml::Element* element) -> bool;

/**
 * @brief Records that a script changed a document (content, classes, styles): layout read after
 * this must update the document first.
 */
void mark_ui_layout_stale();

/**
 * @brief Changes recorded by mark_ui_layout_stale; a reader that saw the same value has current layout.
 */
auto get_ui_layout_serial() -> uint64_t;

/**
 * @brief Registers the RmlUi plugin that ends the handles of destroyed elements. Call after
 * RmlUi is initialised.
 */
void register_ui_element_handles();

/**
 * @brief Releases the kept orphans, unregisters the plugin and ends every handle. Call before
 * RmlUi shuts down.
 */
void unregister_ui_element_handles();

} // namespace unravel
