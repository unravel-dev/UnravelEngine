#pragma once

#include <context/context.hpp>
#include <imgui/imgui.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace unravel
{

/// A page of a settings window.
struct settings_category
{
    std::string name;
    const char* icon{};
    /// One line under the title of the page.
    std::string description;
    /// Words the search finds the page by, besides its name.
    std::string keywords;
    std::function<void(rtti::context&)> draw;
};

/**
 * @brief The body of a settings window: a sidebar with a search field over the categories, and
 * beside it the page of the selected category under its title and description.
 *
 * Sizes are in units of the font size, so the view follows the UI scale.
 */
class settings_view
{
public:
    explicit settings_view(std::vector<settings_category> categories);

    /// Shows the category with this name. Any other name leaves the selection as it is.
    void select(const std::string& name);
    void draw(rtti::context& ctx);

private:
    void draw_sidebar();
    void draw_category_row(std::size_t index);
    void draw_page(rtti::context& ctx);
    auto is_match(const settings_category& category) const -> bool;

    std::vector<settings_category> categories_;
    std::size_t selected_{};
    ImGuiTextFilter filter_;
};

} // namespace unravel
