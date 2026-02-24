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

/// Default pixels per world unit (Unity-style). 100 px = 1 world unit.
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

    /// Loaded RmlUi document (owned by context)
    Rml::ElementDocument* document = nullptr;

    uint64_t version = 0;

    /// Rendering mode: screen or world
    ui_render_mode render_mode = ui_render_mode::screen_space_overlay;

    /// Document resolution (pixels). For screen-space: viewport size. For world-space: configurable (e.g. 512×512)
    usize32_t size = {512, 512};
    /// Pixels per world unit for world-space rendering (default: 100, Unity-style)
    float pixels_per_world_unit = UI_DEFAULT_PIXELS_PER_WORLD_UNIT;

    /// Framebuffer for world-space render-to-texture (owned by component)
    gfx::frame_buffer::ptr framebuffer;

    /// Last frame this world-space document was rendered (for render-once-per-frame)
    uint32_t last_render_frame = 0;

    /// Render layer stack for this document (owned by component, used during render pass)
    std::shared_ptr<RmlUi_RenderLayerStack> render_layer_stack;

    auto get_world_space_scale() const -> math::vec2;

        /**
     * @brief Gets the bounding box of the document
     * @return The bounding box in local space
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

public:
    /// Flag to indicate that stylesheet reload is needed on next update
    bool needs_stylesheet_reload = false;

};

} // namespace unravel
