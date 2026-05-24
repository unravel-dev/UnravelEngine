#pragma once

#include "entt/entity/fwd.hpp"
#include <engine/engine_export.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <ospp/event.h>
#include <hpp/utility.hpp>
#include <engine/ecs/scene.h>
#include <math/math.h>
#include <string>
#include <vector>
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
    class camera;
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
     * @brief Update world-space UI documents (common + world-space specific). Call before render_world_space.
     */
    void update_world_space(rtti::context& ctx, entt::handle camera_entity, scene& scn, bool& process_input);

    /**
     * @brief Render world-space UI documents (sort + frame-state render). Call after update_world_space.
     */
    void render_world_space(rtti::context& ctx, entt::handle camera_entity, scene& scn, delta_t dt);

    /**
     * @brief Update screen-space UI documents (common + screen-space specific). Call before render_screen_space.
     */
    void update_screen_space(rtti::context& ctx, entt::handle camera_entity, scene& scn, bool& process_input);

    /**
     * @brief Render screen-space overlay UI documents to output. Call after update_screen_space.
     */
    void render_screen_space(rtti::context& ctx, const gfx::frame_buffer::ptr& output, entt::handle camera_entity,
                            scene& scn, delta_t dt);



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

    void release_resources();
    
    /**
     * @brief Register component creation/destruction callbacks with ECS
     * @param ctx Engine context
     */
    void register_component_callbacks(rtti::context& ctx);

    auto is_debugger_enabled() const -> bool;
    void set_debugger_enabled(bool enabled);
private:

    /**
     * @brief Update UI documents (load, reload, visibility). No camera dependency.
     * @param ctx Engine context
     * @param scn Scene containing UI documents
     */
    void update_ui_documents(rtti::context& ctx, scene& scn);

    /**
     * @brief Update a single world-space document (dimensions, density, input). Call from render path, back-to-front.
     * @param ctx Engine context for input
     * @param scn Scene for process_mouse_move
     * @param handle Entity handle with ui_document_component and transform_component
     * @param comp UI document component
     * @param cam Camera for projection
     * @param dp_ratio Density-independent pixel ratio
     * @param hit_found In/out: true if a closer doc already consumed the mouse
     */
    void update_world_space_document(rtti::context& ctx, scene& scn, entt::handle handle, ui_document_component& comp,
                                    const camera& cam, float dp_ratio, bool process_input, bool& hit_found);

    /**
     * @brief Update a single screen-space document (dimensions, density, input). Call from render path.
     * @param ctx Engine context for input
     * @param comp UI document component
     * @param viewport_width Viewport width
     * @param viewport_height Viewport height
     * @param dp_ratio Density-independent pixel ratio
     * @param scn Scene for process_mouse_move
     * @param hit_found In/out: true if a previous doc already consumed the mouse
     */
    void update_screen_space_document(rtti::context& ctx, Rml::Context* context, int viewport_width,
                                     int viewport_height, float dp_ratio, scene& scn, bool process_input, bool& hit_found);
    

    void update_ui_document_common(entt::handle handle, ui_document_component& ui_comp, bool is_playing);                                 
    /**
     * @brief Load a UI document for a specific component
     * @param entity Entity owning the component
     * @param component The ui_document_component to load the document for
     * @return True if loading was successful, false otherwise
     */
    auto load_ui_document(entt::handle entity, ui_document_component& component, bool log_error = true) -> bool;



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
