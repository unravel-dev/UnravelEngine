/*
 * RmlUi Engine Platform Interface
 * 
 * Platform integration for RmlUi using ospp windowing and input systems
 */

#pragma once

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <ospp/event.h>
#include <ospp/window.h>

// Forward declarations
namespace input {
    enum class key_code : int32_t;
}

namespace unravel
{

/**
 * @class RmlUi_SystemInterface
 * @brief System interface implementation using engine's ospp systems
 * 
 * This class provides RmlUi with access to system-level functionality
 * through the engine's existing ospp windowing and input systems.
 */
class RmlUi_SystemInterface : public Rml::SystemInterface
{
public:
    RmlUi_SystemInterface();
    ~RmlUi_SystemInterface();

    /**
     * @brief Initialize the system interface with engine context
     * @param ctx Engine context for accessing systems
     * @return True if initialization was successful
     */
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Cleanup resources
     */
    void shutdown();
    // -- Inherited from Rml::SystemInterface --

    /**
     * @brief Get elapsed time since application start
     * @return Time in seconds
     */
    double GetElapsedTime() override;


    /**
     * @brief Translate the input string into the translated string.
     * @param[out] translated Translated string ready for display.
     * @param[in] input String as received from XML.
     * @return Number of translations that occured.
     */
	auto TranslateString(Rml::String& translated, const Rml::String& input) -> int override;

    /**
     * @brief Set the mouse cursor
     * @param cursor_name Name of the cursor to set
     */
    void SetMouseCursor(const Rml::String& cursor_name) override;

    /**
     * @brief Set clipboard text
     * @param text Text to set in clipboard
     */
    void SetClipboardText(const Rml::String& text) override;

    /**
     * @brief Get clipboard text
     * @param text Reference to store clipboard text
     */
    void GetClipboardText(Rml::String& text) override;

    /**
     * @brief Activate virtual keyboard (for mobile/touch devices)
     * @param caret_position Position of text caret
     * @param line_height Height of text line
     */
    void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;

    /**
     * @brief Deactivate virtual keyboard
     */
    void DeactivateKeyboard() override;

    /// Log the specified message.
	/// @param[in] type Type of log message, ERROR, WARNING, etc.
	/// @param[in] message Message to log.
	/// @return True to continue execution, false to break into the debugger.
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

private:
    // Initialize cursors
    void init_cursors();
    void cleanup_cursors();
};

/**
 * @namespace RmlEngine
 * @brief Utility functions for ospp to RmlUi integration
 */
namespace RmlEngine
{

/**
 * @brief Process ospp event and forward to RmlUi context
 * @param context RmlUi context to receive events
 * @param event ospp event to process
 * @return True if event should continue propagating, false if handled
 */
auto input_event_handler(Rml::Context* context, const os::event& event) -> bool;

/**
 * @brief Convert ospp key code to RmlUi key identifier
 * @param ospp_key ospp key code
 * @return RmlUi key identifier
 */
auto convert_key(os::key::code ospp_key) -> Rml::Input::KeyIdentifier;

/**
 * @brief Convert RmlUi key identifier to ospp key code
 * @param rml_key RmlUi key identifier
 * @return ospp key code
 */
auto convert_rml_key_to_ospp(Rml::Input::KeyIdentifier rml_key) -> os::key::code;

/**
 * @brief Convert RmlUi key identifier to engine input key code
 * @param rml_key RmlUi key identifier
 * @return engine input key code
 */
auto convert_rml_key_to_input(Rml::Input::KeyIdentifier rml_key) -> input::key_code;

/**
 * @brief Convert ospp mouse button to RmlUi mouse button
 * @param ospp_button ospp mouse button
 * @return RmlUi mouse button index
 */
auto convert_mouse_button(os::mouse::button ospp_button) -> int;

/**
 * @brief Get current key modifier state
 * @return RmlUi key modifier flags
 */
auto get_key_modifier_state() -> int;

} // namespace RmlEngine

} // namespace unravel
