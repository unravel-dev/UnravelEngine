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
/// FNV-1a 64-bit offset basis and prime.
constexpr uint64_t fnv_offset_basis = 1469598103934665603ull;
constexpr uint64_t fnv_prime = 1099511628211ull;
} // namespace

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
        [&](auto /*entity*/, auto&& transform_comp, auto&& light_comp, auto&& /*active*/)
        {
            const auto& light = light_comp.get_light();
            // A light the GI leaves out takes no record (UE skips lights whose indirect intensity is 0).
            if(light.indirect_intensity <= 0.0f)
            {
                return;
            }
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
            // Same linear decode as the deferred direct-lighting pass: the surface cache's
            // lighting and directly-lit pixels must agree on the light's color.
            const auto light_color_linear = light.color.to_linear();
            dst[8] = light_color_linear.value.r;
            dst[9] = light_color_linear.value.g;
            dst[10] = light_color_linear.value.b;
            dst[11] = light.intensity * light.indirect_intensity;
            dst[12] = cos_inner;
            dst[13] = cos_outer;
            dst[14] = falloff_exponent;
            // 1 when the light casts shadows: GI traces their visibility, as the direct lighting pass shadows them.
            dst[15] = light.casts_shadows ? 1.0f : 0.0f;
            ++light_count_;
        });
    if(data_.empty())
    {
        // Nothing to upload; the reset hash makes the next populated frame upload whatever it holds.
        content_hash_ = fnv_offset_basis;
        return;
    }
    ensure_capacity(uint32_t(data_.size() / 4u));
    // FNV-1a over the exact bytes the GPU receives gates the upload: re-staging an unchanged
    // light set every frame is, on Vulkan, continuous staging-allocator churn for identical bytes.
    uint64_t hash = fnv_offset_basis;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data_.data());
    for(size_t i = 0; i < data_.size() * sizeof(float); ++i)
    {
        hash = (hash ^ bytes[i]) * fnv_prime;
    }
    if(hash == content_hash_ && buffer_uploaded_)
    {
        return;
    }
    content_hash_ = hash;
    buffer_uploaded_ = true;
    bgfx::update(buffer_, 0, bgfx::copy(data_.data(), uint32_t(data_.size() * sizeof(float))));
}

} // namespace unravel
