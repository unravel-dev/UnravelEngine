#pragma once

#include <context/context.hpp>
#include <imgui/imgui.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace unravel
{

/// How a property relates to a settings profile's overrides.
enum class override_mark : std::uint8_t
{
    none,
    /// A group holding overridden properties.
    contains,
    /// Overridden, or inside an overridden value.
    overridden,
};

/// The mark of `path` in `overrides`, anything with is_overridden and has_overrides_within (profile_overrides).
template<typename Overrides>
auto get_override_mark(const Overrides& overrides, const std::string& path) -> override_mark
{
    if(overrides.is_overridden(path))
    {
        return override_mark::overridden;
    }
    return overrides.has_overrides_within(path) ? override_mark::contains : override_mark::none;
}

/// How marked properties look.
enum class override_style : std::uint8_t
{
    /// The edited profile's overrides: bold, as a prefab instance's overrides are.
    owned,
    /// Another profile's overrides hiding what is edited (the previewed or the deploy profile): in the color of
    /// the header's preview choice, regular weight.
    shadowing,
};

/// What a property_override_scope marks, and how.
struct property_override_marks
{
    /// The mark of a property, by its path in the settings document.
    std::function<override_mark(const std::string& path)> get_mark;
    /// Stops overriding a path; empty: the context menu offers no revert.
    std::function<void(const std::string& path)> revert;
    /// The revert item's text, such as "Revert to Project Settings".
    std::string revert_label;
    override_style style{override_style::owned};
    /// The color of an overridden property's mark in the row gutter; none (alpha 0) takes the style's color.
    ImVec4 marker_color{};
    /// The tooltip line of an overridden property, such as "Profile Android overrides this setting."
    std::string overridden_note;
    /// The tooltip line of a group holding overridden properties.
    std::string contains_note;
    /// Whether a property is one every profile shares: drawn read-only. Empty: none is.
    std::function<bool(const std::string& path)> is_shared;
    /// The tooltip line of a shared property.
    std::string shared_note;
};

/**
 * @brief Marks the properties a settings profile overrides while an object is inspected, says so in their
 * tooltips, and offers reverting one from the property's context menu.
 *
 * Active only inside a property_override_scope. The inspector pushes each property's name as it draws it,
 * so the view knows the path of the property being drawn ("graphics/particles/spawn_scale", from the root
 * the scope names): the serialization path the profile's overrides use. Each property gets its own mark: a
 * group holding overrides does not mark the properties in it that are not overridden.
 */
class property_override_view
{
public:
    auto is_active() const -> bool;
    /// Called by the inspector around each property it draws; draws a marked one in the scope's style.
    void push_segment(const std::string& segment);
    void pop_segment();
    /// The mark of the property being drawn.
    auto get_current_mark() const -> override_mark;
    /// The context menu's revert item for the property being drawn; empty when it has none.
    auto get_revert_label() const -> const std::string&;
    /// Reverts the property being drawn.
    void revert_current() const;
    /// The line the property's tooltip adds about its overrides; empty when it has none.
    auto get_tooltip_note() const -> const std::string&;
    /// The color the row gutter marks an overridden property with.
    auto get_marker_color() const -> ImVec4;
    /// Whether the property being drawn is shared by every profile, so it cannot be edited here.
    auto is_current_shared() const -> bool;

private:
    friend class property_override_scope;
    friend class property_override_plain_scope;

    /// Pushes the font and colors of `mark` in the scope's style, or the unmarked ones for none.
    void push_style(override_mark mark);

    property_override_marks marks_;
    std::string path_;
    /// Per pushed segment: where the path ended before it, its mark, and whether it pushed a style.
    std::vector<std::size_t> path_ends_;
    std::vector<override_mark> segment_marks_;
    std::vector<bool> is_styled_;
    /// The style unmarked properties are drawn in, taken when the scope begins.
    ImFont* plain_font_{};
    ImVec4 plain_text_color_{};
    ImVec4 plain_check_color_{};
};

/// While alive, text is drawn as unmarked properties are: a marked property's context menu and tooltip use it,
/// so they do not take the property's font and colors.
class property_override_plain_scope
{
public:
    explicit property_override_plain_scope(const property_override_view& view);
    ~property_override_plain_scope();

    property_override_plain_scope(const property_override_plain_scope&) = delete;
    auto operator=(const property_override_plain_scope&) -> property_override_plain_scope& = delete;

private:
    bool is_pushed_{};
};

/// While alive, the inspections drawn in it are marked as `marks` says.
class property_override_scope
{
public:
    /// @param root The path of the inspected object within its document, such as "graphics".
    property_override_scope(rtti::context& ctx, const std::string& root, property_override_marks marks);
    ~property_override_scope();

    property_override_scope(const property_override_scope&) = delete;
    auto operator=(const property_override_scope&) -> property_override_scope& = delete;

private:
    property_override_view& view_;
    property_override_view previous_;
};

} // namespace unravel
