#pragma once

#include <engine/engine_export.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <ospp/event.h>
#include <hpp/utility.hpp>
#include <engine/ecs/scene.h>
#include <math/math.h>
#include <string>
#include <entt/entt.hpp>
#include <graphics/frame_buffer.h>
#include <engine/ui/rmlui/RmlUi_RenderLayerStack.h>

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
    static void on_load_component(entt::registry& r, entt::entity e);
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
    void on_frame_update(rtti::context& ctx, entt::handle camera_entity, scene& scn, delta_t dt);

    /**
     * @brief Render UI (updates documents and renders). Call from render path with the active camera.
     * @param output Output frame buffer
     * @param camera_entity Camera entity for viewport and world-space input (can be null)
     * @param dt Delta time since last frame
     */
    void on_frame_render(const gfx::frame_buffer::ptr& output, entt::handle camera_entity, scene& scn, delta_t dt);


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

    auto is_debugger_enabled() const -> bool;
    void set_debugger_enabled(bool enabled);
private:
    /**
     * @brief Update all UI document components (load documents, manage lifecycle, process input)
     * @param ctx Engine context
     * @param camera_entity Camera entity for viewport and world-space input (can be null)
     */
    void update_ui_document_components(rtti::context& ctx, scene& scn, entt::handle camera_entity);
    
    /**
     * @brief Load a UI document for a specific component
     * @param entity Entity owning the component
     * @param component The ui_document_component to load the document for
     * @param reload_stylesheet Whether to reload stylesheet
     * @return True if loading was successful, false otherwise
     */
    auto load_ui_document(entt::entity entity, ui_document_component& component, bool reload_stylesheet = false) -> bool;



    auto is_not_root_element(scene& scn, Rml::Element* element) -> bool;

    void load_font(const std::string& path);

    /// Ensure framebuffer exists for document; create or resize if needed
    void ensure_document_framebuffer(ui_document_component& comp);


    auto process_mouse_move(scene& scn, Rml::Context* context, int x, int y) -> bool;

    auto process_event(scene& scn, Rml::Context* context, os::event& event) -> bool;
    /// Debug/legacy context (empty, used for RmlUi debugger host - menu, info, log)
    Rml::Context* debug_context_ = nullptr;
    std::shared_ptr<RmlUi_RenderLayerStack> debug_render_layer_stack_ = nullptr;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>();
    std::set<std::string> fonts_loaded_;
};

} // namespace unravel
