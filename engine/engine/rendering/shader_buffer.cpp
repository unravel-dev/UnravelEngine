#include "shader_buffer.h"

#include <engine/rendering/gpu_resource_utils.h>
#include <engine/rendering/render_feature_support.h>
#include <graphics/shader.h>
#include <logging/logging.h>

#include <bimg/bimg.h>

#include <algorithm>
#include <cassert>
#include <utility>

namespace unravel
{
namespace
{

/// Read with texelFetch only, never filtered: 32-bit integer and float formats do not filter on every device.
constexpr uint64_t TEXTURE_FLAGS = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
/// The name the texture variants add to the variant they stand in for (SHADER_BUFFER_TEXTURES in their defines).
constexpr const char* TEXTURE_VARIANT = "textures";
constexpr uint32_t BITS_PER_BYTE = 8;
constexpr uint32_t UINTS_PER_UINT4 = 4;

auto get_row_count(uint32_t elements) -> uint32_t
{
    return std::max((elements + SHADER_BUFFER_TEXTURE_WIDTH - 1) / SHADER_BUFFER_TEXTURE_WIDTH, 1u);
}

} // namespace

auto get_vertex_shader_buffer_storage() -> shader_buffer_storage
{
    return get_render_feature_support().is_available(render_feature::vertex_storage_buffers)
               ? shader_buffer_storage::buffer
               : shader_buffer_storage::texture;
}

auto get_shader_buffer_variant(const std::string& variant, shader_buffer_storage storage) -> std::string
{
    if(storage == shader_buffer_storage::buffer)
    {
        return variant;
    }
    return variant == gfx::shader::DEFAULT_VARIANT ? std::string(TEXTURE_VARIANT) : variant + "_" + TEXTURE_VARIANT;
}

shader_buffer::shader_buffer(std::string sampler,
                             shader_buffer_element element,
                             shader_buffer_storage storage,
                             uint16_t buffer_flags)
    : sampler_name_(std::move(sampler))
    , element_(element)
    , storage_(storage)
    , buffer_flags_(buffer_flags)
{
}

shader_buffer::shader_buffer(shader_buffer&& other) noexcept
    : sampler_name_(std::move(other.sampler_name_))
    , element_(other.element_)
    , storage_(other.storage_)
    , buffer_flags_(other.buffer_flags_)
    , vertex_buffer_(std::exchange(other.vertex_buffer_, BGFX_INVALID_HANDLE))
    , index_buffer_(std::exchange(other.index_buffer_, BGFX_INVALID_HANDLE))
    , texture_(std::exchange(other.texture_, BGFX_INVALID_HANDLE))
    , sampler_(std::exchange(other.sampler_, BGFX_INVALID_HANDLE))
    , capacity_(std::exchange(other.capacity_, 0u))
{
}

auto shader_buffer::operator=(shader_buffer&& other) noexcept -> shader_buffer&
{
    if(this != &other)
    {
        assert(!is_valid() && !bgfx::isValid(sampler_) && "destroy() a shader buffer before replacing it");
        sampler_name_ = std::move(other.sampler_name_);
        element_ = other.element_;
        storage_ = other.storage_;
        buffer_flags_ = other.buffer_flags_;
        vertex_buffer_ = std::exchange(other.vertex_buffer_, BGFX_INVALID_HANDLE);
        index_buffer_ = std::exchange(other.index_buffer_, BGFX_INVALID_HANDLE);
        texture_ = std::exchange(other.texture_, BGFX_INVALID_HANDLE);
        sampler_ = std::exchange(other.sampler_, BGFX_INVALID_HANDLE);
        capacity_ = std::exchange(other.capacity_, 0u);
    }
    return *this;
}

shader_buffer::~shader_buffer()
{
    assert(!is_valid() && !bgfx::isValid(sampler_) && "destroy() must run before the graphics backend shuts down");
}

void shader_buffer::set_storage(shader_buffer_storage storage)
{
    destroy();
    storage_ = storage;
}

void shader_buffer::init()
{
    if(storage_ == shader_buffer_storage::texture && !bgfx::isValid(sampler_))
    {
        sampler_ = bgfx::createUniform(sampler_name_.c_str(), bgfx::UniformType::Sampler);
    }
}

auto shader_buffer::reserve(uint32_t elements) -> bool
{
    const uint32_t required = storage_ == shader_buffer_storage::texture
                                  ? get_row_count(elements) * SHADER_BUFFER_TEXTURE_WIDTH
                                  : std::max(elements, 1u);
    if(is_valid() && required <= capacity_)
    {
        return false;
    }
    destroy_storage();
    if(storage_ == shader_buffer_storage::texture)
    {
        init();
        create_texture(required);
    }
    else
    {
        create_buffer(required);
    }
    return is_valid();
}

void shader_buffer::create_buffer(uint32_t elements)
{
    if(element_ == shader_buffer_element::float4)
    {
        vertex_buffer_ = bgfx::createDynamicVertexBuffer(elements, gpu_resource::get_vec4_layout(), buffer_flags_);
    }
    else
    {
        index_buffer_ =
            bgfx::createDynamicIndexBuffer(elements * get_uints_per_element(), buffer_flags_ | BGFX_BUFFER_INDEX32);
    }
    capacity_ = elements;
}

void shader_buffer::create_texture(uint32_t elements)
{
    const uint32_t rows = get_row_count(elements);
    const uint32_t max_rows = bgfx::getCaps()->limits.maxTextureSize;
    if(rows > max_rows)
    {
        APPLOG_ERROR("{}: {} elements need {} texture rows, and this device makes at most {}.",
                     sampler_name_,
                     elements,
                     rows,
                     max_rows);
        return;
    }
    texture_ = bgfx::createTexture2D(uint16_t(SHADER_BUFFER_TEXTURE_WIDTH),
                                     uint16_t(rows),
                                     false,
                                     1,
                                     get_texture_format(),
                                     TEXTURE_FLAGS);
    capacity_ = rows * SHADER_BUFFER_TEXTURE_WIDTH;
}

void shader_buffer::update(uint32_t first, uint32_t count, const void* data) const
{
    count = std::min(count, capacity_ - std::min(first, capacity_));
    if(count == 0)
    {
        return;
    }
    const uint32_t element_bytes = get_element_bytes();
    const auto* bytes = static_cast<const uint8_t*>(data);
    if(bgfx::isValid(vertex_buffer_))
    {
        bgfx::update(vertex_buffer_, first, bgfx::copy(bytes, count * element_bytes));
        return;
    }
    if(bgfx::isValid(index_buffer_))
    {
        bgfx::update(index_buffer_, first * get_uints_per_element(), bgfx::copy(bytes, count * element_bytes));
        return;
    }
    while(count > 0)
    {
        // Whole rows go up as one rectangle, the part of a row before or after them on its own.
        const uint32_t column = first % SHADER_BUFFER_TEXTURE_WIDTH;
        const bool is_whole_rows = column == 0 && count >= SHADER_BUFFER_TEXTURE_WIDTH;
        const uint32_t width =
            is_whole_rows ? SHADER_BUFFER_TEXTURE_WIDTH : std::min(count, SHADER_BUFFER_TEXTURE_WIDTH - column);
        const uint32_t height = is_whole_rows ? count / SHADER_BUFFER_TEXTURE_WIDTH : 1u;
        const uint32_t texels = width * height;
        bgfx::updateTexture2D(texture_,
                              0,
                              0,
                              uint16_t(column),
                              uint16_t(first / SHADER_BUFFER_TEXTURE_WIDTH),
                              uint16_t(width),
                              uint16_t(height),
                              bgfx::copy(bytes, texels * element_bytes));
        bytes += size_t(texels) * element_bytes;
        first += texels;
        count -= texels;
    }
}

void shader_buffer::update_by_reference(uint32_t count,
                                        const void* data,
                                        bgfx::ReleaseFn release,
                                        void* user_data) const
{
    count = std::min(count, capacity_);
    const uint32_t element_bytes = get_element_bytes();
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint32_t whole_rows = count / SHADER_BUFFER_TEXTURE_WIDTH;
    const uint32_t referenced = bgfx::isValid(texture_) ? whole_rows * SHADER_BUFFER_TEXTURE_WIDTH : count;
    if(bgfx::isValid(texture_))
    {
        update(referenced, count - referenced, bytes + size_t(referenced) * element_bytes);
    }
    if(referenced == 0 || !is_valid())
    {
        release(const_cast<void*>(data), user_data);
        return;
    }
    const bgfx::Memory* memory = bgfx::makeRef(data, referenced * element_bytes, release, user_data);
    if(bgfx::isValid(vertex_buffer_))
    {
        bgfx::update(vertex_buffer_, 0, memory);
    }
    else if(bgfx::isValid(index_buffer_))
    {
        bgfx::update(index_buffer_, 0, memory);
    }
    else
    {
        bgfx::updateTexture2D(texture_,
                              0,
                              0,
                              0,
                              0,
                              uint16_t(SHADER_BUFFER_TEXTURE_WIDTH),
                              uint16_t(whole_rows),
                              memory);
    }
}

void shader_buffer::bind(uint8_t stage) const
{
    if(bgfx::isValid(vertex_buffer_))
    {
        bgfx::setBuffer(stage, vertex_buffer_, bgfx::Access::Read);
    }
    else if(bgfx::isValid(index_buffer_))
    {
        bgfx::setBuffer(stage, index_buffer_, bgfx::Access::Read);
    }
    else if(bgfx::isValid(texture_))
    {
        bgfx::setTexture(stage, sampler_, texture_);
    }
}

void shader_buffer::bind_compute(uint8_t stage, bgfx::Access::Enum access) const
{
    assert(storage_ == shader_buffer_storage::buffer && "compute shaders read and write the buffer storage only");
    if(bgfx::isValid(vertex_buffer_))
    {
        bgfx::setBuffer(stage, vertex_buffer_, access);
    }
    else if(bgfx::isValid(index_buffer_))
    {
        bgfx::setBuffer(stage, index_buffer_, access);
    }
}

auto shader_buffer::is_valid() const -> bool
{
    return bgfx::isValid(vertex_buffer_) || bgfx::isValid(index_buffer_) || bgfx::isValid(texture_);
}

auto shader_buffer::get_capacity() const -> uint32_t
{
    return is_valid() ? capacity_ : 0u;
}

auto shader_buffer::get_storage() const -> shader_buffer_storage
{
    return storage_;
}

void shader_buffer::destroy()
{
    destroy_storage();
    gpu_resource::destroy_handle(sampler_);
}

void shader_buffer::destroy_storage()
{
    gpu_resource::destroy_handle(vertex_buffer_);
    gpu_resource::destroy_handle(index_buffer_);
    gpu_resource::destroy_handle(texture_);
    capacity_ = 0;
}

auto shader_buffer::get_element_bytes() const -> uint32_t
{
    return bimg::getBitsPerPixel(bimg::TextureFormat::Enum(get_texture_format())) / BITS_PER_BYTE;
}

auto shader_buffer::get_uints_per_element() const -> uint32_t
{
    return element_ == shader_buffer_element::uint4 ? UINTS_PER_UINT4 : 1u;
}

auto shader_buffer::get_texture_format() const -> bgfx::TextureFormat::Enum
{
    switch(element_)
    {
        case shader_buffer_element::uint4:
            return bgfx::TextureFormat::RGBA32U;
        case shader_buffer_element::uint1:
            return bgfx::TextureFormat::R32U;
        case shader_buffer_element::float4:
        default:
            return bgfx::TextureFormat::RGBA32F;
    }
}

} // namespace unravel
