/*
 * RmlUi Engine Backend
 * 
 * Custom RmlUi backend for UnravelEngine using:
 * - ospp for windowing and input
 * - gfx (bgfx wrapper) for rendering
 */

#pragma once

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/frame_buffer.h>

namespace unravel
{

using KeyDownCallback = bool (*)(Rml::Context* context, Rml::Input::KeyIdentifier key, int key_modifier, float native_dp_ratio, bool priority);

/**
 * @namespace RmlUi_Backend_Engine
 * @brief RmlUi backend implementation for UnravelEngine
 * 
 * This backend integrates RmlUi with the engine's existing systems:
 * - Uses ospp for windowing and input handling
 * - Uses gfx (bgfx wrapper) for rendering
 * - Integrates with engine's asset management and resource systems
 */
namespace RmlUi_Backend_Engine {

/**
 * @brief Initialize the RmlUi backend with engine context
 * @param ctx Engine context for accessing systems
 * @param window_name Name for the UI context (optional)
 * @param width Initial viewport width
 * @param height Initial viewport height
 * @return True if initialization was successful
 */
auto initialize(rtti::context& ctx, const char* window_name = "RmlUi", int width = 1024, int height = 768) -> bool;

/**
 * @brief Shutdown the RmlUi backend and cleanup resources
 */
void shutdown();

/**
 * @brief Get the system interface for RmlUi
 * @return Pointer to the custom system interface
 */
auto get_system_interface() -> Rml::SystemInterface*;

/**
 * @brief Get the render interface for RmlUi
 * @return Pointer to the custom render interface
 */
auto get_render_interface() -> Rml::RenderInterface*;

/**
 * @brief Process input events and forward them to RmlUi
 * @param context RmlUi context to receive events
 * @param key_down_callback Optional callback for key down events
 * @return False if the application should exit
 */
auto process_events(Rml::Context* context, KeyDownCallback key_down_callback = nullptr) -> bool;

/**
 * @brief Request the backend to exit during next event processing
 */
void request_exit();

/**
 * @brief Prepare render state for RmlUi rendering
 * Call this before rendering RmlUi context
 */
void begin_frame();

/**
 * @brief Present the rendered frame
 * Call this after rendering RmlUi context
 */
void present_frame(const gfx::frame_buffer::ptr& framebuffer);

/**
 * @brief Update viewport size (call on window resize)
 * @param width New viewport width
 * @param height New viewport height
 */
void set_viewport(int width, int height);

} // namespace RmlUi_Backend_Engine

} // namespace unravel
