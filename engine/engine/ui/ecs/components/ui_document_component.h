#pragma once

#include <engine/ecs/components/basic_component.h>
#include <engine/assets/asset_handle.h>
#include <engine/ui/ui_tree.h>
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
    asset_handle<ui_tree> asset;

    
    /// Shared pointer to the loaded RmlUi document
    Rml::ElementDocument* document = nullptr;

    uint64_t version = 0;
    
    
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
