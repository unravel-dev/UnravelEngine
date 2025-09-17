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

namespace unravel
{

/**
 * @class SystemInterface_Engine
 * @brief System interface implementation using engine's ospp systems
 * 
 * This class provides RmlUi with access to system-level functionality
 * through the engine's existing ospp windowing and input systems.
 */
class SystemInterface_Engine : public Rml::SystemInterface
{
public:
    SystemInterface_Engine();
    ~SystemInterface_Engine();

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

    /**
     * @brief Set the window for cursor management
     * @param window Pointer to the main engine window
     */
    void set_window(const std::unique_ptr<class render_window>& window);

    // -- Inherited from Rml::SystemInterface --

    /**
     * @brief Get elapsed time since application start
     * @return Time in seconds
     */
    double GetElapsedTime() override;

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

private:
    rtti::context* ctx_ = nullptr;
    const std::unique_ptr<class render_window>* window_ = nullptr;

    // Initialize cursors
    void init_cursors();
    void cleanup_cursors();
};

/**
 * @namespace RmlEngine
 * @brief Utility functions for ospp to RmlUi integration
 */
namespace RmlEngine {

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
