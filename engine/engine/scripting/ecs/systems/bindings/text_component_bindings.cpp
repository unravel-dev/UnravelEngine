#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/text_component.h>

namespace unravel
{
namespace
{

//------------------------------
auto internal_m2n_text_get_text(entt::entity id) -> const std::string&
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_text();
    }

    static const std::string empty;
    return empty;
}

void internal_m2n_text_set_text(entt::entity id, const std::string& text)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_text(text);
    }
}

auto internal_m2n_text_get_buffer_type(entt::entity id) -> text_component::buffer_type
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_buffer_type();
    }

    return text_component::buffer_type::static_buffer;
}

void internal_m2n_text_set_buffer_type(entt::entity id, text_component::buffer_type type)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_buffer_type(type);
    }
}

auto internal_m2n_text_get_overflow_type(entt::entity id) -> text_component::overflow_type
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_overflow_type();
    }

    return text_component::overflow_type::word;
}

void internal_m2n_text_set_overflow_type(entt::entity id, text_component::overflow_type type)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_overflow_type(type);
    }
}

auto internal_m2n_text_get_font(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_font().uid();
    }

    return hpp::uuid{};
}

void internal_m2n_text_set_font(entt::entity id, hpp::uuid uid)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<font>(uid);
        comp->set_font(asset);
    }
}

auto internal_m2n_text_get_font_size(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_font_size();
    }

    return 0;
}

void internal_m2n_text_set_font_size(entt::entity id, uint32_t font_size)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_font_size(font_size);
    }
}

auto internal_m2n_text_get_render_font_size(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_render_font_size();
    }

    return 0;
}

auto internal_m2n_text_get_auto_size(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_auto_size();
    }

    return false;
}

void internal_m2n_text_set_auto_size(entt::entity id, bool auto_size)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_auto_size(auto_size);
    }
}
auto internal_m2n_text_get_auto_size_range(entt::entity id) -> urange32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_auto_size_range();
    }

    return {};
}

void internal_m2n_text_set_auto_size_range(entt::entity id, urange32_t range)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_auto_size_range(range);
    }
}

auto internal_m2n_text_get_area(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto area = comp->get_area();
        return {area.width, area.height};
    }

    return {};
}

void internal_m2n_text_set_area(entt::entity id, math::vec2 area)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_area({area.x, area.y});
    }
}

auto internal_m2n_text_get_render_area(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto area = comp->get_render_area();
        return {area.width, area.height};
    }

    return {};
}

auto internal_m2n_text_get_is_rich_text(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_is_rich_text();
    }

    return false;
}

void internal_m2n_text_set_is_rich_text(entt::entity id, bool rich)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_is_rich_text(rich);
    }
}

auto internal_m2n_text_get_alignment(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_alignment().flags;
    }

    return alignment{}.flags;
}

void internal_m2n_text_set_alignment(entt::entity id, uint32_t alignment_flags)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_alignment({alignment_flags});
    }
}

auto internal_m2n_text_get_bounds(entt::entity id) -> math::bbox
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_bounds();
    }

    return math::bbox::empty;
}

auto internal_m2n_text_get_render_bounds(entt::entity id) -> math::bbox
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_render_bounds();
    }

    return math::bbox::empty;
}

// ==== Text Style Functions ====

void internal_m2n_text_set_opacity(entt::entity id, float opacity)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_opacity(opacity);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_opacity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_opacity();
    }
    return 1.0f;
}

void internal_m2n_text_set_text_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_text_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_text_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_text_color();
    }
    return math::color::white();
}

void internal_m2n_text_set_background_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_background_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_background_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_background_color();
    }
    return math::color::transparent();
}

void internal_m2n_text_set_foreground_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_foreground_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_foreground_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_foreground_color();
    }
    return math::color::transparent();
}

void internal_m2n_text_set_overline_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_overline_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_overline_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_overline_color();
    }
    return math::color::white();
}

void internal_m2n_text_set_underline_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_underline_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_underline_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_underline_color();
    }
    return math::color::white();
}

void internal_m2n_text_set_strike_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_strike_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_strike_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_strike_color();
    }
    return math::color::white();
}

void internal_m2n_text_set_outline_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_outline_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_outline_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_outline_color();
    }
    return math::color::black();
}

void internal_m2n_text_set_outline_width(entt::entity id, float width)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.outline_width = width;
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_outline_width(entt::entity id) -> float
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().outline_width;
    }
    return 0.0f;
}

void internal_m2n_text_set_shadow_offsets(entt::entity id, math::vec2 offsets)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.shadow_offsets = offsets;
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_shadow_offsets(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().shadow_offsets;
    }
    return {0.0f, 0.0f};
}

void internal_m2n_text_set_shadow_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_shadow_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_shadow_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_shadow_color();
    }
    return math::color::black();
}

void internal_m2n_text_set_shadow_softener(entt::entity id, float softener)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.shadow_softener = softener;
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_shadow_softener(entt::entity id) -> float
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().shadow_softener;
    }
    return 1.0f;
}

void internal_m2n_text_set_style_flags(entt::entity id, uint32_t flags)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_style_flags({flags});
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_style_flags(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_style_flags().flags;
    }
    return gfx::style_normal;
}

} // namespace

void register_text_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.TextComponent");
    reg.add_internal_call("internal_m2n_text_get_text", dotnet_internal_call(internal_m2n_text_get_text));
    reg.add_internal_call("internal_m2n_text_set_text", dotnet_internal_call(internal_m2n_text_set_text));
    reg.add_internal_call("internal_m2n_text_get_buffer_type", dotnet_internal_call(internal_m2n_text_get_buffer_type));
    reg.add_internal_call("internal_m2n_text_set_buffer_type", dotnet_internal_call(internal_m2n_text_set_buffer_type));
    reg.add_internal_call("internal_m2n_text_get_overflow_type",
                            dotnet_internal_call(internal_m2n_text_get_overflow_type));
    reg.add_internal_call("internal_m2n_text_set_overflow_type",
                            dotnet_internal_call(internal_m2n_text_set_overflow_type));
    reg.add_internal_call("internal_m2n_text_get_font", dotnet_internal_call(internal_m2n_text_get_font));
    reg.add_internal_call("internal_m2n_text_set_font", dotnet_internal_call(internal_m2n_text_set_font));

    reg.add_internal_call("internal_m2n_text_get_font_size", dotnet_internal_call(internal_m2n_text_get_font_size));
    reg.add_internal_call("internal_m2n_text_set_font_size", dotnet_internal_call(internal_m2n_text_set_font_size));
    reg.add_internal_call("internal_m2n_text_get_render_font_size",
                            dotnet_internal_call(internal_m2n_text_get_render_font_size));

    reg.add_internal_call("internal_m2n_text_get_auto_size", dotnet_internal_call(internal_m2n_text_get_auto_size));
    reg.add_internal_call("internal_m2n_text_set_auto_size", dotnet_internal_call(internal_m2n_text_set_auto_size));

    reg.add_internal_call("internal_m2n_text_get_auto_size_range",
                            dotnet_internal_call(internal_m2n_text_get_auto_size_range));
    reg.add_internal_call("internal_m2n_text_set_auto_size_range",
                            dotnet_internal_call(internal_m2n_text_set_auto_size_range));

    reg.add_internal_call("internal_m2n_text_get_area", dotnet_internal_call(internal_m2n_text_get_area));
    reg.add_internal_call("internal_m2n_text_set_area", dotnet_internal_call(internal_m2n_text_set_area));
    reg.add_internal_call("internal_m2n_text_get_render_area", dotnet_internal_call(internal_m2n_text_get_render_area));

    reg.add_internal_call("internal_m2n_text_get_is_rich_text", dotnet_internal_call(internal_m2n_text_get_is_rich_text));
    reg.add_internal_call("internal_m2n_text_set_is_rich_text", dotnet_internal_call(internal_m2n_text_set_is_rich_text));

    reg.add_internal_call("internal_m2n_text_get_alignment", dotnet_internal_call(internal_m2n_text_get_alignment));
    reg.add_internal_call("internal_m2n_text_set_alignment", dotnet_internal_call(internal_m2n_text_set_alignment));

    reg.add_internal_call("internal_m2n_text_get_bounds", dotnet_internal_call(internal_m2n_text_get_bounds));
    reg.add_internal_call("internal_m2n_text_get_render_bounds", dotnet_internal_call(internal_m2n_text_get_render_bounds));

    // Text Style Functions
    reg.add_internal_call("internal_m2n_text_set_opacity", dotnet_internal_call(internal_m2n_text_set_opacity));
    reg.add_internal_call("internal_m2n_text_get_opacity", dotnet_internal_call(internal_m2n_text_get_opacity));
    reg.add_internal_call("internal_m2n_text_set_text_color", dotnet_internal_call(internal_m2n_text_set_text_color));
    reg.add_internal_call("internal_m2n_text_get_text_color", dotnet_internal_call(internal_m2n_text_get_text_color));
    reg.add_internal_call("internal_m2n_text_set_background_color", dotnet_internal_call(internal_m2n_text_set_background_color));
    reg.add_internal_call("internal_m2n_text_get_background_color", dotnet_internal_call(internal_m2n_text_get_background_color));
    reg.add_internal_call("internal_m2n_text_set_foreground_color", dotnet_internal_call(internal_m2n_text_set_foreground_color));
    reg.add_internal_call("internal_m2n_text_get_foreground_color", dotnet_internal_call(internal_m2n_text_get_foreground_color));
    reg.add_internal_call("internal_m2n_text_set_overline_color", dotnet_internal_call(internal_m2n_text_set_overline_color));
    reg.add_internal_call("internal_m2n_text_get_overline_color", dotnet_internal_call(internal_m2n_text_get_overline_color));
    reg.add_internal_call("internal_m2n_text_set_underline_color", dotnet_internal_call(internal_m2n_text_set_underline_color));
    reg.add_internal_call("internal_m2n_text_get_underline_color", dotnet_internal_call(internal_m2n_text_get_underline_color));
    reg.add_internal_call("internal_m2n_text_set_strike_color", dotnet_internal_call(internal_m2n_text_set_strike_color));
    reg.add_internal_call("internal_m2n_text_get_strike_color", dotnet_internal_call(internal_m2n_text_get_strike_color));
    reg.add_internal_call("internal_m2n_text_set_outline_color", dotnet_internal_call(internal_m2n_text_set_outline_color));
    reg.add_internal_call("internal_m2n_text_get_outline_color", dotnet_internal_call(internal_m2n_text_get_outline_color));
    reg.add_internal_call("internal_m2n_text_set_outline_width", dotnet_internal_call(internal_m2n_text_set_outline_width));
    reg.add_internal_call("internal_m2n_text_get_outline_width", dotnet_internal_call(internal_m2n_text_get_outline_width));
    reg.add_internal_call("internal_m2n_text_set_shadow_offsets", dotnet_internal_call(internal_m2n_text_set_shadow_offsets));
    reg.add_internal_call("internal_m2n_text_get_shadow_offsets", dotnet_internal_call(internal_m2n_text_get_shadow_offsets));
    reg.add_internal_call("internal_m2n_text_set_shadow_color", dotnet_internal_call(internal_m2n_text_set_shadow_color));
    reg.add_internal_call("internal_m2n_text_get_shadow_color", dotnet_internal_call(internal_m2n_text_get_shadow_color));
    reg.add_internal_call("internal_m2n_text_set_shadow_softener", dotnet_internal_call(internal_m2n_text_set_shadow_softener));
    reg.add_internal_call("internal_m2n_text_get_shadow_softener", dotnet_internal_call(internal_m2n_text_get_shadow_softener));
    reg.add_internal_call("internal_m2n_text_set_style_flags", dotnet_internal_call(internal_m2n_text_set_style_flags));
    reg.add_internal_call("internal_m2n_text_get_style_flags", dotnet_internal_call(internal_m2n_text_get_style_flags));
}

} // namespace unravel
