#include "ui_element_queries.h"

#include "ui_element_handles.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StyleTypes.h>

#include <unordered_map>

namespace unravel
{

void update_ui_layout_if_stale(Rml::ElementDocument& document)
{
    static std::unordered_map<ui_element_handle, uint64_t> updated_at_serial;
    auto& updated_at = updated_at_serial[get_ui_element_handle(&document)];
    const auto serial = get_ui_layout_serial();
    if(updated_at != serial)
    {
        document.UpdateDocument();
        updated_at = serial;
    }
}

auto get_ui_element_rect(Rml::Element& element, Rml::BoxArea area) -> math::vec4
{
    if(auto* document = element.GetOwnerDocument())
    {
        update_ui_layout_if_stale(*document);
    }
    const auto offset = element.GetAbsoluteOffset(area);
    const auto size = element.GetBox().GetSize(area);
    return {offset.x, offset.y, size.x, size.y};
}

namespace
{
auto is_inline_display(Rml::Style::Display display) -> bool
{
    return display == Rml::Style::Display::Inline || display == Rml::Style::Display::InlineBlock ||
           display == Rml::Style::Display::InlineFlex;
}
} // namespace

auto get_ui_element_text(const Rml::Element& element, const ui_text_options& options) -> std::string
{
    std::string text;
    for(int i = 0; i < element.GetNumChildren(); ++i)
    {
        auto* child = element.GetChild(i);
        if(auto* text_node = rmlui_dynamic_cast<Rml::ElementText*>(child))
        {
            text += text_node->GetText();
            continue;
        }
        const auto& computed = child->GetComputedValues();
        if(options.visible_only &&
           (computed.display() == Rml::Style::Display::None || computed.visibility() != Rml::Style::Visibility::Visible))
        {
            continue;
        }
        const bool is_block = !options.block_separator.empty() && !is_inline_display(computed.display());
        if(is_block)
        {
            text += options.block_separator;
        }
        text += get_ui_element_text(*child, options);
        if(is_block)
        {
            text += options.block_separator;
        }
    }
    return text;
}

auto is_ui_element_styled_visible(Rml::Element& element) -> bool
{
    // A value resolved from var() lives in storage the next GetProperty call reuses: read each one at once.
    const auto* display = element.GetProperty(Rml::PropertyId::Display);
    const bool is_displayed = !display || display->Get<int>() != static_cast<int>(Rml::Style::Display::None);
    const auto* visibility = element.GetProperty(Rml::PropertyId::Visibility);
    const bool is_visible = !visibility || visibility->Get<int>() == static_cast<int>(Rml::Style::Visibility::Visible);
    return is_displayed && is_visible;
}

auto get_ui_element_at(Rml::ElementDocument& document, const math::vec2& point) -> Rml::Element*
{
    auto* context = document.GetContext();
    if(!context)
    {
        return nullptr;
    }
    update_ui_layout_if_stale(document);
    auto* element = context->GetElementAtPoint({point.x, point.y});
    // The context's root, or an element of another document of the same context, is not in this document.
    return element && element->GetOwnerDocument() == &document ? element : nullptr;
}

} // namespace unravel
