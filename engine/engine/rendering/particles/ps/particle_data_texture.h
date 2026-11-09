#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief Whether the particle vertex shaders read their data from textures instead of storage buffers.
 *
 * True on a device without the storage buffer path (render_feature::particle_storage_buffers: no compute, or no
 * storage buffers in vertex shaders, as WebGL): the CPU then writes the records, culls and sorts.
 */
auto uses_data_textures() -> bool;

/**
 * @brief The vs_particle.eshader variant to draw with for @p variant: its data texture twin (textures, lit_textures,
 * velocity_textures) where @ref uses_data_textures, else @p variant itself.
 */
auto get_particle_vertex_variant(const std::string& variant) -> std::string;

/**
 * @brief A texture the particle vertex shaders read in place of a storage buffer: the buffer's elements as texels,
 * in rows of data_texture_width, read with texelFetch (PARTICLE_DATA_TEXTURES in particle_common.sh).
 *
 * Holds a raw bgfx handle like the buffers it stands in for: its owner destroys it before the renderer goes.
 */
class data_texture
{
public:
    explicit data_texture(bgfx::TextureFormat::Enum format);

    /**
     * @brief Makes room for @p texels texels, creating the texture again when it has fewer rows: the content is then
     * lost. Returns whether it was created, so the caller uploads everything again.
     */
    auto reserve(uint32_t texels) -> bool;

    /// Copies @p count texels from @p data to the texels from @p first on, in as few updates as the rows allow.
    void update(uint32_t first, uint32_t count, const void* data) const;

    auto is_valid() const -> bool;
    auto get_handle() const -> bgfx::TextureHandle;

    void destroy();

private:
    auto get_capacity() const -> uint32_t;

    bgfx::TextureFormat::Enum format_;
    uint32_t texel_bytes_ = 0;
    bgfx::TextureHandle handle_ = BGFX_INVALID_HANDLE;
    uint32_t rows_ = 0;
};

} // namespace ps_soa
} // namespace unravel
