#include "particle_data_texture.h"
#include "particle_gpu_schema.h"

#include <engine/rendering/gpu_resource_utils.h>
#include <engine/rendering/render_feature_support.h>
#include <graphics/shader.h>
#include <logging/logging.h>

#include <bimg/bimg.h>

#include <algorithm>

namespace unravel
{
namespace ps_soa
{
namespace
{

/// Read with texelFetch only, never filtered: 32-bit integer and float formats do not filter on every device.
constexpr uint64_t k_data_texture_flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
constexpr uint32_t k_bits_per_byte = 8;
/// The name the data texture variants of vs_particle.eshader add to the variant they stand in for.
constexpr const char* k_texture_variant = "textures";

} // namespace

auto uses_data_textures() -> bool
{
    return !get_render_feature_support().is_available(render_feature::particle_storage_buffers);
}

auto get_particle_vertex_variant(const std::string& variant) -> std::string
{
    if(!uses_data_textures())
    {
        return variant;
    }
    return variant == gfx::shader::DEFAULT_VARIANT ? std::string(k_texture_variant) : variant + "_" + k_texture_variant;
}

data_texture::data_texture(bgfx::TextureFormat::Enum format)
    : format_(format)
    , texel_bytes_(bimg::getBitsPerPixel(bimg::TextureFormat::Enum(format)) / k_bits_per_byte)
{
}

auto data_texture::reserve(uint32_t texels) -> bool
{
    const uint32_t rows = std::max((texels + data_texture_width - 1) / data_texture_width, 1u);
    if(bgfx::isValid(handle_) && rows <= rows_)
    {
        return false;
    }
    destroy();
    const uint32_t max_rows = bgfx::getCaps()->limits.maxTextureSize;
    if(rows > max_rows)
    {
        APPLOG_ERROR("Particles: {} texels of particle data need {} texture rows, and this device makes at most {}.",
                     texels,
                     rows,
                     max_rows);
        return false;
    }
    handle_ = bgfx::createTexture2D(uint16_t(data_texture_width), uint16_t(rows), false, 1, format_, k_data_texture_flags);
    rows_ = rows;
    return true;
}

void data_texture::update(uint32_t first, uint32_t count, const void* data) const
{
    const uint32_t capacity = get_capacity();
    count = std::min(count, capacity - std::min(first, capacity));
    const auto* bytes = static_cast<const uint8_t*>(data);
    while(count > 0)
    {
        // Whole rows go up as one rectangle, the part of a row before or after them on its own.
        const uint32_t column = first % data_texture_width;
        const bool is_whole_rows = column == 0 && count >= data_texture_width;
        const uint32_t width = is_whole_rows ? data_texture_width : std::min(count, data_texture_width - column);
        const uint32_t height = is_whole_rows ? count / data_texture_width : 1u;
        const uint32_t texels = width * height;
        bgfx::updateTexture2D(handle_,
                              0,
                              0,
                              uint16_t(column),
                              uint16_t(first / data_texture_width),
                              uint16_t(width),
                              uint16_t(height),
                              bgfx::copy(bytes, texels * texel_bytes_));
        bytes += size_t(texels) * texel_bytes_;
        first += texels;
        count -= texels;
    }
}

auto data_texture::is_valid() const -> bool
{
    return bgfx::isValid(handle_);
}

auto data_texture::get_handle() const -> bgfx::TextureHandle
{
    return handle_;
}

void data_texture::destroy()
{
    gpu_resource::destroy_handle(handle_);
    rows_ = 0;
}

auto data_texture::get_capacity() const -> uint32_t
{
    return bgfx::isValid(handle_) ? rows_ * data_texture_width : 0u;
}

} // namespace ps_soa
} // namespace unravel
