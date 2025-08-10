#include "text_component.hpp"
#include "serialization/serialization.h"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(text_component)
{
    rttr::registration::class_<text_style>("text_style")(rttr::metadata("pretty_name", "TextStyle"))
        .constructor<>()()

        .property("opacity", &text_style::get_opacity, &text_style::set_opacity)(
            rttr::metadata("pretty_name", "Opacity"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f))
        .property("text_color",
                  &text_style::get_text_color,
                  &text_style::set_text_color)(rttr::metadata("pretty_name", "Color"), rttr::metadata("tooltip", "."))

        .property("outline_color", &text_style::get_outline_color, &text_style::set_outline_color)(
            rttr::metadata("pretty_name", "Outline Color"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("group", "Outline"))

        .property("outline_width", &text_style::outline_width)(rttr::metadata("pretty_name", "Outline Width"),
                                                               rttr::metadata("tooltip", "."),
                                                               rttr::metadata("min", 0.0f),
                                                               rttr::metadata("step", 0.01f),
                                                               rttr::metadata("group", "Outline"))

        .property("shadow_color", &text_style::get_shadow_color, &text_style::set_shadow_color)(
            rttr::metadata("pretty_name", "Shadow Color"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("group", "Shadow"))

        .property("shadow_softener", &text_style::shadow_softener)(rttr::metadata("pretty_name", "Shadow Softenss"),
                                                                   rttr::metadata("tooltip", "."),
                                                                   rttr::metadata("min", 0.0f),
                                                                   rttr::metadata("max", 10.0f),
                                                                   rttr::metadata("group", "Shadow"))

        .property("shadow_offsets", &text_style::shadow_offsets)(rttr::metadata("pretty_name", "Shadow Offsets"),
                                                                 rttr::metadata("tooltip", "."),
                                                                 rttr::metadata("group", "Shadow"))

        .property("style_flags", &text_style::get_style_flags, &text_style::set_style_flags)(
            rttr::metadata("pretty_name", "Flags"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("group", "Style"))

        .property("background_color", &text_style::get_background_color, &text_style::set_background_color)(
            rttr::metadata("pretty_name", "Background Color"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("group", "Style"))
        .property("foreground_color", &text_style::get_foreground_color, &text_style::set_foreground_color)(
            rttr::metadata("pretty_name", "Foreground Color"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("group", "Style"))

        .property("overline_color", &text_style::get_overline_color, &text_style::set_overline_color)(
            rttr::metadata("pretty_name", "Overline Color"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("group", "Style"))

        .property("underline_color", &text_style::get_underline_color, &text_style::set_underline_color)(
            rttr::metadata("pretty_name", "Underline Color"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("group", "Style"))

        .property("strike_color", &text_style::get_strike_color, &text_style::set_strike_color)(
            rttr::metadata("pretty_name", "Strike Color"),
            rttr::metadata("tooltip", "."),
            rttr::metadata("group", "Style"));

    using BT = text_component::buffer_type;
    using OT = text_component::overflow_type;

    rttr::registration::enumeration<BT>("buffer_type")(rttr::value("Static", BT::static_buffer),
                                                       rttr::value("Dynamic", BT::dynamic_buffer),
                                                       rttr::value("Transient", BT::transient_buffer));

    rttr::registration::enumeration<OT>("overflow_type")(rttr::value("None", OT::none),
                                                         rttr::value("Word", OT::word),
                                                         rttr::value("Grapheme", OT::grapheme));

    // predicates for conditional GUI
    auto auto_size_pred = rttr::property_predicate(
        [](rttr::instance& i)
        {
            return i.try_convert<text_component>()->get_auto_size();
        });

    auto font_size_read_only = rttr::property_predicate(
        [](rttr::instance& i)
        {
            return i.try_convert<text_component>()->get_auto_size();
        });

    rttr::registration::class_<text_component>("text_component")(rttr::metadata("category", "UI"),
                                                                 rttr::metadata("pretty_name", "Text"))
        .constructor<>()()
        .method("component_exists", &component_exists<text_component>)

        .property("text", &text_component::get_text, &text_component::set_text)(
            rttr::metadata("pretty_name", "Text"),
            rttr::metadata("tooltip", "The UTF-8 string to display."),
            rttr::metadata("multiline", true),
            rttr::metadata("wrap", true),
            rttr::metadata(
                "example",
                R"(<color=blue>Blue text with <background-color=yellow>yellow background</background-color> and <style=underline>underlined</style> <alpha=0.4>transparent words</alpha>.</color>
<outline-width=1><outline-color=red>This text has a red outline</outline-color> and <shadow-offset=2,2><shadow-color=gray>gray shadow</shadow-color></shadow-offset>.</outline-width>

<color=green>Green text with <style=overline>overlined</style> and <style=strikethrough>strikethrough</style> styles.</color>
<shadow-offset=3,3><shadow-color=black><shadow-softener=2>This text has a softened shadow</shadow-softener> and <foreground-color=#FFD70055><color=black>black text with gold transparent foreground</color></foreground-color>.</shadow-color></shadow-offset>

<color=purple>Purple text with <style=underline|overline>both underline and overline</style> effects.</color>)"))

        .property("is_rich", &text_component::get_is_rich_text, &text_component::set_is_rich_text)(
            rttr::metadata("pretty_name", "Rich Text"),
            rttr::metadata("tooltip", "Enable parsing of <color> / <style> tags."))

        .property("font", &text_component::get_font, &text_component::set_font)(
            rttr::metadata("pretty_name", "Font"),
            rttr::metadata("tooltip", "The font asset to use."))

        .property("font_size", &text_component::get_font_size, &text_component::set_font_size)(
            rttr::metadata("pretty_name", "Font Size"),
            rttr::metadata("tooltip", "Desired base font size."),
            rttr::metadata("readonly_predicate", font_size_read_only))

        .property_readonly("render_font_size", &text_component::get_render_font_size)(
            rttr::metadata("pretty_name", "Render Font Size"),
            rttr::metadata("tooltip", "Actual size used after auto-scaling."))

        .property_readonly("render_buffers_count", &text_component::get_render_buffers_count)(
            rttr::metadata("pretty_name", "Render Buffers"),
            rttr::metadata("tooltip", "How many render buffers are used for this text."))

        .property("auto_size", &text_component::get_auto_size, &text_component::set_auto_size)(
            rttr::metadata("pretty_name", "Auto Size"),
            rttr::metadata("tooltip", "Automatically shrink or grow font to fit area."))

        .property("auto_size_range", &text_component::get_auto_size_range, &text_component::set_auto_size_range)(
            rttr::metadata("pretty_name", "Auto Size Range"),
            rttr::metadata("tooltip", "Min/Max font sizes when Auto Size is enabled."),
            rttr::metadata("predicate", auto_size_pred))

        .property("alignment", &text_component::get_alignment, &text_component::set_alignment)(
            rttr::metadata("pretty_name", "Alignment"),
            rttr::metadata("tooltip", "Horizontal and vertical alignment flags."))

        .property("apply_kerning", &text_component::get_apply_kerning, &text_component::set_apply_kerning)(
            rttr::metadata("pretty_name", "Apply Kerning"),
            rttr::metadata("tooltip", "Enable kerning."))

        .property("overflow", &text_component::get_overflow_type, &text_component::set_overflow_type)(
            rttr::metadata("pretty_name", "Overflow"),
            rttr::metadata("tooltip", "How text should wrap or overflow the area."))

        .property("buffer_type", &text_component::get_buffer_type, &text_component::set_buffer_type)(
            rttr::metadata("pretty_name", "Buffer Type"),
            rttr::metadata("tooltip", "Static, Dynamic, or Transient text buffer storage."))

        .property("area", &text_component::get_area, &text_component::set_area)(
            rttr::metadata("pretty_name", "Area"),
            rttr::metadata("tooltip", "Bounds (width × height)."))

        .property("style", &text_component::get_style, &text_component::set_style)(
            rttr::metadata("pretty_name", "Style"),
            rttr::metadata("tooltip", "Main style for the text"));
}

SERIALIZE_INLINE(text_style)
{
    try_serialize(ar, ser20::make_nvp("opacity", obj.opacity));
    try_serialize(ar, ser20::make_nvp("text_color", obj.text_color));
    try_serialize(ar, ser20::make_nvp("background_color", obj.background_color));
    try_serialize(ar, ser20::make_nvp("foreground_color", obj.foreground_color));
    try_serialize(ar, ser20::make_nvp("overline_color", obj.overline_color));
    try_serialize(ar, ser20::make_nvp("underline_color", obj.underline_color));
    try_serialize(ar, ser20::make_nvp("strike_color", obj.strike_color));
    try_serialize(ar, ser20::make_nvp("outline_color", obj.outline_color));
    try_serialize(ar, ser20::make_nvp("shadow_color", obj.shadow_color));
    try_serialize(ar, ser20::make_nvp("shadow_softener", obj.shadow_softener));
    try_serialize(ar, ser20::make_nvp("shadow_offsets", obj.shadow_offsets));
    try_serialize(ar, ser20::make_nvp("style_flags", obj.style_flags));
}

SAVE(text_component)
{
    try_save(ar, ser20::make_nvp("text", obj.get_text()));
    try_save(ar, ser20::make_nvp("is_rich", obj.get_is_rich_text()));
    try_save(ar, ser20::make_nvp("font", obj.get_font()));
    try_save(ar, ser20::make_nvp("font_size", obj.get_font_size()));
    try_save(ar, ser20::make_nvp("auto_size", obj.get_auto_size()));
    try_save(ar, ser20::make_nvp("auto_size_range", obj.get_auto_size_range()));
    try_save(ar, ser20::make_nvp("alignment", obj.get_alignment().flags));
    try_save(ar, ser20::make_nvp("overflow", obj.get_overflow_type()));
    try_save(ar, ser20::make_nvp("area", obj.get_area()));
    try_save(ar, ser20::make_nvp("buffer_type", obj.get_buffer_type()));
    try_save(ar, ser20::make_nvp("style", obj.get_style()));
}
SAVE_INSTANTIATE(text_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(text_component, ser20::oarchive_binary_t);

LOAD(text_component)
{
    std::string text;
    if(try_load(ar, ser20::make_nvp("text", text)))
    {
        obj.set_text(text);
    }

    bool is_rich = false;
    if(try_load(ar, ser20::make_nvp("is_rich", is_rich)))
    {
        obj.set_is_rich_text(is_rich);
    }

    asset_handle<font> font_h;
    if(try_load(ar, ser20::make_nvp("font", font_h)))
    {
        obj.set_font(font_h);
    }

    int font_sz = 0;
    if(try_load(ar, ser20::make_nvp("font_size", font_sz)))
    {
        obj.set_font_size(font_sz);
    }

    bool auto_sz = false;
    if(try_load(ar, ser20::make_nvp("auto_size", auto_sz)))
    {
        obj.set_auto_size(auto_sz);
    }

    urange32_t auto_range;
    if(try_load(ar, ser20::make_nvp("auto_size_range", auto_range)))
    {
        obj.set_auto_size_range(auto_range);
    }

    uint32_t align = 0;
    if(try_load(ar, ser20::make_nvp("alignment", align)))
    {
        obj.set_alignment({align});
    }

    text_component::overflow_type ov;
    if(try_load(ar, ser20::make_nvp("overflow", ov)))
    {
        obj.set_overflow_type(ov);
    }

    fsize_t area{};
    if(try_load(ar, ser20::make_nvp("area", area)))
    {
        obj.set_area(area);
    }

    text_component::buffer_type bt;
    if(try_load(ar, ser20::make_nvp("buffer_type", bt)))
    {
        obj.set_buffer_type(bt);
    }

    text_style style;
    if(try_load(ar, ser20::make_nvp("style", style)))
    {
        obj.set_style(style);
    }
}

LOAD_INSTANTIATE(text_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(text_component, ser20::iarchive_binary_t);
} // namespace unravel
