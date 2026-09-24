#include "gpu_light_buffer.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/light_component.h>

namespace unravel
{
namespace
{
namespace ANONYMOUS
{
/// Layout of the light buffer: a flat array of vec4, matching BUFFER_RO(_, vec4, _).
auto get_vec4_buffer_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout decl;
        decl.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
        return decl;
    }();
    return layout;
}
}

auto to_gpu_light_type(light_type type) -> gpu_light_buffer::gpu_light_type
{
    switch(type)
    {
        case light_type::point:
            return gpu_light_buffer::gpu_light_type::point;
        case light_type::directional:
            return gpu_light_buffer::gpu_light_type::directional;
        case light_type::spot:
        default:
            return gpu_light_buffer::gpu_light_type::spot;
    }
}

} // namespace

auto gpu_light_buffer::init() -> bool
{
    shutdown();
    // One light's worth up front so the handle is always valid to bind, even with no lights.
    ensure_capacity(light_vec4_stride);
    return is_valid();
}

void gpu_light_buffer::shutdown()
{
    if(bgfx::isValid(buffer_))
    {
        bgfx::destroy(buffer_);
        buffer_ = {bgfx::kInvalidHandle};
    }
    capacity_vec4_ = 0;
    light_count_ = 0;
    data_.clear();
}

namespace
{
/// Offsets into a packed light record (see update): type, range, colour, intensity.
constexpr size_t record_type = 3;
constexpr size_t record_range = 7;
constexpr size_t record_color = 8;
constexpr size_t record_intensity = 11;
/// Rec. 709 luminance weights for a light's brightness.
constexpr float luminance_r = 0.2126f;
constexpr float luminance_g = 0.7152f;
constexpr float luminance_b = 0.0722f;

auto is_directional(const std::array<float, 16>& record) -> bool
{
    return static_cast<uint32_t>(record[record_type]) ==
           static_cast<uint32_t>(gpu_light_buffer::gpu_light_type::directional);
}

auto light_brightness(const std::array<float, 16>& record) -> float
{
    return (luminance_r * record[record_color] + luminance_g * record[record_color + 1] +
            luminance_b * record[record_color + 2]) *
           record[record_intensity];
}

auto influence_bounds(const std::array<float, 16>& record) -> math::bbox
{
    const math::vec3 position(record[0], record[1], record[2]);
    const math::vec3 reach(math::max(record[record_range], 0.0f));
    math::bbox bounds;
    bounds.min = position - reach;
    bounds.max = position + reach;
    return bounds;
}

/// True when two brightnesses differ by more than gpu_light_buffer::global_change_ratio; a
/// light switched on or off always does.
auto is_global_brightness_change(float before, float after) -> bool
{
    const float low = math::min(before, after);
    const float high = math::max(before, after);
    if(high <= 0.0f)
    {
        return false;
    }
    return low <= 0.0f || high > gpu_light_buffer::global_change_ratio * low;
}
} // namespace

void gpu_light_buffer::classify_changes()
{
    local_changes_.clear();
    bool global = false;
    for(const auto& [id, record] : current_lights_)
    {
        const auto previous = previous_lights_.find(id);
        if(previous == previous_lights_.end())
        {
            if(is_directional(record))
            {
                global = true;
            }
            else
            {
                local_changes_.push_back({id, influence_bounds(record)});
            }
            continue;
        }
        const auto& before = previous->second;
        if(before == record)
        {
            continue;
        }
        const bool was_directional = is_directional(before);
        const bool now_directional = is_directional(record);
        if(was_directional && now_directional)
        {
            global = global || is_global_brightness_change(light_brightness(before), light_brightness(record));
            continue;
        }
        global = global || was_directional || now_directional;
        if(!was_directional)
        {
            local_changes_.push_back({id, influence_bounds(before)});
        }
        if(!now_directional)
        {
            local_changes_.push_back({id, influence_bounds(record)});
        }
    }
    for(const auto& [id, record] : previous_lights_)
    {
        if(current_lights_.count(id) != 0)
        {
            continue;
        }
        if(is_directional(record))
        {
            global = true;
        }
        else
        {
            local_changes_.push_back({id, influence_bounds(record)});
        }
    }
    if(global)
    {
        ++global_revision_;
    }
    previous_lights_.swap(current_lights_);
    current_lights_.clear();
}

void gpu_light_buffer::ensure_capacity(uint32_t required_vec4)
{
    if(bgfx::isValid(buffer_) && required_vec4 <= capacity_vec4_)
    {
        return;
    }
    if(bgfx::isValid(buffer_))
    {
        bgfx::destroy(buffer_);
    }
    // Dynamic buffers cannot grow through update() -- a write past the allocated size is
    // silently dropped and the shader reads zeros -- so capacity is tracked and the buffer
    // recreated, with slack so a scene gaining lights does not recreate it every frame.
    capacity_vec4_ = required_vec4 + required_vec4 / 2u + light_vec4_stride * 16u;
    buffer_ = bgfx::createDynamicVertexBuffer(capacity_vec4_, ANONYMOUS::get_vec4_buffer_layout(),
                                              BGFX_BUFFER_COMPUTE_READ);
    // A fresh buffer holds nothing yet, whatever the content hash says.
    buffer_uploaded_ = false;
}

void gpu_light_buffer::update(scene& scn)
{
    APP_SCOPE_PERF("Rendering/GPU Light Buffer");
    data_.clear();
    light_count_ = 0;
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto entity, auto&& transform_comp, auto&& light_comp, auto&& active)
        {
            const auto& light = light_comp.get_light();
            // Scale must not leak into a light's transform: only its position and orientation
            // are meaningful, and a scaled parent would otherwise skew the direction axis.
            auto world_transform = transform_comp.get_transform_global();
            world_transform.reset_scale();
            const auto position = world_transform.get_position();
            const auto direction = world_transform.z_unit_axis();
            const auto type = to_gpu_light_type(light.type);
            float range = 0.0f;
            float cos_inner = 0.0f;
            float cos_outer = 0.0f;
            float falloff_exponent = 1.0f;
            if(light.type == light_type::point)
            {
                range = light.point_data.range;
                falloff_exponent = light.point_data.exponent_falloff;
            }
            else if(light.type == light_type::spot)
            {
                range = light.spot_data.get_range();
                // Half angles, matching what the per-light direct shaders are given.
                cos_inner = math::cos(math::radians(light.spot_data.get_inner_angle() * 0.5f));
                cos_outer = math::cos(math::radians(light.spot_data.get_outer_angle() * 0.5f));
            }
            const size_t base = data_.size();
            data_.resize(base + size_t(light_vec4_stride) * 4u, 0.0f);
            float* dst = data_.data() + base;
            dst[0] = position.x;
            dst[1] = position.y;
            dst[2] = position.z;
            dst[3] = float(static_cast<uint32_t>(type));
            dst[4] = direction.x;
            dst[5] = direction.y;
            dst[6] = direction.z;
            dst[7] = range;
            // Same linear decode as the deferred direct-lighting pass: GI-lit voxels
            // and directly-lit pixels must agree on the light's color.
            const auto light_color_linear = light.color.to_linear();
            dst[8] = light_color_linear.value.r;
            dst[9] = light_color_linear.value.g;
            dst[10] = light_color_linear.value.b;
            dst[11] = light.intensity;
            dst[12] = cos_inner;
            dst[13] = cos_outer;
            dst[14] = falloff_exponent;
            // Reserved for the shadow atlas slot, once shadows are resident. -1 means the
            // light casts no resident shadow and must be treated as unshadowed.
            dst[15] = -1.0f;
            auto& record = current_lights_[static_cast<uint32_t>(entity)];
            for(size_t i = 0; i < record.size(); ++i)
            {
                record[i] = dst[i];
            }
            ++light_count_;
        });
    classify_changes();
    if(data_.empty())
    {
        // An emptied light set is a content change too: without flipping the hash, the last
        // populated frame's value would linger and the probe fast-window (and the gate below,
        // if lights later return unchanged) would read "nothing changed".
        content_hash_ = 1469598103934665603ull;
        return;
    }
    ensure_capacity(uint32_t(data_.size() / 4u));
    // FNV-1a over the exact bytes the GPU receives: any light property change flips the hash,
    // which is what the world probes key their fast-refresh window on.
    uint64_t hash = 1469598103934665603ull;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data_.data());
    for(size_t i = 0; i < data_.size() * sizeof(float); ++i)
    {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    // The hash it just computed also gates the upload: a static light set re-staged the whole
    // buffer every frame - on Vulkan that is continuous staging-allocator churn for identical
    // bytes (the same waste upload_instance_grid was already gated against).
    if(hash == content_hash_ && buffer_uploaded_)
    {
        return;
    }
    content_hash_ = hash;
    buffer_uploaded_ = true;
    bgfx::update(buffer_, 0, bgfx::copy(data_.data(), uint32_t(data_.size() * sizeof(float))));
}

} // namespace unravel
