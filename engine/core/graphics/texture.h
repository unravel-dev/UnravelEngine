#pragma once

#include "format.h"
#include "handle_impl.h"
#include <base/basetypes.hpp>
#include <array>
#include <memory>
#include <string>

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

    /// Leaves the eviction system before its members go, so a sweep never calls into it half destroyed.
    ~texture() override;

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

    /// Where the texture keeps a normal map's vector, read as one (gfx::get_normal_map_channels).
    auto get_normal_map_channels() const -> normal_map_channels;

    /// Marks the texture as a normal map, as its import settings say, which decides how an ASTC one is read.
    void set_normal_map(bool is_normal_map);

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

    /// Creates the GPU copy at @p level from the parsed image a loaded texture keeps (see
    /// ievictable::on_set_level); a texture built from a desc restores level 0 only.
    auto on_set_level(std::uint8_t level) -> bool override;

    /// The estimated GPU bytes of the copy at @p level: what @ref get_estimated_gpu_size gives for the
    /// texture without its @p level top mips.
    [[nodiscard]] auto gpu_size_at(std::uint8_t level) const -> std::uint64_t override;

    /// The level that leaves only mips no longer than @p tail_size: non-zero for a single 2D texture
    /// loaded from a file or memory, with mips above that size.
    [[nodiscard]] auto get_tail_level(std::uint32_t tail_size) const -> std::uint8_t override;

    /// Texture detail info. Always describes the full texture, whatever level its GPU copy has.
    bgfx::TextureInfo info{};
    /// Creation flags.
    std::uint64_t flags = BGFX_TEXTURE_NONE;

private:
    /// What a texture loaded from a file or memory creates its GPU copies from.
    struct image_source;

    /// The level a loaded texture starts at and the GPU bytes admitted for it.
    struct load_start
    {
        std::uint8_t level{0};
        /// False when not even the smallest copy fits: the first draw creates it.
        bool create{false};
        std::uint64_t reserved_bytes{0};
    };

    /// Parses @p data and creates the texture, keeping the parsed image when eviction is supported.
    void load_image(const void* data, std::uint32_t size, std::uint64_t load_flags, std::uint8_t skip, const char* name);
    /// Creates a texture loaded from an image: a GPU-written one whole and untracked, any other at the
    /// level @ref admit_load_start picks, registered with the eviction system.
    void adopt_image_eviction(const char* name);
    /// Whether a texture made from @p desc with initial contents @p mem is tracked by eviction: it has
    /// CPU contents to come back from and the GPU never writes it.
    static auto is_tracked_desc(const texture_desc& desc, const bgfx::Memory* mem) -> bool;
    /// Creates a tracked texture from @p desc and @p mem, counting its bytes against the hard limit (it is
    /// created past it too: only the create can free @p mem), and registers it with a copy of @p mem to
    /// come back from.
    void create_tracked(const texture_desc& desc, const bgfx::Memory* mem, std::uint64_t estimated_size);
    /// Where a loaded texture starts. With a tail, whole only while that keeps the GPU under the soft
    /// budget, else at its tail under the hard limit: past the soft budget the frame pass would take its
    /// top mips at once, and it sharpens when draws read it and there is room. Without a tail, whole
    /// under the hard limit. Nothing created when that does not fit either.
    auto admit_load_start(const std::string& what) const -> load_start;
    /// A new GPU copy of the kept image without its @p level top mips.
    auto create_level(std::uint8_t level) const -> bgfx::TextureHandle;

    texture_desc desc_{};
    /// The parsed image of a loaded texture while eviction is supported; null otherwise.
    std::shared_ptr<const image_source> source_;
    /// Estimated GPU bytes at each level of a loaded texture.
    std::array<std::uint64_t, residency::k_max_levels> level_bytes_{};
    /// Levels the loaded texture has: its mip count.
    std::uint8_t level_count_{0};
    /// Whether the texture can keep a tail: one 2D side with mips, loaded, not written by the GPU.
    bool can_reduce_{false};
    /// Whether the texture's import settings call it a normal map.
    bool is_normal_map_{false};
};


/// Whether @p tex is missing or was built from a desc other than @p desc.
inline auto needs_recreate(const gfx::texture::ptr& tex, const texture_desc& desc) -> bool
{
    return !tex || tex->get_desc() != desc;
}
} // namespace gfx
