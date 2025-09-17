#pragma once
#include <engine/engine_export.h>

#include <memory>
#include <string>

// Forward declarations
namespace Rml
{
    class StyleSheet;
    class StyleSheetContainer;
}

namespace unravel
{

/**
 * @struct style_sheet
 * @brief Represents a UI style sheet asset (CSS/RCSS document).
 * 
 * This asset contains styling information for UI elements,
 * similar to Unity's USS files. It defines the visual appearance
 * and layout properties of UI elements.
 */
struct style_sheet
{
    using sptr = std::shared_ptr<style_sheet>; ///< Shared pointer to a style sheet.
    using wptr = std::weak_ptr<style_sheet>;   ///< Weak pointer to a style sheet.
    using uptr = std::unique_ptr<style_sheet>; ///< Unique pointer to a style sheet.

    /// The CSS/RCSS content of the style sheet
    std::string content;
    
    /**
     * @brief Check if the style sheet content is valid/non-empty
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
