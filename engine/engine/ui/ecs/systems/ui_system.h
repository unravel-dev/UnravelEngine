#pragma once

#include <engine/engine_export.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <ospp/event.h>
#include <hpp/utility.hpp>

#include <string>
#include <entt/entt.hpp>

// Forward declarations
namespace Rml
{
    class Context;
    class ElementDocument;
    class Element;
}

namespace unravel
{
    struct ui_document_component;

/**
 * @class ui_system
 * @brief System responsible for managing user interface components and rendering.
 * 
 * This system integrates RmlUi for HTML/CSS-based user interfaces with the engine's
 * rendering and input systems. It provides a high-level API for loading and managing
 * UI documents.
 */
struct ui_system
{
    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_destroy_component(entt::registry& r, entt::entity e);

    /**
     * @brief Initializes the UI system with the given context.
     * @param ctx The context to initialize with.
     * @return True if initialization was successful, false otherwise.
     */
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Deinitializes the UI system with the given context.
     * @param ctx The context to deinitialize.
     * @return True if deinitialization was successful, false otherwise.
     */
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Update UI system (called every frame)
     * @param ctx Engine context
     * @param dt Delta time since last frame
     */
    void on_frame_update(rtti::context& ctx, delta_t dt);

    /**
     * @brief Render UI (called every frame after update)
     * @param ctx Engine context
     * @param dt Delta time since last frame
     */
    void on_frame_render(rtti::context& ctx, delta_t dt);

    /**
     * @brief Handle window resize events
     * @param width New window width
     * @param height New window height
     */
    void on_window_resize(int width, int height);

    /**
     * @brief Get the main RmlUi context
     * @return Pointer to RmlUi context, or nullptr if not initialized
     */
    auto get_context() -> Rml::Context*;


    /**
     * @brief Handle OS events and forward to RmlUi
     * @param ctx Engine context
     * @param event OS event to process
     */
    void on_os_event(rtti::context& ctx, os::event& event);

    /**
     * @brief Load fonts
     */
    void load_fonts();
    
    /**
     * @brief Register component creation/destruction callbacks with ECS
     * @param ctx Engine context
     */
    void register_component_callbacks(rtti::context& ctx);

private:
    /**
     * @brief Update all UI document components (load documents, manage lifecycle)
     * @param ctx Engine context
     */
    void update_ui_document_components(rtti::context& ctx);
    
    /**
     * @brief Load a UI document for a specific component
     * @param component The ui_document_component to load the document for
     * @return True if loading was successful, false otherwise
     */
    auto load_ui_document(ui_document_component& component) -> bool;



    auto is_root_element(rtti::context& ctx, Rml::Element* element) -> bool;
    
    Rml::Context* ui_context_ = nullptr;
    Rml::ElementDocument* test_document_ = nullptr;
    std::shared_ptr<int> sentinel_ = std::make_shared<int>();
};

} // namespace unravel
