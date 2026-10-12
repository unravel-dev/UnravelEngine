#pragma once

#include "../script_system.h"

#include <string>

namespace Rml
{
class Event;
}

namespace unravel
{

/**
 * @brief The payload scripts receive for an RmlUi event (see UIEventManager.InternalDispatchPayload).
 * @param event The event being dispatched; its target and current element get handles.
 * @param is_capture_listener Whether a capture-phase listener receives it.
 * @param text Receives the event's text: typed text, a changed value, an animated property or form values.
 */
auto make_ui_event_payload(const Rml::Event& event, bool is_capture_listener, std::string& text)
    -> script_system::ui_event_payload;

} // namespace unravel
