#pragma once

#include "handle_impl.h"
#include <base/basetypes.hpp>
#include <memory>

namespace gfx
{
/// The storage shape of a texture.
enum class texture_kind : std::uint8_t
{
    /// A 2D texture, optionally an array of layers.
    tex_2d,
    /// A volume of depth slices.
    tex_3d,
    /// Six square faces, optionally an array of layers.
    tex_cube,
};

/**
 * @brief Everything that makes two size-based textures interchangeable.
 *
 * A texture built from a desc keeps it (texture::get_desc()), so comparing descs is exact: a 1x1
 * texture asked for with mips still compares equal to its desc although bgfx gives it one level.
 * Members are ordered for designated initializers: {.size = s, .format = f, .flags = fl} covers
 * the common case.
 */
struct texture_desc
{
    /// Width x height. A cube's faces are size.width square; pass a square size.
    usize32_t size{};
    bgfx::TextureFormat::Enum format{bgfx::TextureFormat::Unknown};
    /// BGFX_TEXTURE_* creation flags, plus the BGFX_SAMPLER_* defaults baked into the texture.
    std::uint64_t flags{BGFX_TEXTURE_NONE};
    /// A full mip chain when true, a single level otherwise.
    bool has_mips{false};
    /// Array layers of a 2D or cube texture; a 3D texture has exactly one.
    std::uint16_t layers{1};
    /// Depth slices of a 3D texture; 2D and cube textures have exactly one.
    std::uint16_t depth{1};
    texture_kind kind{texture_kind::tex_2d};

    auto operator==(const texture_desc& other) const -> bool = default;
};

struct texture : public handle_impl<texture, bgfx::TextureHandle>
{
    //-----------------------------------------------------------------------------
    //  Name : Texture ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    texture() = default;

    texture(const char* _path,
            std::uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            std::uint8_t _skip = 0,
            bgfx::TextureInfo* _info = nullptr);

    texture(const void* _data,
            std::uint32_t _size,
            std::uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            std::uint8_t _skip = 0,
            bgfx::TextureInfo* _info = nullptr,
            const char* _name = nullptr);

    //-----------------------------------------------------------------------------
    //  Name : Texture ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    texture(std::uint16_t _width,
            std::uint16_t _height,
            bool _hasMips,
            std::uint16_t _numLayers,
            bgfx::TextureFormat::Enum _format,
            std::uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            const bgfx::Memory* _mem = nullptr);


    //-----------------------------------------------------------------------------
    //  Name : Texture ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    texture(std::uint16_t _width,
            std::uint16_t _height,
            std::uint16_t _depth,
            bool _hasMips,
            bgfx::TextureFormat::Enum _format,
            std::uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            const bgfx::Memory* _mem = nullptr);

    //-----------------------------------------------------------------------------
    //  Name : Texture ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    texture(std::uint16_t _size,
            bool _hasMips,
            std::uint16_t _numLayers,
            bgfx::TextureFormat::Enum _format,
            std::uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            const bgfx::Memory* _mem = nullptr);

    /**
     * @brief Creates the texture @p desc describes. The size-based constructors above forward here.
     *
     * @param desc The texture's shape, format and flags; kept, see get_desc().
     * @param mem Initial contents, or null. With eviction supported the contents are backed up so
     * an evicted texture can be restored.
     */
    explicit texture(const texture_desc& desc, const bgfx::Memory* mem = nullptr);

    /**
     * @brief The desc this texture was built from. For a texture loaded from a file or memory it
     * is read from the loaded info.
     */
    [[nodiscard]] auto get_desc() const -> const texture_desc&;

    //-----------------------------------------------------------------------------
    //  Name : get_size ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    auto get_size() const -> usize32_t;

    //-----------------------------------------------------------------------------
    //  Name : is_render_target ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    auto is_render_target() const -> bool;

    //-----------------------------------------------------------------------------
    //  Name : is_gpu_generated ()
    /// <summary>
    /// Whether the texture's contents are produced on the GPU (render target or compute-write) rather
    /// than uploaded from CPU memory. Such textures have no CPU backing to restore from, so eviction
    /// can only recreate them as empty storage.
    /// </summary>
    //-----------------------------------------------------------------------------
    auto is_gpu_generated() const -> bool;

    //-----------------------------------------------------------------------------
    //  Name : fallback_handle ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    auto fallback_handle() const -> bgfx::TextureHandle;


    auto get_estimated_gpu_size() const -> std::uint64_t;

    /// Texture detail info.
    bgfx::TextureInfo info{};
    /// Creation flags.
    std::uint64_t flags = BGFX_TEXTURE_NONE;

private:
    void adopt_loaded_eviction(eviction::backing_buffer backing,
                               std::uint64_t estimated_size,
                               std::uint64_t load_flags,
                               std::uint8_t skip,
                               const char* label);

    texture_desc desc_{};
};


/// Whether @p tex is missing or was built from a desc other than @p desc.
inline auto needs_recreate(const gfx::texture::ptr& tex, const texture_desc& desc) -> bool
{
    return !tex || tex->get_desc() != desc;
}
} // namespace gfx
