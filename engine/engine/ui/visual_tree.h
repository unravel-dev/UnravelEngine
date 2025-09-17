#pragma once
#include <engine/engine_export.h>

#include <memory>
#include <string>

// Forward declarations
namespace Rml
{
    class ElementDocument;
}

namespace unravel
{

/**
 * @struct visual_tree
 * @brief Represents a UI visual tree asset (HTML/RML document).
 * 
 * This asset contains the structure and content of a UI document,
 * similar to Unity's UXML files. It defines the hierarchy and
 * properties of UI elements.
 */
struct visual_tree
{
    using sptr = std::shared_ptr<visual_tree>; ///< Shared pointer to a visual tree.
    using wptr = std::weak_ptr<visual_tree>;   ///< Weak pointer to a visual tree.
    using uptr = std::unique_ptr<visual_tree>; ///< Unique pointer to a visual tree.

    /// The HTML/RML content of the visual tree
    std::string content;
    
    /**
     * @brief Check if the visual tree content is valid/non-empty
     * @return True if content is not empty
     */
    [[nodiscard]] auto is_valid() const -> bool;
    
    /**
     * @brief Get the size of the content in bytes
     * @return Size of content string
     */
    [[nodiscard]] auto get_content_size() const -> size_t;
};

} // namespace unravel
