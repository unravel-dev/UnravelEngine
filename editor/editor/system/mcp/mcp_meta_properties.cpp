#include "mcp_meta_properties.h"

#include "mcp_tools_common.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/prefab.h>
#include <engine/layers/layer_mask.h>
#include <engine/physics/physics_material.h>
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
#include <optional>
#include <unordered_map>

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
                    return fmt::format("{:.6g}", value.cast<T>());
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
                    if constexpr(std::is_unsigned_v<T>)
                    {
                        if(number < 0.0)
                        {
                            error = "expected a non-negative integer";
                            return false;
                        }
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
    return {.schema_type = "array",
            .schema_extra = R"("items":"number","minItems":2,"maxItems":2)",
            .format_hint = "[min, max]",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto& bounds = value.cast<const range<T>&>();
                return fmt::format("[{:.6g},{:.6g}]", bounds.min, bounds.max);
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
                static const auto element_codec = make_number_codec<T>("number");
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
            .schema_extra = is_float ? R"("items":"number","minItems":2,"maxItems":2)"
                                     : R"("items":"integer","minItems":2,"maxItems":2)",
            .format_hint = "[width, height]",
            .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
            {
                const auto& extent = value.cast<const size<T>&>();
                if constexpr(is_float)
                {
                    return fmt::format("[{:.6g},{:.6g}]", extent.width, extent.height);
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

/// How one key of a curve of T spells its value: the fields after "progress".
template<typename T>
struct curve_key_json;

template<>
struct curve_key_json<float>
{
    static constexpr const char* format = "[{progress, value}]";

    static auto to_fields(float value) -> std::string
    {
        return fmt::format(R"("value":{:.6g})", value);
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
        return fmt::format(R"("min":{:.6g},"max":{:.6g})", value.min, value.max);
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
        return R"("color":)" + color_to_json(value);
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
        return R"("value":)" + vec3_to_json(value);
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
                    json += fmt::format(R"({}{{"progress":{:.6g},{}}})",
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
                return fmt::format("{:.6g}", value.cast<seconds>().count());
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
                auto& am = ctx.get_cached<asset_manager>();
                auto handle = [&]
                {
                    if(auto as_uuid = hpp::uuid::from_string(key))
                    {
                        return am.get_asset<T>(*as_uuid);
                    }
                    return am.get_asset<T>(key);
                }();
                if(!handle)
                {
                    error = "asset not found: " + key;
                    return false;
                }
                out = entt::meta_any{handle};
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
             .schema_extra = R"("items":"number","minItems":2,"maxItems":2)",
             .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
             {
                 return vec2_to_json(value.cast<math::vec2>());
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
             .schema_extra = R"("items":"number","minItems":3,"maxItems":3)",
             .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
             {
                 return vec3_to_json(value.cast<math::vec3>());
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
             .schema_extra = R"("items":"number","minItems":3,"maxItems":4)",
             .to_json = [](rtti::context&, const entt::meta_any& value) -> std::optional<std::string>
             {
                 return color_to_json(value.cast<math::color>());
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
        add(entt::type_id<std::chrono::duration<float>>(), make_seconds_codec());
        return result;
    }();
    return table;
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
    return type.data().begin() != type.data().end();
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
    std::string_view name;
    if(element.get_string().get(name) != simdjson::SUCCESS)
    {
        error = "expected one of " + fmt::format("{}", fmt::join(enum_value_names(type), ", "));
        return false;
    }
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
    error = "\"" + std::string(name) + "\" is not one of " + fmt::format("{}", fmt::join(enum_value_names(type), ", "));
    return false;
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
    if(is_nested_object(type))
    {
        return "object";
    }
    return "unsupported";
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
    std::string_view word;
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
        std::string_view name;
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
    bool first = true;
    for(auto [id, prop] : object.type().data())
    {
        const auto name = entt::get_name(prop);
        if(name.empty() || (only != nullptr && only->count(name) == 0) ||
           (!include_hidden && !entt::is_property_visible(object, prop)))
        {
            continue;
        }
        auto value = prop.get(object);
        if(!value)
        {
            continue;
        }
        const auto value_json = value_to_json(ctx, value);
        if(!value_json)
        {
            continue;
        }
        if(!first)
        {
            json += ",";
        }
        first = false;
        json += make_json_string(name) + ":" + *value_json;
    }
    json += "}";
    return json;
}

void apply_meta_properties(rtti::context& ctx,
                           entt::meta_any& object,
                           const simdjson::dom::object& properties,
                           component_apply_result& result,
                           const std::unordered_set<std::string>* only)
{
    const auto type = object.type();
    for(auto field : properties)
    {
        const std::string key(field.key);
        auto fail = [&](const std::string& message)
        {
            result.ok = false;
            result.errors.push_back(key + ": " + message);
        };
        const auto prop = find_meta_property(type, key);
        if(!prop || (only != nullptr && only->count(key) == 0))
        {
            if(const auto renamed = entt::find_renamed_property(type, key))
            {
                fail("renamed " + entt::get_name(renamed));
                continue;
            }
            result.unknown.push_back(key);
            continue;
        }
        if(is_read_only(object, prop))
        {
            fail("read-only");
            continue;
        }

        const auto target = prop.type();
        simdjson::dom::object nested;
        if(is_nested_object(target) && field.value.get_object().get(nested) == simdjson::SUCCESS)
        {
            // Partial objects merge into the current value, then the whole struct goes back
            // through the property's setter, as an inspector edit of one sub-field does.
            auto current = prop.get(object);
            component_apply_result nested_result;
            apply_meta_properties(ctx, current, nested, nested_result);
            for(const auto& error : nested_result.errors)
            {
                fail(error);
            }
            for(const auto& unknown : nested_result.unknown)
            {
                result.unknown.push_back(key + "." + unknown);
            }
            if(nested_result.ok && nested_result.unknown.empty())
            {
                prop.set(object, current) ? result.applied.push_back(key) : fail("the setter rejected the value");
            }
            continue;
        }

        entt::meta_any converted;
        std::string error;
        if(const auto* codec = find_codec(target))
        {
            converted = prop.get(object);
            if(!codec->from_json(ctx, field.value, converted, error))
            {
                fail(error);
                continue;
            }
            if(codec->as_double && !check_range(prop, codec->as_double(converted), error))
            {
                fail(error);
                continue;
            }
        }
        else if(target.is_enum())
        {
            if(!enum_from_json(field.value, target, converted, error))
            {
                fail(error);
                continue;
            }
        }
        else
        {
            fail(fmt::format("type {} cannot be set through MCP", entt::get_pretty_name(target)));
            continue;
        }

        prop.set(object, converted) ? result.applied.push_back(key) : fail("the setter rejected the value");
    }
    if(!result.unknown.empty())
    {
        // A typo silently ignored reads as success; reject it so the caller sees the name.
        result.ok = false;
        result.errors.push_back("unknown properties: " + fmt::format("{}", fmt::join(result.unknown, ", ")) +
                                " (see scene_list_component_properties)");
    }
}

auto has_meta_properties(const entt::meta_type& type) -> bool
{
    for(auto [id, prop] : type.data())
    {
        if(!entt::get_name(prop).empty() && std::string_view(schema_type_of(prop.type())) != "unsupported")
        {
            return true;
        }
    }
    return false;
}

auto find_meta_property(const entt::meta_type& type, std::string_view name) -> entt::meta_data
{
    for(auto [id, prop] : type.data())
    {
        if(entt::get_name(prop) == name)
        {
            return prop;
        }
    }
    return {};
}

namespace
{
/// "properties":{name:{schema},...} of a reflected struct, leaving out what the bridge cannot express.
auto object_properties_schema(const entt::meta_type& type) -> std::string
{
    std::string properties;
    for(auto [id, prop] : type.data())
    {
        const auto name = entt::get_name(prop);
        if(name.empty() || std::string_view(schema_type_of(prop.type())) == "unsupported")
        {
            continue;
        }
        properties += (properties.empty() ? "" : ",") + make_json_string(name) + ":{" + meta_property_schema(prop) + "}";
    }
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
    std::string fields = fmt::format(R"("type":{})", make_json_string(schema_type_of(prop_type)));
    const auto* codec = find_codec(prop_type);
    if(codec != nullptr && codec->schema_extra != nullptr)
    {
        fields += std::string(",") + codec->schema_extra;
    }
    if(prop_type.is_enum())
    {
        fields += ",\"enum\":" + strings_to_json_array(enum_value_names(prop_type));
    }
    if(is_nested_object(prop_type))
    {
        fields += "," + object_properties_schema(prop_type);
    }
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
    for(auto [id, prop] : type.data())
    {
        const auto name = entt::get_name(prop);
        if(name.empty() || (only != nullptr && only->count(name) == 0))
        {
            continue;
        }
        if(!first)
        {
            json += ",";
        }
        first = false;
        json += fmt::format(R"({{"component":{},"name":{},{}}})",
                            make_json_string(component),
                            make_json_string(name),
                            meta_property_schema(prop));
    }
}

} // namespace unravel::mcp
