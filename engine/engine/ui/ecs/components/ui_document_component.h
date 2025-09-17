#pragma once

#include <engine/ecs/components/basic_component.h>
#include <engine/assets/asset_handle.h>
#include <engine/ui/visual_tree.h>
#include <string>
#include <memory>

// Forward declarations
namespace Rml
{
    class ElementDocument;
}

namespace unravel
{

/**
 * @struct ui_document_component
 * @brief Component that holds a reference to a UI document for RmlUi rendering.
 * 
 * This component manages an RmlUi document within the shared UI context.
 * Each component instance holds its own document while sharing the global UI context.
 */
struct ui_document_component : public component_crtp<ui_document_component, owned_component>
{
    /// Path to the UI document file (HTML/RML)
    asset_handle<visual_tree> asset;

    
    /// Shared pointer to the loaded RmlUi document
    Rml::ElementDocument* document = nullptr;

    uint64_t version = 0;
    
    /// Whether the document should be shown automatically when loaded
    bool auto_show = true;
    
    
    /**
     * @brief Check if document is currently loaded
     * @return True if document is loaded and valid
     */
    [[nodiscard]] auto is_loaded() const -> bool;
    
    /**
     * @brief Check if document is currently visible
     * @return True if document is visible
     */
    [[nodiscard]] auto is_visible() const -> bool;
};

} // namespace unravel
