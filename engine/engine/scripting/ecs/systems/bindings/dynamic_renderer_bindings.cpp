#include "script_glue_common.h"
#include "script_bindings.h"
#include "dynamic_renderer_script_helpers.h"
#include "gradient_script_helpers.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/rendering/ecs/components/line_component.h>
#include <engine/rendering/ecs/components/trail_component.h>

#include <algorithm>
#include <cmath>

namespace unravel
{

auto find_dynamic_renderer(entt::entity id, uint32_t slot) -> dynamic_renderer*
{
    if(slot == LINE_MATERIAL_INSTANCE_SLOT)
    {
        return safe_get_component<line_component>(id);
    }
    if(slot == TRAIL_MATERIAL_INSTANCE_SLOT)
    {
        return safe_get_component<trail_component>(id);
    }
    return nullptr;
}

auto find_dynamic_renderer_strip(entt::entity id, uint32_t slot) -> strip_style*
{
    if(slot == LINE_MATERIAL_INSTANCE_SLOT)
    {
        auto comp = safe_get_component<line_component>(id);
        return comp ? &comp->strip : nullptr;
    }
    if(slot == TRAIL_MATERIAL_INSTANCE_SLOT)
    {
        auto comp = safe_get_component<trail_component>(id);
        return comp ? &comp->strip : nullptr;
    }
    return nullptr;
}

namespace
{
/// Whether every coordinate of a value a script passed is a number: one that is not is ignored.
auto is_script_number(float value) -> bool
{
    return std::isfinite(value);
}

auto is_script_number(const math::vec3& value) -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// ---------------------------------------------------------------------------------
// What a Line and a Trail share (DynamicRendererComponent)
// ---------------------------------------------------------------------------------

/// The renderer's material as scripts name it: its asset's id.
auto internal_m2n_dynamic_renderer_get_material(entt::entity id, uint32_t slot) -> hpp::uuid
{
    if(const auto* renderer = find_dynamic_renderer(id, slot))
    {
        return renderer->get_material().uid();
    }
    return {};
}

void internal_m2n_dynamic_renderer_set_material(entt::entity id, uint32_t slot, const hpp::uuid& uid)
{
    if(auto* renderer = find_dynamic_renderer(id, slot))
    {
        auto& am = engine::context().get_cached<asset_manager>();
        renderer->set_material(uid.is_nil() ? asset_handle<material>{} : am.get_asset<material>(uid));
    }
}

auto internal_m2n_dynamic_renderer_get_sort_priority(entt::entity id, uint32_t slot) -> int32_t
{
    const auto* renderer = find_dynamic_renderer(id, slot);
    return renderer != nullptr ? renderer->sort_priority : 0;
}

void internal_m2n_dynamic_renderer_set_sort_priority(entt::entity id, uint32_t slot, int32_t priority)
{
    if(auto* renderer = find_dynamic_renderer(id, slot))
    {
        renderer->sort_priority = priority;
    }
}

auto internal_m2n_dynamic_renderer_get_casts_shadow(entt::entity id, uint32_t slot) -> bool
{
    const auto* renderer = find_dynamic_renderer(id, slot);
    return renderer != nullptr && renderer->casts_shadow;
}

void internal_m2n_dynamic_renderer_set_casts_shadow(entt::entity id, uint32_t slot, bool value)
{
    if(auto* renderer = find_dynamic_renderer(id, slot))
    {
        renderer->casts_shadow = value;
    }
}

// ---------------------------------------------------------------------------------
// The strip of a Line and of a Trail (StripComponent)
// ---------------------------------------------------------------------------------

/// A whole-number setting of a strip as scripts name it: the managed StripSetting enum, in its order.
enum class strip_setting : int32_t
{
    shape,
    alignment,
    uv_mode,
    subdivisions,
    corner_vertices,
    cap_vertices,
    tube_sides,
};

/// A number of a strip as scripts name it: the managed StripNumber enum, in its order.
enum class strip_number : int32_t
{
    width,
    tile_length,
};

auto internal_m2n_strip_get_setting(entt::entity id, uint32_t slot, int32_t setting) -> int32_t
{
    const auto* strip = find_dynamic_renderer_strip(id, slot);
    if(strip == nullptr)
    {
        return 0;
    }
    switch(strip_setting(setting))
    {
        case strip_setting::shape:
            return int32_t(strip->shape);
        case strip_setting::alignment:
            return int32_t(strip->alignment);
        case strip_setting::uv_mode:
            return int32_t(strip->uv_mode);
        case strip_setting::subdivisions:
            return int32_t(strip->subdivisions);
        case strip_setting::corner_vertices:
            return int32_t(strip->corner_vertices);
        case strip_setting::cap_vertices:
            return int32_t(strip->cap_vertices);
        case strip_setting::tube_sides:
            return int32_t(strip->tube_sides);
    }
    return 0;
}

/// Sets a strip's setting, held to the range its Inspector row has. A value an enum does not have is ignored.
void internal_m2n_strip_set_setting(entt::entity id, uint32_t slot, int32_t setting, int32_t value)
{
    auto* strip = find_dynamic_renderer_strip(id, slot);
    if(strip == nullptr)
    {
        return;
    }
    const auto added = uint32_t(std::clamp(value, 0, int32_t(STRIP_MAX_ADDED_VERTICES)));
    switch(strip_setting(setting))
    {
        case strip_setting::shape:
            if(value >= int32_t(strip_shape::flat) && value <= int32_t(strip_shape::tube))
            {
                strip->shape = strip_shape(value);
            }
            break;
        case strip_setting::alignment:
            if(value >= int32_t(strip_alignment::camera) && value <= int32_t(strip_alignment::world_axis))
            {
                strip->alignment = strip_alignment(value);
            }
            break;
        case strip_setting::uv_mode:
            if(value >= int32_t(strip_uv_mode::stretch) && value <= int32_t(strip_uv_mode::per_segment))
            {
                strip->uv_mode = strip_uv_mode(value);
            }
            break;
        case strip_setting::subdivisions:
            strip->subdivisions = added;
            break;
        case strip_setting::corner_vertices:
            strip->corner_vertices = added;
            break;
        case strip_setting::cap_vertices:
            strip->cap_vertices = added;
            break;
        case strip_setting::tube_sides:
            strip->tube_sides = uint32_t(std::clamp(value, int32_t(STRIP_MIN_TUBE_SIDES), int32_t(STRIP_MAX_TUBE_SIDES)));
            break;
    }
}

auto internal_m2n_strip_get_number(entt::entity id, uint32_t slot, int32_t number) -> float
{
    const auto* strip = find_dynamic_renderer_strip(id, slot);
    if(strip == nullptr)
    {
        return 0.0f;
    }
    return strip_number(number) == strip_number::width ? strip->width : strip->tile_length;
}

void internal_m2n_strip_set_number(entt::entity id, uint32_t slot, int32_t number, float value)
{
    auto* strip = find_dynamic_renderer_strip(id, slot);
    if(strip == nullptr || !is_script_number(value))
    {
        return;
    }
    if(strip_number(number) == strip_number::width)
    {
        strip->width = std::max(value, 0.0f);
    }
    else
    {
        strip->tile_length = std::max(value, STRIP_MIN_TILE_LENGTH);
    }
}

auto internal_m2n_strip_get_axis(entt::entity id, uint32_t slot) -> math::vec3
{
    const auto* strip = find_dynamic_renderer_strip(id, slot);
    return strip != nullptr ? strip->axis : math::vec3(0.0f, 1.0f, 0.0f);
}

void internal_m2n_strip_set_axis(entt::entity id, uint32_t slot, const math::vec3& axis)
{
    auto* strip = find_dynamic_renderer_strip(id, slot);
    if(strip != nullptr && is_script_number(axis))
    {
        strip->axis = axis;
    }
}

auto internal_m2n_strip_get_width_over_length(entt::entity id, uint32_t slot) -> std::vector<curve_value_key>
{
    const auto* strip = find_dynamic_renderer_strip(id, slot);
    return strip != nullptr ? to_value_keys(strip->width_over_length) : std::vector<curve_value_key>{};
}

void internal_m2n_strip_set_width_over_length(entt::entity id, uint32_t slot, const std::vector<curve_value_key>& keys)
{
    if(auto* strip = find_dynamic_renderer_strip(id, slot))
    {
        strip->width_over_length = to_value_curve(keys, strip->width_over_length);
    }
}

auto internal_m2n_strip_get_color_over_length(entt::entity id, uint32_t slot) -> std::vector<gradient_key>
{
    const auto* strip = find_dynamic_renderer_strip(id, slot);
    return strip != nullptr ? to_color_keys(strip->color_over_length) : std::vector<gradient_key>{};
}

void internal_m2n_strip_set_color_over_length(entt::entity id, uint32_t slot, const std::vector<gradient_key>& keys)
{
    if(auto* strip = find_dynamic_renderer_strip(id, slot))
    {
        strip->color_over_length = to_color_gradient(keys, strip->color_over_length);
    }
}

// ---------------------------------------------------------------------------------
// Line
// ---------------------------------------------------------------------------------

/// The line's points as scripts pass them: three numbers a point.
auto internal_m2n_line_get_points(entt::entity id) -> std::vector<float>
{
    std::vector<float> values;
    if(auto comp = safe_get_component<line_component>(id))
    {
        values.reserve(comp->points.size() * 3);
        for(const auto& point : comp->points)
        {
            values.push_back(point.x);
            values.push_back(point.y);
            values.push_back(point.z);
        }
    }
    return values;
}

void internal_m2n_line_set_points(entt::entity id, const std::vector<float>& values)
{
    if(auto comp = safe_get_component<line_component>(id))
    {
        // Every point is kept, as the Inspector keeps it: a build draws the first MAX_POINTS of them.
        const size_t count = values.size() / 3;
        comp->points.clear();
        comp->points.reserve(count);
        for(size_t i = 0; i < count; ++i)
        {
            comp->points.emplace_back(values[i * 3], values[i * 3 + 1], values[i * 3 + 2]);
        }
    }
}

auto internal_m2n_line_get_point_count(entt::entity id) -> int32_t
{
    if(auto comp = safe_get_component<line_component>(id))
    {
        return int32_t(comp->points.size());
    }
    return 0;
}

void internal_m2n_line_set_point(entt::entity id, int32_t index, const math::vec3& point)
{
    if(auto comp = safe_get_component<line_component>(id))
    {
        if(index >= 0 && size_t(index) < comp->points.size() && is_script_number(point))
        {
            comp->points[size_t(index)] = point;
        }
    }
}

auto internal_m2n_line_get_point(entt::entity id, int32_t index) -> math::vec3
{
    if(auto comp = safe_get_component<line_component>(id))
    {
        if(index >= 0 && size_t(index) < comp->points.size())
        {
            return comp->points[size_t(index)];
        }
    }
    return {};
}

/// The on / off settings of a line, by LineComponent.Flag.
auto internal_m2n_line_get_flag(entt::entity id, int32_t flag) -> bool
{
    if(auto comp = safe_get_component<line_component>(id))
    {
        return flag == 0 ? comp->world_space : comp->loop;
    }
    return false;
}

void internal_m2n_line_set_flag(entt::entity id, int32_t flag, bool value)
{
    if(auto comp = safe_get_component<line_component>(id))
    {
        (flag == 0 ? comp->world_space : comp->loop) = value;
    }
}

// ---------------------------------------------------------------------------------
// Trail
// ---------------------------------------------------------------------------------

/// The numbers of a trail, by TrailComponent.Field: 0 its Time, 1 its Min Distance.
auto internal_m2n_trail_get_float(entt::entity id, int32_t field) -> float
{
    if(auto comp = safe_get_component<trail_component>(id))
    {
        return field == 0 ? comp->time : comp->min_distance;
    }
    return 0.0f;
}

void internal_m2n_trail_set_float(entt::entity id, int32_t field, float value)
{
    auto comp = safe_get_component<trail_component>(id);
    if(comp && is_script_number(value))
    {
        (field == 0 ? comp->time : comp->min_distance) = std::max(value, 0.0f);
    }
}

/// The on / off settings of a trail, by TrailComponent.Flag.
auto internal_m2n_trail_get_flag(entt::entity id, int32_t flag) -> bool
{
    if(auto comp = safe_get_component<trail_component>(id))
    {
        return flag == 0 ? comp->emitting : comp->edge;
    }
    return false;
}

void internal_m2n_trail_set_flag(entt::entity id, int32_t flag, bool value)
{
    if(auto comp = safe_get_component<trail_component>(id))
    {
        (flag == 0 ? comp->emitting : comp->edge) = value;
    }
}

/// The ends of a trail's edge, by index: 0 its base, 1 its tip.
auto internal_m2n_trail_get_edge(entt::entity id, int32_t end) -> math::vec3
{
    if(auto comp = safe_get_component<trail_component>(id))
    {
        return end == 0 ? comp->edge_base : comp->edge_tip;
    }
    return {};
}

void internal_m2n_trail_set_edge(entt::entity id, int32_t end, const math::vec3& point)
{
    auto comp = safe_get_component<trail_component>(id);
    if(comp && is_script_number(point))
    {
        (end == 0 ? comp->edge_base : comp->edge_tip) = point;
    }
}

void internal_m2n_trail_clear(entt::entity id)
{
    if(auto comp = safe_get_component<trail_component>(id))
    {
        comp->clear();
    }
}

auto internal_m2n_trail_get_point_count(entt::entity id) -> int32_t
{
    if(auto comp = safe_get_component<trail_component>(id))
    {
        return int32_t(comp->get_point_count());
    }
    return 0;
}

} // namespace

void register_dynamic_renderer_script_bindings()
{
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.DynamicRendererComponent");
        reg.add_internal_call("internal_m2n_dynamic_renderer_get_material", script_internal_call(internal_m2n_dynamic_renderer_get_material));
        reg.add_internal_call("internal_m2n_dynamic_renderer_set_material", script_internal_call(internal_m2n_dynamic_renderer_set_material));
        reg.add_internal_call("internal_m2n_dynamic_renderer_get_sort_priority", script_internal_call(internal_m2n_dynamic_renderer_get_sort_priority));
        reg.add_internal_call("internal_m2n_dynamic_renderer_set_sort_priority", script_internal_call(internal_m2n_dynamic_renderer_set_sort_priority));
        reg.add_internal_call("internal_m2n_dynamic_renderer_get_casts_shadow", script_internal_call(internal_m2n_dynamic_renderer_get_casts_shadow));
        reg.add_internal_call("internal_m2n_dynamic_renderer_set_casts_shadow", script_internal_call(internal_m2n_dynamic_renderer_set_casts_shadow));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.StripComponent");
        reg.add_internal_call("internal_m2n_strip_get_setting", script_internal_call(internal_m2n_strip_get_setting));
        reg.add_internal_call("internal_m2n_strip_set_setting", script_internal_call(internal_m2n_strip_set_setting));
        reg.add_internal_call("internal_m2n_strip_get_number", script_internal_call(internal_m2n_strip_get_number));
        reg.add_internal_call("internal_m2n_strip_set_number", script_internal_call(internal_m2n_strip_set_number));
        reg.add_internal_call("internal_m2n_strip_get_axis", script_internal_call(internal_m2n_strip_get_axis));
        reg.add_internal_call("internal_m2n_strip_set_axis", script_internal_call(internal_m2n_strip_set_axis));
        reg.add_internal_call("internal_m2n_strip_get_width_over_length", script_internal_call(internal_m2n_strip_get_width_over_length));
        reg.add_internal_call("internal_m2n_strip_set_width_over_length", script_internal_call(internal_m2n_strip_set_width_over_length));
        reg.add_internal_call("internal_m2n_strip_get_color_over_length", script_internal_call(internal_m2n_strip_get_color_over_length));
        reg.add_internal_call("internal_m2n_strip_set_color_over_length", script_internal_call(internal_m2n_strip_set_color_over_length));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.LineComponent");
        reg.add_internal_call("internal_m2n_line_get_points", script_internal_call(internal_m2n_line_get_points));
        reg.add_internal_call("internal_m2n_line_set_points", script_internal_call(internal_m2n_line_set_points));
        reg.add_internal_call("internal_m2n_line_get_point_count", script_internal_call(internal_m2n_line_get_point_count));
        reg.add_internal_call("internal_m2n_line_get_point", script_internal_call(internal_m2n_line_get_point));
        reg.add_internal_call("internal_m2n_line_set_point", script_internal_call(internal_m2n_line_set_point));
        reg.add_internal_call("internal_m2n_line_get_flag", script_internal_call(internal_m2n_line_get_flag));
        reg.add_internal_call("internal_m2n_line_set_flag", script_internal_call(internal_m2n_line_set_flag));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.TrailComponent");
        reg.add_internal_call("internal_m2n_trail_get_float", script_internal_call(internal_m2n_trail_get_float));
        reg.add_internal_call("internal_m2n_trail_set_float", script_internal_call(internal_m2n_trail_set_float));
        reg.add_internal_call("internal_m2n_trail_get_flag", script_internal_call(internal_m2n_trail_get_flag));
        reg.add_internal_call("internal_m2n_trail_set_flag", script_internal_call(internal_m2n_trail_set_flag));
        reg.add_internal_call("internal_m2n_trail_get_edge", script_internal_call(internal_m2n_trail_get_edge));
        reg.add_internal_call("internal_m2n_trail_set_edge", script_internal_call(internal_m2n_trail_set_edge));
        reg.add_internal_call("internal_m2n_trail_clear", script_internal_call(internal_m2n_trail_clear));
        reg.add_internal_call("internal_m2n_trail_get_point_count", script_internal_call(internal_m2n_trail_get_point_count));
    }
}

} // namespace unravel
