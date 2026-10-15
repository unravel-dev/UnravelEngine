#include "mcp_meta_properties.h"

#include "mcp_tools_common.h"
#include "mcp_asset_lookup.h"
#include "mcp_project_paths.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/prefab.h>
#include <engine/layers/layer_mask.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/rendering/font.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/settings/settings.h>
#include <engine/ui/ui_tree.h>
#include <base/basetypes.hpp>
#include <graphics/texture.h>
#include <hpp/string_view.hpp>
#include <math/bbox.h>
#include <math/color.h>
#include <math/curve.h>
#include <reflection/reflection.h>
#include <uuid/uuid.h>

#include <array>
#include <cctype>
#include <chrono>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_map>
#include <variant>

namespace unravel::mcp
{
namespace
{

/// How one value type crosses between a meta_any and JSON. Types without a codec are enums
/// (handled from their reflected values), nested reflected structs (recursed into), or not
/// expressible here.
struct value_codec
{
    const char* schema_type{};
    /// Extra schema fragment such as an "items" description.
    const char* schema_extra{};
    /// How a writable value is spelled in JSON, appended to the reflection's tooltip.
    const char* format_hint{};
    std::function<std::optional<std::string>(rtti::context&, const entt::meta_any&)> to_json;
    /// On entry the meta_any holds the property's current value (empty when there is none), for a codec that
    /// merges what it reads into it.
    std::function<bool(rtti::context&, const simdjson::dom::element&, entt::meta_any&, std::string&)> from_json;
    /// For numbers: the value as a double, so min / max attributes can be checked.
    std::function<double(const entt::meta_any&)> as_double;
};

template<typename T>
auto make_number_codec(const char* schema_type) -> value_codec
{
    return {.schema_type = schema_type,
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                if constexpr(std::is_floating_point_v<T>)
                {
                    return fmt::format("{}", value.cast<T>());
                }
                else
                {
                    return fmt::format("{}", value.cast<T>());
                }
            },
            .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                double number = 0.0;
                int64_t integer = 0;
                if(element.get_double().get(number) != simdjson::SUCCESS)
                {
                    if(element.get_int64().get(integer) != simdjson::SUCCESS)
                    {
                        error = "expected a number";
                        return false;
                    }
                    number = static_cast<double>(integer);
                }
                if constexpr(!std::is_floating_point_v<T>)
                {
                    if(number != static_cast<double>(static_cast<int64_t>(number)))
                    {
                        error = "expected an integer";
                        return false;
                    }
                    // Out of the type's range a cast would wrap: 300 into a byte stores 44.
                    const auto lowest = static_cast<double>(std::numeric_limits<T>::lowest());
                    const auto highest = static_cast<double>(std::numeric_limits<T>::max());
                    if(number < lowest || number > highest)
                    {
                        error = fmt::format("expected an integer from {} to {}", std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max());
                        return false;
                    }
                }
                out = entt::meta_any{static_cast<T>(number)};
                return true;
            },
            .as_double = [](const entt::meta_any& value) -> double
            {
                return static_cast<double>(value.cast<T>());
            }};
}

/// A range crosses as [min, max].
template<typename T>
auto make_range_codec() -> value_codec
{
    constexpr bool is_float = std::is_floating_point_v<T>;
    return {.schema_type = "array",
            .schema_extra = is_float ? R"("items":{"type":"number"},"minItems":2,"maxItems":2)"
                                     : R"("items":{"type":"integer"},"minItems":2,"maxItems":2)",
            .format_hint = "[min, max]",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto& bounds = value.cast<const range<T>&>();
                if constexpr(is_float)
                {
                    return fmt::format("[{},{}]", bounds.min, bounds.max);
                }
                else
                {
                    return fmt::format("[{},{}]", bounds.min, bounds.max);
                }
            },
            .from_json = [](rtti::context& ctx, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                simdjson::dom::array items;
                if(element.get_array().get(items) != simdjson::SUCCESS || items.size() != 2)
                {
                    error = "expected [min, max]";
                    return false;
                }
                static const auto element_codec = make_number_codec<T>(is_float ? "number" : "integer");
                std::array<T, 2> values{};
                std::size_t index = 0;
                for(auto item : items)
                {
                    entt::meta_any converted;
                    if(!element_codec.from_json(ctx, item, converted, error))
                    {
                        return false;
                    }
                    values[index++] = converted.cast<T>();
                }
                out = entt::meta_any{range<T>(values[0], values[1])};
                return true;
            }};
}

/// A size crosses as [width, height], each element checked as the number codec checks it.
template<typename T>
auto make_size_codec() -> value_codec
{
    constexpr bool is_float = std::is_floating_point_v<T>;
    return {.schema_type = "array",
            .schema_extra = is_float ? R"("items":{"type":"number"},"minItems":2,"maxItems":2)"
                                     : R"("items":{"type":"integer"},"minItems":2,"maxItems":2)",
            .format_hint = "[width, height]",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto& extent = value.cast<const size<T>&>();
                if constexpr(is_float)
                {
                    return fmt::format("[{},{}]", extent.width, extent.height);
                }
                else
                {
                    return fmt::format("[{},{}]", extent.width, extent.height);
                }
            },
            .from_json = [](rtti::context& ctx, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                simdjson::dom::array items;
                if(element.get_array().get(items) != simdjson::SUCCESS || items.size() != 2)
                {
                    error = "expected [width, height]";
                    return false;
                }
                static const auto element_codec = make_number_codec<T>(is_float ? "number" : "integer");
                std::array<T, 2> values{};
                std::size_t index = 0;
                for(auto item : items)
                {
                    entt::meta_any converted;
                    if(!element_codec.from_json(ctx, item, converted, error))
                    {
                        return false;
                    }
                    values[index++] = converted.cast<T>();
                }
                out = entt::meta_any{size<T>(values[0], values[1])};
                return true;
            }};
}

/// A color as [r, g, b, a], each number the shortest text that reads back as the same value. The bridge spells
/// every number that way, so writing back what a read gave changes nothing (and makes no prefab override).
auto exact_color_json(const math::color& color) -> std::string
{
    const math::vec4 v = color;
    return fmt::format("[{},{},{},{}]", v.x, v.y, v.z, v.w);
}

/// How one key of a curve of T spells its value: the fields after "progress".
template<typename T>
struct curve_key_json;

template<>
struct curve_key_json<float>
{
    static constexpr const char* format = "[{progress, value}]";

    static auto to_fields(float value) -> std::string
    {
        return fmt::format(R"("value":{})", value);
    }

    static auto from_fields(const simdjson::dom::object& key, float& out, std::string& /*error*/) -> bool
    {
        double value{};
        if(key["value"].get_double().get(value) != simdjson::SUCCESS)
        {
            return false;
        }
        out = static_cast<float>(value);
        return true;
    }
};

template<>
struct curve_key_json<frange_t>
{
    static constexpr const char* format = "[{progress, min, max}]";

    static auto to_fields(const frange_t& value) -> std::string
    {
        return fmt::format(R"("min":{},"max":{})", value.min, value.max);
    }

    static auto from_fields(const simdjson::dom::object& key, frange_t& out, std::string& /*error*/) -> bool
    {
        double low{};
        double high{};
        if(key["min"].get_double().get(low) != simdjson::SUCCESS || key["max"].get_double().get(high) != simdjson::SUCCESS)
        {
            return false;
        }
        out = frange_t(static_cast<float>(low), static_cast<float>(high));
        return true;
    }
};

template<>
struct curve_key_json<math::color>
{
    static constexpr const char* format = "[{progress, color: [r, g, b, a]}]";

    static auto to_fields(const math::color& value) -> std::string
    {
        return R"("color":)" + exact_color_json(value);
    }

    static auto from_fields(const simdjson::dom::object& key, math::color& out, std::string& error) -> bool
    {
        simdjson::dom::element color;
        return key["color"].get(color) == simdjson::SUCCESS && parse_color(color, out, error);
    }
};

template<>
struct curve_key_json<math::vec3>
{
    static constexpr const char* format = "[{progress, value: [x, y, z]}]";

    static auto to_fields(const math::vec3& value) -> std::string
    {
        return R"("value":)" + fmt::format("[{},{},{}]", value.x, value.y, value.z);
    }

    static auto from_fields(const simdjson::dom::object& key, math::vec3& out, std::string& error) -> bool
    {
        simdjson::dom::element value;
        return key["value"].get(value) == simdjson::SUCCESS && parse_vec3(value, out, error);
    }
};

/// A curve crosses as its keys, sorted by progress (usually 0..1). A write keeps the interpolation and tangents of
/// the keys it leaves where they were, so writing back what a read gave changes nothing.
template<typename T>
auto make_curve_codec() -> value_codec
{
    return {.schema_type = "array",
            .format_hint = curve_key_json<T>::format,
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                std::string json = "[";
                for(const auto& point : value.cast<const math::curve<T>&>().get_points())
                {
                    json += fmt::format(R"({}{{"progress":{},{}}})",
                                        json.size() > 1 ? "," : "",
                                        point.progress,
                                        curve_key_json<T>::to_fields(point.element));
                }
                return json + "]";
            },
            .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                const std::string expected = std::string("expected ") + curve_key_json<T>::format;
                simdjson::dom::array items;
                if(element.get_array().get(items) != simdjson::SUCCESS)
                {
                    error = expected;
                    return false;
                }
                typename math::curve<T>::points_t points;
                for(auto item : items)
                {
                    simdjson::dom::object key;
                    double progress{};
                    typename math::curve<T>::point_t point;
                    if(item.get_object().get(key) != simdjson::SUCCESS ||
                       key["progress"].get_double().get(progress) != simdjson::SUCCESS ||
                       !curve_key_json<T>::from_fields(key, point.element, error))
                    {
                        error = error.empty() ? expected : error;
                        return false;
                    }
                    point.progress = static_cast<float>(progress);
                    points.push_back(point);
                }
                const auto* current = out.try_cast<math::curve<T>>();
                math::curve<T> curve = current != nullptr ? *current : math::curve<T>{};
                curve.set_points_keeping_shape(points);
                out = entt::meta_any{std::move(curve)};
                return true;
            }};
}

auto make_bbox_codec() -> value_codec
{
    return {.schema_type = "object",
            .format_hint = "{min: [x, y, z], max: [x, y, z]}",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                return bbox_to_json(value.cast<const math::bbox&>());
            },
            .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                simdjson::dom::object box;
                simdjson::dom::element low;
                simdjson::dom::element high;
                math::bbox bounds;
                if(element.get_object().get(box) != simdjson::SUCCESS || box["min"].get(low) != simdjson::SUCCESS ||
                   box["max"].get(high) != simdjson::SUCCESS)
                {
                    error = "expected {min: [x, y, z], max: [x, y, z]}";
                    return false;
                }
                if(!parse_vec3(low, bounds.min, error) || !parse_vec3(high, bounds.max, error))
                {
                    return false;
                }
                out = entt::meta_any{bounds};
                return true;
            }};
}

/// Seconds, as a number.
auto make_seconds_codec() -> value_codec
{
    using seconds = std::chrono::duration<float>;
    return {.schema_type = "number",
            .format_hint = "seconds",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                return fmt::format("{}", value.cast<seconds>().count());
            },
            .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                float value{};
                if(!parse_number(element, value, error))
                {
                    return false;
                }
                out = entt::meta_any{seconds(value)};
                return true;
            },
            .as_double = [](const entt::meta_any& value) -> double
            {
                return static_cast<double>(value.cast<seconds>().count());
            }};
}

/// An enum value's spelling without case, spaces or underscores: "out_expo", "OutExpo" and "Out Expo" are one value.
auto fold_enum_spelling(hpp::string_view text) -> std::string
{
    std::string folded;
    for(const char c : text)
    {
        if(c != '_' && c != ' ')
        {
            folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return folded;
}

auto layer_names(rtti::context& ctx) -> const std::array<std::string, 32>&
{
    // The names the running game uses; the layer inspector reads the same ones.
    if(ctx.has<settings>())
    {
        return ctx.get<settings>().layer.layers;
    }
    return get_reserved_layers_as_array();
}

template<typename T>
auto make_handle_codec() -> value_codec
{
    return {.schema_type = "string",
            .format_hint = "asset key or uid, or null",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto* handle = value.try_cast<asset_handle<T>>();
                if(handle == nullptr)
                {
                    return std::nullopt;
                }
                return asset_handle_key_json(*handle);
            },
            .from_json = [](rtti::context& ctx, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                if(element.is_null())
                {
                    out = entt::meta_any{asset_handle<T>{}};
                    return true;
                }
                std::string key;
                if(!parse_string(element, key, error))
                {
                    return false;
                }
                if(key.empty())
                {
                    out = entt::meta_any{asset_handle<T>{}};
                    return true;
                }
                auto handle = find_existing_asset<T>(ctx, to_asset_key(key), error);
                if(!handle)
                {
                    return false;
                }
                out = entt::meta_any{handle};
                return true;
            }};
}

/// A fixed-length vector of numbers or booleans (vec4, bvec3) crosses as an array of that length.
template<int Length, typename T>
auto make_vector_codec() -> value_codec
{
    using vector_type = glm::vec<Length, T, glm::defaultp>;
    constexpr bool is_bool = std::is_same_v<T, bool>;
    static const std::string schema_extra =
        fmt::format(R"("items":{{"type":"{}"}},"minItems":{},"maxItems":{})", is_bool ? "boolean" : "number", Length, Length);
    return {.schema_type = "array",
            .schema_extra = schema_extra.c_str(),
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto& vector = value.cast<const vector_type&>();
                std::string json = "[";
                for(int i = 0; i < Length; ++i)
                {
                    json += (i > 0 ? "," : "");
                    if constexpr(is_bool)
                    {
                        json += bool_to_json(vector[i]);
                    }
                    else
                    {
                        json += fmt::format("{}", vector[i]);
                    }
                }
                return json + "]";
            },
            .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                simdjson::dom::array items;
                if(element.get_array().get(items) != simdjson::SUCCESS || items.size() != static_cast<size_t>(Length))
                {
                    error = fmt::format("expected an array of {} {}", Length, is_bool ? "booleans" : "numbers");
                    return false;
                }
                vector_type vector{};
                int index = 0;
                for(auto item : items)
                {
                    bool is_read = false;
                    if constexpr(is_bool)
                    {
                        is_read = parse_bool(item, vector[index], error);
                    }
                    else
                    {
                        is_read = parse_number(item, vector[index], error);
                    }
                    if(!is_read)
                    {
                        return false;
                    }
                    ++index;
                }
                out = entt::meta_any{vector};
                return true;
            }};
}

/// An entity reference crosses as the entity's id, found in the scene being edited.
auto make_entity_codec() -> value_codec
{
    return {.schema_type = "string",
            .format_hint = "entity id, or null",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto& entity = value.cast<const entt::handle&>();
                return entity ? make_json_string(entity_id_string(entity)) : std::string("null");
            },
            .from_json = [](rtti::context& ctx, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                if(element.is_null())
                {
                    out = entt::meta_any{entt::handle{}};
                    return true;
                }
                std::string id;
                if(!parse_string(element, id, error))
                {
                    return false;
                }
                auto* scn = ctx.get_cached<editing_manager>().get_active_scene(ctx);
                const auto entity = scn != nullptr ? find_entity_by_id(*scn, id) : entt::handle{};
                if(!entity)
                {
                    error = "no entity " + id + " in the scene being edited";
                    return false;
                }
                out = entt::meta_any{entity};
                return true;
            }};
}

/// A name and the flag it stands for, in the order the inspector shows them.
struct named_flag
{
    const char* name;
    uint32_t flag;
};

constexpr std::array<named_flag, 3> k_horizontal_alignments{{
    {"left", align::left},
    {"center", align::center},
    {"right", align::right},
}};

constexpr std::array<named_flag, 6> k_vertical_alignments{{
    {"top", align::top},
    {"middle", align::middle},
    {"bottom", align::bottom},
    {"capline", align::capline},
    {"midline", align::midline},
    {"baseline", align::baseline},
}};

constexpr std::array<named_flag, 5> k_text_style_flags{{
    {"overline", gfx::style_overline},
    {"underline", gfx::style_underline},
    {"strike_through", gfx::style_strike_through},
    {"background", gfx::style_background},
    {"foreground", gfx::style_foreground},
}};

/// The name of the first entry of @p names whose flag @p flags has; the first name when none is set.
template<size_t Count>
auto flag_name(const std::array<named_flag, Count>& names, uint32_t flags) -> const char*
{
    const auto found = std::find_if(names.begin(),
                                    names.end(),
                                    [flags](const named_flag& entry)
                                    {
                                        return (flags & entry.flag) != 0;
                                    });
    return found != names.end() ? found->name : names.front().name;
}

template<size_t Count>
auto find_flag(const std::array<named_flag, Count>& names, hpp::string_view name, uint32_t& flag, std::string& error)
    -> bool
{
    const auto found = std::find_if(names.begin(),
                                    names.end(),
                                    [name](const named_flag& entry)
                                    {
                                        return name == entry.name;
                                    });
    if(found == names.end())
    {
        std::vector<std::string> spellings;
        std::transform(names.begin(),
                       names.end(),
                       std::back_inserter(spellings),
                       [](const named_flag& entry)
                       {
                           return std::string(entry.name);
                       });
        error = "\"" + std::string(name) + "\" is not one of " + fmt::format("{}", fmt::join(spellings, ", "));
        return false;
    }
    flag = found->flag;
    return true;
}

/// Text alignment crosses as {horizontal, vertical}, the inspector's two rows; a write may name one and keep
/// the other.
auto make_alignment_codec() -> value_codec
{
    return {.schema_type = "object",
            .schema_extra =
                R"("properties":{"horizontal":{"type":"string","enum":["left","center","right"]},"vertical":{"type":"string","enum":["top","middle","bottom","capline","midline","baseline"]}})",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto flags = value.cast<const alignment&>().flags;
                return fmt::format(R"({{"horizontal":"{}","vertical":"{}"}})",
                                   flag_name(k_horizontal_alignments, flags),
                                   flag_name(k_vertical_alignments, flags));
            },
            .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                simdjson::dom::object fields;
                if(element.get_object().get(fields) != simdjson::SUCCESS)
                {
                    error = "expected {horizontal, vertical}";
                    return false;
                }
                const auto* current = out.try_cast<alignment>();
                alignment result = current != nullptr ? *current : alignment{};
                hpp::string_view name;
                uint32_t flag{};
                if(fields["horizontal"].get_string().get(name) == simdjson::SUCCESS)
                {
                    if(!find_flag(k_horizontal_alignments, name, flag, error))
                    {
                        return false;
                    }
                    result.flags = (result.flags & ~align::horizontal_mask) | flag;
                }
                if(fields["vertical"].get_string().get(name) == simdjson::SUCCESS)
                {
                    if(!find_flag(k_vertical_alignments, name, flag, error))
                    {
                        return false;
                    }
                    result.flags = (result.flags & ~align::vertical_text_mask) | flag;
                }
                out = entt::meta_any{result};
                return true;
            }};
}

/// Text style flags cross as the names of the flags set.
auto make_text_style_flags_codec() -> value_codec
{
    return {.schema_type = "array",
            .schema_extra =
                R"("items":{"type":"string","enum":["overline","underline","strike_through","background","foreground"]})",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto flags = value.cast<const text_style_flags&>().flags;
                std::vector<std::string> names;
                for(const auto& entry : k_text_style_flags)
                {
                    if((flags & entry.flag) != 0)
                    {
                        names.emplace_back(entry.name);
                    }
                }
                return strings_to_json_array(names);
            },
            .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                simdjson::dom::array items;
                if(element.get_array().get(items) != simdjson::SUCCESS)
                {
                    error = "expected an array of style names";
                    return false;
                }
                text_style_flags result{gfx::style_normal};
                for(auto item : items)
                {
                    hpp::string_view name;
                    uint32_t flag{};
                    if(item.get_string().get(name) != simdjson::SUCCESS)
                    {
                        error = "style names are strings";
                        return false;
                    }
                    if(!find_flag(k_text_style_flags, name, flag, error))
                    {
                        return false;
                    }
                    result.flags |= flag;
                }
                out = entt::meta_any{result};
                return true;
            }};
}

using physics_shape = physics_compound_shape::shape_t;

/// The JSON name of physics shape type @p type: its pretty name in lower case ("box", "mesh").
auto shape_type_name(const entt::meta_type& type) -> std::string
{
    return to_lower_ascii(entt::get_pretty_name(type));
}

/// Calls @p visit with a default value of each shape type a physics shape can hold, until it returns true.
template<size_t Index = 0, typename Visitor>
auto find_shape_type(Visitor&& visit) -> bool
{
    if constexpr(Index < std::variant_size_v<physics_shape>)
    {
        if(visit(std::variant_alternative_t<Index, physics_shape>{}))
        {
            return true;
        }
        return find_shape_type<Index + 1>(std::forward<Visitor>(visit));
    }
    return false;
}

/// The names of the shape types, for errors.
auto shape_type_names() -> std::vector<std::string>
{
    std::vector<std::string> names;
    find_shape_type(
        [&names](auto shape)
        {
            names.push_back(shape_type_name(entt::resolve<decltype(shape)>()));
            return false;
        });
    return names;
}

/// One collision shape crosses as {type, then that shape type's reflected fields}. A write of the same type
/// keeps the fields it leaves out; another type starts from that type's defaults.
auto make_physics_shape_codec() -> value_codec
{
    return {.schema_type = "object",
            .format_hint = "{type: box|sphere|capsule|cylinder|mesh, then that shape's fields}",
            .to_json = [](rtti::context& ctx, const entt::meta_any& value) -> std::optional<std::string>
            {
                return std::visit(
                    [&ctx](const auto& shape) -> std::string
                    {
                        auto object = entt::forward_as_meta(shape);
                        const auto fields = meta_properties_to_json(ctx, object);
                        const auto type_field =
                            R"({"type":)" + make_json_string(shape_type_name(entt::resolve<std::decay_t<decltype(shape)>>()));
                        return fields.size() > 2 ? type_field + "," + fields.substr(1) : type_field + "}";
                    },
                    value.cast<const physics_compound_shape&>().shape);
            },
            .from_json = [](rtti::context& ctx, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                -> bool
            {
                simdjson::dom::object fields;
                hpp::string_view type_name;
                if(element.get_object().get(fields) != simdjson::SUCCESS ||
                   fields["type"].get_string().get(type_name) != simdjson::SUCCESS)
                {
                    error = "expected {type: " + fmt::format("{}", fmt::join(shape_type_names(), "|")) + ", ...}";
                    return false;
                }
                // The shape's own fields, without the type that picked it.
                std::string shape_json = "{";
                for(auto field : fields)
                {
                    if(field.key != "type")
                    {
                        shape_json += (shape_json.size() > 1 ? "," : "") + make_json_string(std::string(field.key)) + ":" +
                                      std::string(simdjson::minify(field.value));
                    }
                }
                shape_json += "}";
                simdjson::dom::parser parser;
                simdjson::dom::object shape_fields;
                if(parser.parse(shape_json).get(shape_fields) != simdjson::SUCCESS)
                {
                    error = "the shape's fields could not be read";
                    return false;
                }
                const auto* current = out.try_cast<physics_compound_shape>();
                physics_compound_shape compound = current != nullptr ? *current : physics_compound_shape{};
                const bool is_known = find_shape_type(
                    [&](auto shape) -> bool
                    {
                        using shape_type = decltype(shape);
                        if(shape_type_name(entt::resolve<shape_type>()) != type_name)
                        {
                            return false;
                        }
                        if(const auto* same = std::get_if<shape_type>(&compound.shape))
                        {
                            shape = *same;
                        }
                        auto object = entt::forward_as_meta(shape);
                        component_apply_result result;
                        apply_meta_properties(ctx, object, shape_fields, result);
                        if(result.ok)
                        {
                            compound.shape = shape;
                        }
                        error = fmt::format("{}", fmt::join(result.errors, "; "));
                        return true;
                    });
                if(!is_known)
                {
                    error = "\"" + std::string(type_name) + "\" is not one of " +
                            fmt::format("{}", fmt::join(shape_type_names(), ", "));
                    return false;
                }
                if(!error.empty())
                {
                    return false;
                }
                out = entt::meta_any{compound};
                return true;
            }};
}

auto codecs() -> const std::unordered_map<entt::id_type, value_codec>&
{
    static const auto table = []
    {
        std::unordered_map<entt::id_type, value_codec> result;
        auto add = [&](const entt::type_info& info, value_codec codec)
        {
            result.emplace(info.hash(), std::move(codec));
        };
        add(entt::type_id<float>(), make_number_codec<float>("number"));
        add(entt::type_id<double>(), make_number_codec<double>("number"));
        add(entt::type_id<int8_t>(), make_number_codec<int8_t>("integer"));
        add(entt::type_id<int16_t>(), make_number_codec<int16_t>("integer"));
        add(entt::type_id<int32_t>(), make_number_codec<int32_t>("integer"));
        add(entt::type_id<int64_t>(), make_number_codec<int64_t>("integer"));
        add(entt::type_id<uint8_t>(), make_number_codec<uint8_t>("integer"));
        add(entt::type_id<uint16_t>(), make_number_codec<uint16_t>("integer"));
        add(entt::type_id<uint32_t>(), make_number_codec<uint32_t>("integer"));
        add(entt::type_id<uint64_t>(), make_number_codec<uint64_t>("integer"));
        add(entt::type_id<bool>(),
            {.schema_type = "boolean",
             .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
             {
                 return std::string(bool_to_json(value.cast<bool>()));
             },
             .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                 -> bool
             {
                 bool flag = false;
                 if(!parse_bool(element, flag, error))
                 {
                     return false;
                 }
                 out = entt::meta_any{flag};
                 return true;
             }});
        add(entt::type_id<size<float>>(), make_size_codec<float>());
        add(entt::type_id<frange_t>(), make_range_codec<float>());
        add(entt::type_id<size<int32_t>>(), make_size_codec<int32_t>());
        add(entt::type_id<size<uint32_t>>(), make_size_codec<uint32_t>());
        add(entt::type_id<std::string>(),
            {.schema_type = "string",
             .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
             {
                 return make_json_string(value.cast<std::string>());
             },
             .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                 -> bool
             {
                 std::string text;
                 if(!parse_string(element, text, error))
                 {
                     return false;
                 }
                 out = entt::meta_any{std::move(text)};
                 return true;
             }});
        add(entt::type_id<math::vec2>(),
            {.schema_type = "array",
             .schema_extra = R"("items":{"type":"number"},"minItems":2,"maxItems":2)",
             .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
             {
                 const auto v = value.cast<math::vec2>();
                 return fmt::format("[{},{}]", v.x, v.y);
             },
             .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                 -> bool
             {
                 math::vec2 v{};
                 if(!parse_vec2(element, v, error))
                 {
                     return false;
                 }
                 out = entt::meta_any{v};
                 return true;
             }});
        add(entt::type_id<math::vec3>(),
            {.schema_type = "array",
             .schema_extra = R"("items":{"type":"number"},"minItems":3,"maxItems":3)",
             .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
             {
                 const auto v = value.cast<math::vec3>();
                 return fmt::format("[{},{},{}]", v.x, v.y, v.z);
             },
             .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                 -> bool
             {
                 math::vec3 v{};
                 if(!parse_vec3(element, v, error))
                 {
                     return false;
                 }
                 out = entt::meta_any{v};
                 return true;
             }});
        add(entt::type_id<math::color>(),
            {.schema_type = "array",
             .schema_extra = R"("items":{"type":"number"},"minItems":3,"maxItems":4)",
             .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
             {
                 return exact_color_json(value.cast<math::color>());
             },
             .from_json = [](rtti::context&, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                 -> bool
             {
                 math::color c{};
                 if(!parse_color(element, c, error))
                 {
                     return false;
                 }
                 out = entt::meta_any{c};
                 return true;
             }});
        add(entt::type_id<layer_mask>(),
            {.schema_type = "array",
             .format_hint = "layer names or indices, or \"everything\" / \"nothing\"",
             .to_json = [](rtti::context& ctx, const entt::meta_any& value) -> std::optional<std::string>
             {
                 return layer_mask_to_json(ctx, value.cast<layer_mask>());
             },
             .from_json = [](rtti::context& ctx, const simdjson::dom::element& element, entt::meta_any& out, std::string& error)
                 -> bool
             {
                 layer_mask mask{};
                 if(!parse_layer_mask(ctx, element, mask, error))
                 {
                     return false;
                 }
                 out = entt::meta_any{mask};
                 return true;
             }});
        add(entt::type_id<asset_handle<gfx::texture>>(), make_handle_codec<gfx::texture>());
        add(entt::type_id<asset_handle<physics_material>>(), make_handle_codec<physics_material>());
        add(entt::type_id<asset_handle<font>>(), make_handle_codec<font>());
        add(entt::type_id<asset_handle<audio_clip>>(), make_handle_codec<audio_clip>());
        add(entt::type_id<asset_handle<animation_clip>>(), make_handle_codec<animation_clip>());
        add(entt::type_id<asset_handle<material>>(), make_handle_codec<material>());
        add(entt::type_id<asset_handle<ui_tree>>(), make_handle_codec<ui_tree>());
        add(entt::type_id<asset_handle<mesh>>(), make_handle_codec<mesh>());
        add(entt::type_id<asset_handle<prefab>>(), make_handle_codec<prefab>());
        add(entt::type_id<asset_handle<scene_prefab>>(), make_handle_codec<scene_prefab>());
        add(entt::type_id<math::curve<float>>(), make_curve_codec<float>());
        add(entt::type_id<math::curve<frange_t>>(), make_curve_codec<frange_t>());
        add(entt::type_id<math::curve<math::color>>(), make_curve_codec<math::color>());
        add(entt::type_id<math::curve<math::vec3>>(), make_curve_codec<math::vec3>());
        add(entt::type_id<math::bbox>(), make_bbox_codec());
        add(entt::type_id<math::vec4>(), make_vector_codec<4, float>());
        add(entt::type_id<math::bvec3>(), make_vector_codec<3, bool>());
        add(entt::type_id<entt::handle>(), make_entity_codec());
        add(entt::type_id<range<uint32_t>>(), make_range_codec<uint32_t>());
        add(entt::type_id<alignment>(), make_alignment_codec());
        add(entt::type_id<text_style_flags>(), make_text_style_flags_codec());
        add(entt::type_id<physics_compound_shape>(), make_physics_shape_codec());
        add(entt::type_id<std::chrono::duration<float>>(), make_seconds_codec());
        return result;
    }();
    return table;
}

/// Calls @p visit with each reflected property of @p type: its base types' first, then its own - the
/// order the inspector draws them in.
template<typename Visitor>
void for_each_meta_property(const entt::meta_type& type, Visitor&& visit)
{
    for(auto [base_id, base] : type.base())
    {
        for(auto [id, prop] : base.data())
        {
            visit(prop);
        }
    }
    for(auto [id, prop] : type.data())
    {
        visit(prop);
    }
}

auto find_codec(const entt::meta_type& type) -> const value_codec*
{
    if(!type)
    {
        return nullptr;
    }
    const auto& table = codecs();
    const auto found = table.find(type.info().hash());
    return found == table.end() ? nullptr : &found->second;
}

/// A reflected struct the bridge walks into, rather than a leaf value.
auto is_nested_object(const entt::meta_type& type) -> bool
{
    if(!type || type.is_enum() || find_codec(type) != nullptr || type.is_sequence_container() ||
       type.is_associative_container())
    {
        return false;
    }
    bool has_property = false;
    for_each_meta_property(type,
                           [&has_property](const entt::meta_data&)
                           {
                               has_property = true;
                           });
    return has_property;
}

auto enum_value_names(const entt::meta_type& type) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(auto [id, value] : type.data())
    {
        const auto name = entt::get_name(value);
        if(!name.empty())
        {
            names.push_back(name);
        }
    }
    return names;
}

auto enum_to_json(const entt::meta_any& value) -> std::optional<std::string>
{
    for(auto [id, candidate] : value.type().data())
    {
        if(candidate.get({}) == value)
        {
            return make_json_string(entt::get_name(candidate));
        }
    }
    return std::nullopt;
}

auto enum_from_json(const simdjson::dom::element& element,
                    const entt::meta_type& type,
                    entt::meta_any& out,
                    std::string& error) -> bool
{
    hpp::string_view name;
    if(element.get_string().get(name) != simdjson::SUCCESS)
    {
        error = "expected one of " + fmt::format("{}", fmt::join(enum_value_names(type), ", "));
        return false;
    }
    return find_meta_enum_value(type, name, out, error);
}

auto value_to_json(rtti::context& ctx, entt::meta_any& value) -> std::optional<std::string>;
auto schema_type_of(const entt::meta_type& type) -> const char*;

/// A container's elements as a JSON array; nothing when its element type cannot be spelled, empty or not.
auto sequence_to_json(rtti::context& ctx, entt::meta_any& value) -> std::optional<std::string>
{
    if(hpp::string_view(schema_type_of(value.type())) == "unsupported")
    {
        return std::nullopt;
    }
    std::string json = "[";
    for(auto element : value.as_sequence_container())
    {
        const auto element_json = value_to_json(ctx, element);
        if(!element_json)
        {
            return std::nullopt;
        }
        json += (json.size() > 1 ? "," : "") + *element_json;
    }
    return json + "]";
}

auto value_to_json(rtti::context& ctx, entt::meta_any& value) -> std::optional<std::string>
{
    const auto type = value.type();
    if(const auto* codec = find_codec(type))
    {
        return codec->to_json(ctx, value);
    }
    if(type.is_enum())
    {
        return enum_to_json(value);
    }
    if(type.is_sequence_container())
    {
        return sequence_to_json(ctx, value);
    }
    if(is_nested_object(type))
    {
        return meta_properties_to_json(ctx, value);
    }
    return std::nullopt;
}

auto is_read_only(const entt::meta_any& object, const entt::meta_data& prop) -> bool
{
    return prop.is_const() || entt::is_property_readonly(object, prop);
}

/// The reflection's min / max attributes, which the inspector's widgets clamp to.
auto check_range(const entt::meta_data& prop, double value, std::string& error) -> bool
{
    const auto& min_attr = entt::get_attribute(prop, "min");
    if(min_attr && value < entt::get_attribute_as<double>(prop, "min"))
    {
        error = fmt::format("must be at least {:.6g}", entt::get_attribute_as<double>(prop, "min"));
        return false;
    }
    const auto& max_attr = entt::get_attribute(prop, "max");
    if(max_attr && value > entt::get_attribute_as<double>(prop, "max"))
    {
        error = fmt::format("must be at most {:.6g}", entt::get_attribute_as<double>(prop, "max"));
        return false;
    }
    return true;
}

/// The element type of sequence container type @p type.
auto element_type_of(const entt::meta_type& type) -> entt::meta_type
{
    auto instance = type.construct();
    return instance ? instance.as_sequence_container().value_type() : entt::meta_type{};
}

auto schema_type_of(const entt::meta_type& type) -> const char*
{
    if(const auto* codec = find_codec(type))
    {
        return codec->schema_type;
    }
    if(type.is_enum())
    {
        return "string";
    }
    if(type.is_sequence_container())
    {
        return hpp::string_view(schema_type_of(element_type_of(type))) == "unsupported" ? "unsupported" : "array";
    }
    if(is_nested_object(type))
    {
        return "object";
    }
    return "unsupported";
}

void write_properties(rtti::context& ctx,
                      entt::meta_any& object,
                      const simdjson::dom::object& properties,
                      const std::string& path,
                      component_apply_result& result,
                      const std::unordered_set<std::string>* only);

auto write_value(rtti::context& ctx,
                 entt::meta_any& target,
                 const simdjson::dom::element& value,
                 const std::string& path,
                 component_apply_result& result) -> bool;

/// A container takes the array's length - new elements start from their defaults - and each element.
auto write_sequence(rtti::context& ctx,
                    entt::meta_any& target,
                    const simdjson::dom::element& value,
                    const std::string& path,
                    component_apply_result& result) -> bool
{
    const auto fail = [&](const std::string& message)
    {
        result.ok = false;
        result.errors.push_back(path + ": " + message);
        return false;
    };
    simdjson::dom::array items;
    if(value.get_array().get(items) != simdjson::SUCCESS)
    {
        return fail("expected an array");
    }
    auto sequence = target.as_sequence_container();
    if(!sequence.resize(items.size()))
    {
        return fail("the list cannot take this length");
    }
    bool is_written = true;
    size_t index = 0;
    for(auto item : items)
    {
        auto element = sequence[index];
        is_written = write_value(ctx, element, item, fmt::format("{}[{}]", path, index), result) && is_written;
        ++index;
    }
    return is_written;
}

/**
 * @brief Reads @p value into @p target, which refers to the value being written: a property's current
 * value, or a container element.
 *
 * A nested struct merges the fields @p value names, a container takes @p value's length and each
 * element, anything else is replaced. Errors are reported at @p path.
 */
auto write_value(rtti::context& ctx,
                 entt::meta_any& target,
                 const simdjson::dom::element& value,
                 const std::string& path,
                 component_apply_result& result) -> bool
{
    const auto fail = [&](const std::string& message)
    {
        result.ok = false;
        result.errors.push_back(path + ": " + message);
        return false;
    };
    const auto type = target.type();
    std::string error;
    if(const auto* codec = find_codec(type))
    {
        // The codec reads the current value through the reference (a curve keeps its tangents) and
        // returns a new value, which is then stored where the target points.
        auto decoded = target.as_ref();
        if(!codec->from_json(ctx, value, decoded, error))
        {
            return fail(error);
        }
        return target.assign(decoded) || fail("the value could not be stored");
    }
    if(type.is_enum())
    {
        entt::meta_any decoded;
        if(!enum_from_json(value, type, decoded, error))
        {
            return fail(error);
        }
        return target.assign(decoded) || fail("the value could not be stored");
    }
    if(type.is_sequence_container())
    {
        return write_sequence(ctx, target, value, path, result);
    }
    simdjson::dom::object fields;
    if(!is_nested_object(type))
    {
        return fail(fmt::format("type {} cannot be set through MCP", entt::get_pretty_name(type)));
    }
    if(value.get_object().get(fields) != simdjson::SUCCESS)
    {
        return fail("expected an object");
    }
    const auto errors_before = result.errors.size();
    const auto unknown_before = result.unknown.size();
    write_properties(ctx, target, fields, path + ".", result, nullptr);
    return result.errors.size() == errors_before && result.unknown.size() == unknown_before;
}

/// True when @p value spells what @p prop holds now: writing back what a read gave, read-only
/// properties included, is not an edit. Both sides go through the property's own JSON spelling,
/// so formatting differences (1 vs 1.0) do not count.
auto is_unchanged(rtti::context& ctx,
                  entt::meta_any& object,
                  const entt::meta_data& prop,
                  const simdjson::dom::element& value) -> bool
{
    auto current = prop.get(object);
    const auto current_json = value_to_json(ctx, current);
    auto incoming = prop.get(object);
    component_apply_result ignored;
    if(!current_json || !write_value(ctx, incoming, value, {}, ignored))
    {
        return false;
    }
    const auto incoming_json = value_to_json(ctx, incoming);
    return incoming_json && *incoming_json == *current_json;
}

/**
 * @brief Writes each of @p properties through its reflected setter.
 *
 * @p path is where @p object sits ("" at the top, "light." inside a nested struct), so every error
 * and unknown name reads from the top. Only top-level properties are listed as applied.
 */
void write_properties(rtti::context& ctx,
                      entt::meta_any& object,
                      const simdjson::dom::object& properties,
                      const std::string& path,
                      component_apply_result& result,
                      const std::unordered_set<std::string>* only)
{
    const auto type = object.type();
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto key_path = path + key;
        const auto fail = [&](const std::string& message)
        {
            result.ok = false;
            result.errors.push_back(key_path + ": " + message);
        };
        const auto prop = find_meta_property(type, key);
        if(!prop || (only != nullptr && only->count(key) == 0))
        {
            if(const auto renamed = entt::find_renamed_property(type, key))
            {
                fail("renamed " + entt::get_name(renamed));
                continue;
            }
            result.unknown.push_back(key_path);
            continue;
        }
        if(is_read_only(object, prop))
        {
            if(!is_unchanged(ctx, object, prop, field.value))
            {
                result.read_only.push_back(key_path);
            }
            continue;
        }
        // The value is edited as a whole and goes back through the property's setter, as an inspector
        // edit of one sub-field does.
        auto current = prop.get(object);
        if(!write_value(ctx, current, field.value, key_path, result))
        {
            continue;
        }
        const auto* codec = find_codec(prop.type());
        std::string error;
        if(codec != nullptr && codec->as_double && !check_range(prop, codec->as_double(current), error))
        {
            fail(error);
            continue;
        }
        if(!prop.set(object, current))
        {
            fail("the setter rejected the value");
            continue;
        }
        if(path.empty())
        {
            result.applied.push_back(key);
        }
    }
}

} // namespace

auto layer_mask_to_json(rtti::context& ctx, const layer_mask& mask) -> std::string
{
    if(mask.mask == layer_reserved::everything_layer)
    {
        return R"(["everything"])";
    }
    const auto& names = layer_names(ctx);
    std::vector<std::string> set_layers;
    for(size_t i = 0; i < names.size(); ++i)
    {
        if((static_cast<uint32_t>(mask.mask) & (1u << i)) != 0)
        {
            set_layers.push_back(names[i].empty() ? fmt::format("#{}", i) : names[i]);
        }
    }
    return strings_to_json_array(set_layers);
}

auto parse_layer_mask(rtti::context& ctx, const simdjson::dom::element& value, layer_mask& out, std::string& error)
    -> bool
{
    hpp::string_view word;
    if(value.get_string().get(word) == simdjson::SUCCESS)
    {
        if(word == "everything" || word == "nothing")
        {
            out.mask = word == "everything" ? layer_reserved::everything_layer : layer_reserved::nothing_layer;
            return true;
        }
        error = "expected an array of layer names or indices, or \"everything\" / \"nothing\"";
        return false;
    }
    simdjson::dom::array items;
    if(value.get_array().get(items) != simdjson::SUCCESS)
    {
        error = "expected an array of layer names or indices";
        return false;
    }
    const auto& names = layer_names(ctx);
    uint32_t bits = 0;
    for(auto item : items)
    {
        int64_t index = -1;
        hpp::string_view name;
        if(item.get_int64().get(index) == simdjson::SUCCESS)
        {
            if(index < 0 || index >= static_cast<int64_t>(names.size()))
            {
                error = fmt::format("layer index {} out of range 0..{}", index, names.size() - 1);
                return false;
            }
        }
        else if(item.get_string().get(name) == simdjson::SUCCESS)
        {
            if(name == "everything")
            {
                out.mask = layer_reserved::everything_layer;
                return true;
            }
            const auto found = std::find(names.begin(), names.end(), name);
            if(name.empty() || found == names.end())
            {
                error = "unknown layer \"" + std::string(name) +
                        "\" (name it with settings_set layer.layers, or pass its index)";
                return false;
            }
            index = std::distance(names.begin(), found);
        }
        else
        {
            error = "layer entries must be names or indices";
            return false;
        }
        bits |= 1u << static_cast<uint32_t>(index);
    }
    out.mask = static_cast<int>(bits);
    return true;
}

auto meta_properties_to_json(rtti::context& ctx,
                             entt::meta_any& object,
                             const std::unordered_set<std::string>* only,
                             bool include_hidden) -> std::string
{
    std::string json = "{";
    for_each_meta_property(object.type(),
                           [&](const entt::meta_data& prop)
                           {
                               const auto name = entt::get_name(prop);
                               if(name.empty() || (only != nullptr && only->count(name) == 0) ||
                                  (!include_hidden && !entt::is_property_visible(object, prop)))
                               {
                                   return;
                               }
                               auto value = prop.get(object);
                               const auto value_json = value ? value_to_json(ctx, value) : std::nullopt;
                               if(value_json)
                               {
                                   json += (json.size() > 1 ? "," : "") + make_json_string(name) + ":" + *value_json;
                               }
                           });
    return json + "}";
}

void apply_meta_properties(rtti::context& ctx,
                           entt::meta_any& object,
                           const simdjson::dom::object& properties,
                           component_apply_result& result,
                           const std::unordered_set<std::string>* only)
{
    write_properties(ctx, object, properties, {}, result, only);
    if(!result.unknown.empty())
    {
        // A typo silently ignored reads as success; reject it so the caller sees the name.
        result.ok = false;
        result.errors.push_back("unknown properties: " + fmt::format("{}", fmt::join(result.unknown, ", ")) +
                                " (see scene_list_components)");
    }
}

auto meta_value_to_json(rtti::context& ctx, entt::meta_any& value) -> std::optional<std::string>
{
    return value_to_json(ctx, value);
}

auto meta_value_from_json(rtti::context& ctx,
                          const simdjson::dom::element& value,
                          entt::meta_any& target,
                          std::string& error) -> bool
{
    component_apply_result result;
    if(write_value(ctx, target, value, {}, result))
    {
        return true;
    }
    // Errors read "<path>: <reason>"; at the value itself the path is empty.
    for(auto& message : result.errors)
    {
        message.erase(0, message.rfind(": ", 0) == 0 ? 2 : 0);
    }
    error = fmt::format("{}", fmt::join(result.errors, "; "));
    return false;
}

auto meta_enum_names(const entt::meta_type& type) -> std::vector<std::string>
{
    return enum_value_names(type);
}

auto find_meta_enum_value(const entt::meta_type& type,
                          hpp::string_view name,
                          entt::meta_any& out,
                          std::string& error) -> bool
{
    const std::string wanted = fold_enum_spelling(name);
    for(auto [id, candidate] : type.data())
    {
        if(fold_enum_spelling(entt::get_name(candidate)) == wanted ||
           fold_enum_spelling(entt::get_pretty_name(candidate)) == wanted)
        {
            out = candidate.get({});
            return true;
        }
    }
    error = "\"" + std::string(name.data(), name.size()) + "\" is not one of " +
            fmt::format("{}", fmt::join(enum_value_names(type), ", "));
    return false;
}

auto count_meta_properties(const entt::meta_type& type) -> size_t
{
    size_t count = 0;
    for_each_meta_property(type,
                           [&count](const entt::meta_data& prop)
                           {
                               const bool is_expressible = !entt::get_name(prop).empty() &&
                                                           hpp::string_view(schema_type_of(prop.type())) != "unsupported";
                               count += is_expressible ? 1 : 0;
                           });
    return count;
}

auto find_meta_property(const entt::meta_type& type, hpp::string_view name) -> entt::meta_data
{
    entt::meta_data found;
    for_each_meta_property(type,
                           [&](const entt::meta_data& prop)
                           {
                               if(!found && entt::get_name(prop) == name)
                               {
                                   found = prop;
                               }
                           });
    return found;
}

namespace
{
auto object_properties_schema(const entt::meta_type& type) -> std::string;

/// Schema fields for a value of @p type (no braces): its JSON type and the enum values, struct fields
/// or element schema it has.
auto type_schema_fields(const entt::meta_type& type) -> std::string
{
    std::string fields = fmt::format(R"("type":{})", make_json_string(schema_type_of(type)));
    const auto* codec = find_codec(type);
    if(codec != nullptr && codec->schema_extra != nullptr)
    {
        fields += std::string(",") + codec->schema_extra;
    }
    if(type.is_enum())
    {
        fields += ",\"enum\":" + strings_to_json_array(enum_value_names(type));
    }
    if(type.is_sequence_container())
    {
        fields += ",\"items\":{" + type_schema_fields(element_type_of(type)) + "}";
    }
    if(is_nested_object(type))
    {
        fields += "," + object_properties_schema(type);
    }
    return fields;
}

/// "properties":{name:{schema},...} of a reflected struct, leaving out what the bridge cannot express.
auto object_properties_schema(const entt::meta_type& type) -> std::string
{
    std::string properties;
    for_each_meta_property(type,
                           [&properties](const entt::meta_data& prop)
                           {
                               const auto name = entt::get_name(prop);
                               if(!name.empty() && hpp::string_view(schema_type_of(prop.type())) != "unsupported")
                               {
                                   properties += (properties.empty() ? "" : ",") + make_json_string(name) + ":{" +
                                                 meta_property_schema(prop) + "}";
                               }
                           });
    return "\"properties\":{" + properties + "}";
}
} // namespace

auto meta_object_schema(const entt::meta_type& type) -> std::string
{
    return "\"type\":\"object\"," + object_properties_schema(type);
}

auto meta_property_schema(const entt::meta_data& prop) -> std::string
{
    const auto prop_type = prop.type();
    std::string fields = type_schema_fields(prop_type);
    const auto* codec = find_codec(prop_type);
    if(entt::get_attribute(prop, "min"))
    {
        fields += fmt::format(",\"minimum\":{:.6g}", entt::get_attribute_as<double>(prop, "min"));
    }
    if(entt::get_attribute(prop, "max"))
    {
        fields += fmt::format(",\"maximum\":{:.6g}", entt::get_attribute_as<double>(prop, "max"));
    }
    if(prop.is_const())
    {
        fields += ",\"readOnly\":true";
    }
    auto description = entt::get_attribute_as<std::string>(prop, "tooltip");
    if(codec != nullptr && codec->format_hint != nullptr && !prop.is_const())
    {
        description += description.empty() ? codec->format_hint : std::string(" (") + codec->format_hint + ")";
    }
    if(!description.empty())
    {
        fields += ",\"description\":" + make_json_string(description);
    }
    return fields;
}

void append_meta_schema(const entt::meta_type& type,
                        const std::string& component,
                        std::string& json,
                        bool& first,
                        const std::unordered_set<std::string>* only)
{
    for_each_meta_property(type,
                           [&](const entt::meta_data& prop)
                           {
                               const auto name = entt::get_name(prop);
                               if(name.empty() || (only != nullptr && only->count(name) == 0))
                               {
                                   return;
                               }
                               json += fmt::format(R"({}{{"component":{},"name":{},{}}})",
                                                   first ? "" : ",",
                                                   make_json_string(component),
                                                   make_json_string(name),
                                                   meta_property_schema(prop));
                               first = false;
                           });
}

} // namespace unravel::mcp
