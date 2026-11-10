#pragma once
#include <engine/engine_export.h>

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace unravel
{

/// Texels per row of a shader buffer kept in a texture (SHADER_BUFFER_TEXTURE_WIDTH in shader_buffer.sh): WebGL 2's
/// smallest maximum texture size.
constexpr uint32_t SHADER_BUFFER_TEXTURE_WIDTH = 2048;

/// Where a shader buffer keeps its elements.
enum class shader_buffer_storage : uint8_t
{
    /// A storage buffer (BUFFER_RO in the shaders).
    buffer,
    /// A texture of SHADER_BUFFER_TEXTURE_WIDTH texels a row, read with texelFetch: for devices whose vertex shaders
    /// read no storage buffers, as WebGL 2.
    texture,
};

/// What one element of a shader buffer is, the same in both storages.
enum class shader_buffer_element : uint8_t
{
    /// Four floats: a vec4 of a vec4 buffer, an rgba32f texel.
    float4,
    /// Four uints: four elements of a uint buffer, an rgba32u texel.
    uint4,
    /// One uint: an element of a uint buffer, an r32u texel.
    uint1,
};

/// The storage vertex shaders read on this device: buffers where it runs render_feature::vertex_storage_buffers,
/// textures elsewhere.
ENGINE_EXPORT auto get_vertex_shader_buffer_storage() -> shader_buffer_storage;

/**
 * @brief The variant of a shader that reads its shader buffers from @p storage: @p variant itself for buffers; for
 * textures its twin that defines SHADER_BUFFER_TEXTURES, "textures" for the default variant and @p variant +
 * "_textures" for the others. gpu_program draws with the twin itself where get_vertex_shader_buffer_storage gives
 * textures; a buffer of another storage (the particles', get_particle_data_storage) names its twin with this.
 */
ENGINE_EXPORT auto get_shader_buffer_variant(const std::string& variant, shader_buffer_storage storage) -> std::string;

/**
 * @brief Data shaders read by index: a storage buffer, or where vertex shaders cannot read one a texture of the same
 * elements in rows of SHADER_BUFFER_TEXTURE_WIDTH texels. shader_buffer.sh declares and reads both.
 *
 * Holds raw bgfx handles: its owner calls destroy() before the graphics backend shuts down.
 */
class ENGINE_EXPORT shader_buffer
{
public:
    /**
     * @param sampler The name of the sampler uniform the texture storage binds through, as the shaders declare it.
     * @param element What one element is.
     * @param storage Where the elements live.
     * @param buffer_flags The compute access of the buffer storage: BGFX_BUFFER_COMPUTE_READ, or
     *        BGFX_BUFFER_COMPUTE_READ_WRITE for a buffer compute shaders write.
     */
    shader_buffer(std::string sampler,
                  shader_buffer_element element,
                  shader_buffer_storage storage,
                  uint16_t buffer_flags = BGFX_BUFFER_COMPUTE_READ);
    shader_buffer(shader_buffer&& other) noexcept;
    auto operator=(shader_buffer&& other) noexcept -> shader_buffer&;
    shader_buffer(const shader_buffer&) = delete;
    auto operator=(const shader_buffer&) -> shader_buffer& = delete;
    ~shader_buffer();

    /// Picks where the elements live; destroys the storage and the sampler uniform first.
    void set_storage(shader_buffer_storage storage);

    /**
     * @brief Creates the sampler uniform of the texture storage; buffer storage needs none. The GL renderer wires a
     * program's samplers when it links the program: call it before the programs that read this buffer are created.
     */
    void init();

    /**
     * @brief Makes room for @p elements elements. Storage that holds fewer is created again, larger, and loses its
     * contents: returns true then, so the caller uploads everything again.
     */
    auto reserve(uint32_t elements) -> bool;

    /// Copies @p count elements from @p data to the elements from @p first on, as far as the storage reaches.
    void update(uint32_t first, uint32_t count, const void* data) const;

    /**
     * @brief Hands @p count elements at @p data to the storage from the first element on, as far as it reaches,
     * without copying them: @p data stays untouched until @p release runs with @p user_data. bgfx runs it on its render
     * thread; it runs at once when nothing went by reference (no storage, or a texture's elements short of one row,
     * which are copied: a texture takes whole rows by reference and copies the rest of the last one).
     */
    void update_by_reference(uint32_t count, const void* data, bgfx::ReleaseFn release, void* user_data) const;

    /// Binds the storage for the next draw at @p stage.
    void bind(uint8_t stage) const;

    /// Binds the buffer storage for the next compute dispatch at @p stage with @p access.
    void bind_compute(uint8_t stage, bgfx::Access::Enum access) const;

    auto is_valid() const -> bool;
    /// The elements the storage holds.
    auto get_capacity() const -> uint32_t;
    auto get_storage() const -> shader_buffer_storage;

    /// Releases the storage and the sampler uniform.
    void destroy();

private:
    auto get_element_bytes() const -> uint32_t;
    /// uints per element of a uint buffer.
    auto get_uints_per_element() const -> uint32_t;
    auto get_texture_format() const -> bgfx::TextureFormat::Enum;
    void create_buffer(uint32_t elements);
    void create_texture(uint32_t elements);
    void destroy_storage();

    std::string sampler_name_;
    shader_buffer_element element_{shader_buffer_element::float4};
    shader_buffer_storage storage_{shader_buffer_storage::buffer};
    uint16_t buffer_flags_{BGFX_BUFFER_COMPUTE_READ};
    /// The buffer storage of float4 elements.
    bgfx::DynamicVertexBufferHandle vertex_buffer_ = BGFX_INVALID_HANDLE;
    /// The buffer storage of uint elements.
    bgfx::DynamicIndexBufferHandle index_buffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    uint32_t capacity_ = 0;
};

} // namespace unravel
