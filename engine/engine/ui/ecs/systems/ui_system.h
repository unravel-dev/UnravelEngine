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
#include <engine/input/action_map/coord.hpp>
#include <engine/ui/rmlui/RmlUi_RenderLayerStack.h>
#include <functional>

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
     * Documents drawn straight into the scene (ui_document_component::draws_into_scene) are only
     * updated here: the scene's UI pass draws them with render_world_space_into.
     */
    void render_world_space(rtti::context& ctx, entt::handle camera_entity, scene& scn, delta_t dt);

    /**
     * @brief Draws a world-space document straight into @p output, a camera's target with its depth,
     * through @p document_to_clip from document pixels to the target's clip space. The document tests
     * the depth and leaves its plane in it. A document that meets a clip mask or a backdrop filter
     * renders to its own framebuffer from the next frame on.
     */
    void render_world_space_into(ui_document_component& comp,
                                 const gfx::frame_buffer::ptr& output,
                                 const math::mat4& document_to_clip);

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

    void on_frame_begin(rtti::context& ctx, delta_t dt);

    /**
     * @brief Load fonts
     */
    void load_fonts();

    void release_resources();

    /**
     * @brief Releases the images whose texture asset was imported or reloaded since RmlUi loaded
     * them, and lays the documents out again, so they show the new image at its new size.
     */
    void refresh_changed_textures();

    /**
     * @brief Register component creation/destruction callbacks with ECS
     * @param ctx Engine context
     */
    void register_component_callbacks(rtti::context& ctx);

    auto is_debugger_enabled() const -> bool;
    void set_debugger_enabled(bool enabled);

    /**
     * @brief Update UI documents (load, reload, visibility). No camera dependency.
     * @param ctx Engine context
     * @param scn Scene containing UI documents
     */
    void update_ui_documents(rtti::context& ctx, scene& scn);

    /**
     * @brief Update a single world-space document (dimensions, density, input). Call from render path, back-to-front.
     * @param ctx Engine context for input
     * @param handle Entity handle with ui_document_component and transform_component
     * @param comp UI document component
     * @param cam Camera for projection
     * @param dp_ratio Density-independent pixel ratio
     * @param hit_found In/out: true if a closer doc already has its content under the mouse
     */
    void update_world_space_document(rtti::context& ctx, entt::handle handle, ui_document_component& comp,
                                    const camera& cam, float dp_ratio, bool process_input, bool& hit_found);

    /**
     * @brief Update a single screen-space document (dimensions, density, input). Call from render path.
     * @param ctx Engine context for input
     * @param comp UI document component
     * @param viewport_width Viewport width
     * @param viewport_height Viewport height
     * @param dp_ratio Density-independent pixel ratio
     * @param hit_found In/out: true if a previous doc already has its content under the mouse
     */
    void update_screen_space_document(rtti::context& ctx, Rml::Context* context, int viewport_width,
                                     int viewport_height, float dp_ratio, bool process_input, bool& hit_found);
    

    void update_ui_document_common(entt::handle handle, ui_document_component& ui_comp, bool is_playing);                                 
    /**
     * @brief Load a UI document for a specific component
     * @param entity Entity owning the component
     * @param component The ui_document_component to load the document for
     * @return True if loading was successful, false otherwise
     */
    auto load_ui_document(entt::handle entity, ui_document_component& component, bool log_error = true) -> bool;



    // OS event input routing (implemented in ui_system_input.cpp)
    struct ui_pointer_dispatch_result
    {
        Rml::Context* target_context = nullptr;
    };

    auto find_keyboard_context(scene& scn) -> Rml::Context*;
    auto try_consume_keyboard_event(scene& scn, os::event& event) -> bool;
    auto dispatch_pointer_event(rtti::context& ctx, scene& scn, os::event& event) -> ui_pointer_dispatch_result;
    void apply_mouse_press_focus(scene& scn, os::event& event, Rml::Context* target_context, bool input_allowed);

    void blur_ui_context(Rml::Context* context) const;

    /// Receives a document and the pointer's position in its pixels; is_over is false for a world-space
    /// document the pointer misses.
    using pointer_visitor = std::function<void(Rml::Context* context, bool is_over, Rml::Vector2f position)>;
    /**
     * @brief Visits the debugger and each enabled document, the highest sort order first, with a pointer
     * position in work zone pixels mapped to the document: a screen-space overlay takes it as it is, a
     * world-space document through the scene camera onto its quad.
     */
    void visit_pointer_documents(scene& scn, input::coord position, const pointer_visitor& visit);
    /**
     * @brief Hands a finger to the documents as an RmlUi touch, which presses, drags scroll containers
     * and releases. A finger that lands on a document's content (is_ui_content_at) is the UI's: the
     * event is cleared for the game.
     */
    void dispatch_touch_event(rtti::context& ctx, scene& scn, os::event& event);
    /**
     * @brief The platform's mouse copy of a finger. The documents take the finger itself, so this only
     * clears a press that lands on a document's content, which keeps the game from acting on a tap on
     * the UI.
     */
    void consume_touch_mouse_press(rtti::context& ctx, scene& scn, os::event& event);
    /**
     * @brief Moves the mouse in every document to where the pointer is over it.
     * @return The documents with their content under the pointer (see process_mouse_move).
     */
    auto refresh_mouse_state(rtti::context& ctx, scene& scn) -> std::vector<Rml::Context*>;
    /**
     * @brief Moves the mouse in @p context to @p x, @p y (its pixels).
     * @return Whether the document's content is under it (is_ui_content_at), which then keeps the
     * pointer from the documents behind and presses from the game.
     */
    auto process_mouse_move(Rml::Context* context, int x, int y) -> bool;
    /**
     * @brief Hands @p event to @p context.
     * @param is_pointer_on_content Whether the document's content is under the pointer, from
     * refresh_mouse_state for this event.
     * @return Whether the event is the UI's: a mouse button or motion on the document's content.
     */
    auto process_event(Rml::Context* context, os::event& event, bool is_pointer_on_content) -> bool;

    void load_font(const std::string& path);

    /// Ensure framebuffer exists for document; create or resize if needed
    void ensure_document_framebuffer(ui_document_component& comp);
    private:

    /// Debug/legacy context (empty, used for RmlUi debugger host - menu, info, log)
    Rml::Context* debug_context_ = nullptr;
    /// The documents by sort order, kept to reuse their storage: for the keyboard or pointer event being dispatched,
    /// for visit_pointer_documents, and for render_screen_space.
    std::vector<entt::entity> event_documents_;
    std::vector<entt::entity> visited_documents_;
    std::vector<entt::entity> rendered_documents_;
    Rml::Context* debug_target_context_ = nullptr;
    std::shared_ptr<RmlUi_RenderLayerStack> debug_render_layer_stack_ = nullptr;
    /// The one layer stack the screen-space documents of a view draw into each frame (render_screen_space).
    std::shared_ptr<RmlUi_RenderLayerStack> screen_render_layer_stack_ = nullptr;


    std::shared_ptr<int> sentinel_ = std::make_shared<int>();
    std::set<std::string> fonts_loaded_;
};

} // namespace unravel
