#pragma once

#include "base/basetypes.hpp"
#include <engine/ecs/components/basic_component.h>
#include <engine/assets/asset_handle.h>
#include <engine/ui/ui_tree.h>
#include <graphics/frame_buffer.h>
#include <math/math.h>
#include <string>
#include <memory>

#include "../../rmlui/RmlUi_RenderLayerStack.h"

// Forward declarations
namespace Rml
{
    class Context;
    class ElementDocument;
}

namespace unravel
{

/// Default pixels per world unit: 100 px = 1 world unit.
constexpr float UI_DEFAULT_PIXELS_PER_WORLD_UNIT = 100.0f;

/// Render mode for the UI document.
enum class ui_render_mode
{
    screen_space_overlay,  ///< Screen-space overlay (viewport-sized or positioned)
    world_space           ///< World-space (rendered on 3D quad, uses transform)
};

/**
 * @struct ui_document_component
 * @brief Component that holds a reference to a UI document for RmlUi rendering.
 *
 * Supports both screen-space and world-space rendering via context-per-document
 * and render-to-texture. Each document has its own RmlUi context and framebuffer.
 */
struct ui_document_component : public component_crtp<ui_document_component, owned_component>
{
    /// Path to the UI document file (HTML/RML)
    asset_handle<ui_tree> asset;

    /// Per-document RmlUi context (owned by this component)
    Rml::Context* context = nullptr;

    /// Unique to the current context: script bindings into its data models are made again when it changes.
    uint64_t context_generation = 0;

    /// Loaded RmlUi document (owned by context)
    Rml::ElementDocument* document = nullptr;

    uint64_t version = 0;

    /// Set when a script closed the document: it is not loaded again until a script reloads it.
    bool closed = false;

    /// Rendering mode: screen or world
    ui_render_mode render_mode = ui_render_mode::screen_space_overlay;

    /// Document resolution (pixels). For screen-space: viewport size. For world-space: configurable (e.g. 512×512)
    usize32_t size = {512, 512};
    /// Pixels per world unit for world-space rendering (default: 100)
    float pixels_per_world_unit = UI_DEFAULT_PIXELS_PER_WORLD_UNIT;

    /// Framebuffer for world-space render-to-texture (owned by component)
    gfx::frame_buffer::ptr framebuffer;

    /// Set when the document drew something only a render target of its own can draw (a clip mask or a
    /// backdrop filter): a world-space document then renders to its framebuffer instead of straight
    /// into the scene, until it loads again.
    bool needs_render_target = false;

    /// Last frame this world-space document was rendered (for render-once-per-frame)
    uint32_t last_render_frame = 0;

    /// Render layer stack for this document (owned by component, used during render pass)
    std::shared_ptr<RmlUi_RenderLayerStack> render_layer_stack;

    auto get_world_space_scale() const -> math::vec2;

    /**
     * @brief The world transform of the quad a world-space document is drawn on: @p entity_world
     * scaled to the document's size in world units. The quad spans -0.5..0.5 in its local x and y,
     * with document pixel (0, 0) at local (-0.5, 0.5).
     */
    auto get_world_space_quad(const math::transform& entity_world) const -> math::transform;

    /**
     * @brief Maps document pixels (x right, y down, z toward the reader) to world space on the quad of
     * get_world_space_quad.
     */
    auto get_document_to_world(const math::transform& entity_world) const -> math::mat4;

    /**
     * @brief Whether the scene's UI pass draws this world-space document straight into its target,
     * with no framebuffer of its own.
     */
    [[nodiscard]] auto draws_into_scene() const -> bool;

    /**
     * @brief The quad a world-space document is drawn on, in the entity's local space: centred on the
     * entity, the document's size in world units in x and y, flat in z.
     */
    auto get_bounds() const -> math::bbox;
    /**
     * @brief Check if document is currently loaded
     * @return True if document is loaded and valid
     */
    [[nodiscard]] auto is_loaded() const -> bool;
    
    /**
     * @brief Check if document is currently enabled
     * @return True if document is enabled
     */
    [[nodiscard]] auto is_enabled() const -> bool;


    /**
     * @brief Set the enabled state of the document
     * @param enabled True if document should be enabled
     */
    void set_enabled(bool enabled);

private:
    
    bool enabled_ = true;

};

} // namespace unravel
