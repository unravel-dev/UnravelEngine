#pragma once

#include <bgfx/bgfx.h>
#include <hpp/string_view.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gfx
{

/// The shape of a texture a shader writes or a pass renders to.
enum class texture_dimension : std::uint8_t
{
    two_d,
    three_d,
    cube,
};

/**
 * @brief What the running device lets the engine use: the backend capabilities its features depend on, taken
 * once after the backend starts.
 *
 * Features decide from this snapshot, never from bgfx::getCaps directly, so a simulated device
 * (@ref set_device_caps_mask) reaches every decision the real one would.
 */
struct device_caps
{
    bgfx::RendererType::Enum renderer{bgfx::RendererType::Noop};
    std::uint16_t vendor_id{};
    std::uint16_t device_id{};
    /// Compute shaders.
    bool has_compute{};
    /// Indirect draws (on Vulkan: multi-draw indirect and a first instance in the arguments).
    bool has_draw_indirect{};
    /// Indirect compute dispatches.
    bool has_dispatch_indirect{};
    /// Read-write images in formats other than the 32-bit single-channel ones.
    bool has_image_read_write{};
    /// A blend state per color attachment.
    bool has_blend_independent{};
    /// More than one window.
    bool has_swap_chain{};
    /// The resources a compute dispatch binds: stages at or above it are not bound.
    std::uint8_t max_compute_bindings{};
    std::uint32_t max_texture_size{};
    std::uint8_t max_frame_buffer_attachments{};
    /// BGFX_CAPS_FORMAT_TEXTURE_* per texture format.
    std::array<std::uint32_t, bgfx::TextureFormat::Count> formats{};

    /// Whether @p format has every one of @p caps (BGFX_CAPS_FORMAT_TEXTURE_*).
    auto has_format_caps(bgfx::TextureFormat::Enum format, std::uint32_t caps) const -> bool;
    /// Whether a compute shader can write @p format as a storage image of @p dimension.
    auto can_write_image(bgfx::TextureFormat::Enum format, texture_dimension dimension = texture_dimension::two_d) const
        -> bool;
    /// Whether a compute shader can read and write @p format through one image binding.
    auto can_read_write_image(bgfx::TextureFormat::Enum format) const -> bool;
    /// Whether a pass can render to @p format with @p samples per pixel.
    auto can_render_to(bgfx::TextureFormat::Enum format, std::uint8_t samples = 1) const -> bool;
    /// Whether the backend can build the mip chain of a @p format render target.
    auto can_generate_mips(bgfx::TextureFormat::Enum format) const -> bool;
};

/**
 * @brief What a simulated device lacks next to the real one (the --gfx-disable launch option).
 *
 * The backend keeps its real capabilities; the engine decides from the masked snapshot, so every fallback can
 * be exercised on a device that has everything.
 */
struct device_caps_mask
{
    bool drops_compute{};
    bool drops_draw_indirect{};
    bool drops_dispatch_indirect{};
    bool drops_image_read_write{};
    bool drops_blend_independent{};
    /// Formats other than the 32-bit single-channel ones lose image reads (typed UAV loads).
    bool drops_typed_image_loads{};
    /// The storage formats Vulkan offers only with shaderStorageImageExtendedFormats lose image access.
    bool drops_extended_storage_formats{};
    /// The compute bindings of the simulated device; 0 keeps the real count.
    std::uint8_t max_compute_bindings{};
    /// BGFX_CAPS_FORMAT_TEXTURE_* removed from single formats.
    std::vector<std::pair<bgfx::TextureFormat::Enum, std::uint32_t>> dropped_format_caps;

    auto is_empty() const -> bool;
};

/**
 * @brief Reads a --gfx-disable value: comma-separated names among compute, draw_indirect, dispatch_indirect,
 * image_rw, blend_independent, typed_image_loads, extended_storage_formats, bindings8, and
 * format:<name>:<write|read|render|msaa|mips|all> (for example format:rg11b10f:write).
 * @param unknown_names Receives each name that matched nothing.
 */
auto parse_device_caps_mask(hpp::string_view text, std::vector<std::string>& unknown_names) -> device_caps_mask;

/// The snapshot of @p caps, as @ref get_device_caps reports it for an unmasked device.
auto make_device_caps(const bgfx::Caps& caps) -> device_caps;

/// @p caps without what @p mask drops.
auto apply_device_caps_mask(device_caps caps, const device_caps_mask& mask) -> device_caps;

/// The mask the snapshot taken at @ref init applies. Set BEFORE init.
void set_device_caps_mask(const device_caps_mask& mask);
auto get_device_caps_mask() -> const device_caps_mask&;

/// Takes the snapshot of the running backend. Called by @ref init after the backend started.
void init_device_caps();

/// What the engine may use on this device, simulation included.
auto get_device_caps() -> const device_caps&;

/// What the device really offers, without the simulation mask.
auto get_real_device_caps() -> const device_caps&;

/**
 * @brief The features the engine cannot render without, missing from @p caps: one sentence each, empty when
 * the device has them all.
 */
auto get_missing_minimum_requirements(const device_caps& caps) -> std::vector<std::string>;

/// The GPU maker for a PCI vendor id, or "Unknown vendor".
auto get_vendor_name(std::uint16_t vendor_id) -> const char*;

/// One line naming the device and the capabilities its features depend on.
auto describe_device_caps(const device_caps& caps) -> std::string;

} // namespace gfx
