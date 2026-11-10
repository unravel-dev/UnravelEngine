#include "device_caps.h"

#include <base/platform/config.hpp>
#include <bimg/bimg.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#if UNRAVEL_PLATFORM_EMSCRIPTEN
#include <emscripten/em_js.h>

// The storage textures per shader stage of the WebGPU device the page created (Module.preinitializedWebGPUDevice);
// 0 when there is none.
EM_JS(int, unravel_get_webgpu_storage_textures_per_stage, (), {
    const device = Module.preinitializedWebGPUDevice;
    return device ? device.limits.maxStorageTexturesPerShaderStage : 0;
});
#endif

namespace gfx
{
namespace
{
constexpr std::uint16_t VENDOR_SOFTWARE = 0x0001;
constexpr std::uint16_t VENDOR_AMD = 0x1002;
constexpr std::uint16_t VENDOR_IMAGINATION = 0x1010;
constexpr std::uint16_t VENDOR_APPLE = 0x106b;
constexpr std::uint16_t VENDOR_NVIDIA = 0x10de;
constexpr std::uint16_t VENDOR_ARM = 0x13b5;
constexpr std::uint16_t VENDOR_MICROSOFT = 0x1414;
constexpr std::uint16_t VENDOR_SAMSUNG = 0x144d;
constexpr std::uint16_t VENDOR_QUALCOMM = 0x5143;
constexpr std::uint16_t VENDOR_INTEL = 0x8086;

/// The color targets the deferred G-buffer renders to at once.
constexpr std::uint8_t MIN_FRAME_BUFFER_ATTACHMENTS = 4;

/// Formats Vulkan offers as storage images only with shaderStorageImageExtendedFormats.
constexpr std::array EXTENDED_STORAGE_FORMATS = {
    bgfx::TextureFormat::R8,       bgfx::TextureFormat::R8I,     bgfx::TextureFormat::R8U,
    bgfx::TextureFormat::R8S,      bgfx::TextureFormat::R16,     bgfx::TextureFormat::R16I,
    bgfx::TextureFormat::R16U,     bgfx::TextureFormat::R16F,    bgfx::TextureFormat::R16S,
    bgfx::TextureFormat::RG8,      bgfx::TextureFormat::RG8I,    bgfx::TextureFormat::RG8U,
    bgfx::TextureFormat::RG8S,     bgfx::TextureFormat::RG16,    bgfx::TextureFormat::RG16I,
    bgfx::TextureFormat::RG16U,    bgfx::TextureFormat::RG16F,   bgfx::TextureFormat::RG16S,
    bgfx::TextureFormat::RGBA16,   bgfx::TextureFormat::RGBA16S, bgfx::TextureFormat::RGB10A2,
    bgfx::TextureFormat::RG11B10F,
};

struct format_access_name
{
    const char* name;
    std::uint32_t caps;
};

constexpr std::array FORMAT_ACCESS_NAMES = {
    format_access_name{"write", BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE},
    format_access_name{"read", BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ},
    format_access_name{"render", BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER},
    format_access_name{"msaa", BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA | BGFX_CAPS_FORMAT_TEXTURE_MSAA},
    format_access_name{"mips", BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN},
    format_access_name{"all", UINT32_MAX},
};

device_caps_mask device_caps_mask_;
device_caps real_device_caps_;
device_caps device_caps_;

auto is_single_channel_32_bit(bgfx::TextureFormat::Enum format) -> bool
{
    return format == bgfx::TextureFormat::R32F || format == bgfx::TextureFormat::R32U ||
           format == bgfx::TextureFormat::R32I;
}

/// The formats WebGPU's texture-formats-tier2 lets one binding read and write, besides the 32-bit single-channel
/// ones core WebGPU allows: every other format stays read-only or write-only there, whatever BGFX_CAPS_IMAGE_RW
/// says (bgfx reports it once for all formats).
constexpr std::array<bgfx::TextureFormat::Enum, 15> WEBGPU_READ_WRITE_FORMATS = {
    bgfx::TextureFormat::R8,
    bgfx::TextureFormat::R8U,
    bgfx::TextureFormat::R8I,
    bgfx::TextureFormat::RGBA8,
    bgfx::TextureFormat::RGBA8U,
    bgfx::TextureFormat::RGBA8I,
    bgfx::TextureFormat::R16U,
    bgfx::TextureFormat::R16I,
    bgfx::TextureFormat::R16F,
    bgfx::TextureFormat::RGBA16U,
    bgfx::TextureFormat::RGBA16I,
    bgfx::TextureFormat::RGBA16F,
    bgfx::TextureFormat::RGBA32U,
    bgfx::TextureFormat::RGBA32I,
    bgfx::TextureFormat::RGBA32F,
};

/// Whether @p renderer reads and writes @p format through one binding once read-write images are available.
auto allows_read_write_format(bgfx::RendererType::Enum renderer, bgfx::TextureFormat::Enum format) -> bool
{
    return renderer != bgfx::RendererType::WebGPU ||
           std::find(WEBGPU_READ_WRITE_FORMATS.begin(), WEBGPU_READ_WRITE_FORMATS.end(), format) !=
               WEBGPU_READ_WRITE_FORMATS.end();
}

auto get_dimension_caps(texture_dimension dimension) -> std::uint32_t
{
    switch(dimension)
    {
        case texture_dimension::three_d:
            return BGFX_CAPS_FORMAT_TEXTURE_3D;
        case texture_dimension::cube:
            return BGFX_CAPS_FORMAT_TEXTURE_CUBE;
        case texture_dimension::two_d:
        default:
            return BGFX_CAPS_FORMAT_TEXTURE_2D;
    }
}

auto get_render_caps(std::uint8_t samples) -> std::uint32_t
{
    if(samples > 8)
    {
        return BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X16;
    }
    if(samples > 4)
    {
        return BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X8;
    }
    if(samples > 2)
    {
        return BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X4;
    }
    if(samples > 1)
    {
        return BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X2;
    }
    return BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER;
}

auto to_lower_ascii(hpp::string_view text) -> std::string
{
    std::string lower(text);
    std::transform(lower.begin(),
                   lower.end(),
                   lower.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return lower;
}

auto find_texture_format(hpp::string_view name, bgfx::TextureFormat::Enum& format) -> bool
{
    const std::string lower = to_lower_ascii(name);
    for(int index = 0; index < bgfx::TextureFormat::Count; ++index)
    {
        const auto candidate = static_cast<bgfx::TextureFormat::Enum>(index);
        if(to_lower_ascii(bimg::getName(static_cast<bimg::TextureFormat::Enum>(candidate))) == lower)
        {
            format = candidate;
            return true;
        }
    }
    return false;
}

auto find_format_access(hpp::string_view name, std::uint32_t& caps) -> bool
{
    const auto found = std::find_if(FORMAT_ACCESS_NAMES.begin(),
                                    FORMAT_ACCESS_NAMES.end(),
                                    [&](const format_access_name& access)
                                    {
                                        return name == hpp::string_view(access.name);
                                    });
    if(found == FORMAT_ACCESS_NAMES.end())
    {
        return false;
    }
    caps = found->caps;
    return true;
}

/// Reads "format:<name>:<access>" into @p mask; false when the name or the access matched nothing.
auto parse_format_name(hpp::string_view name, device_caps_mask& mask) -> bool
{
    constexpr hpp::string_view prefix = "format:";
    const auto rest = name.substr(prefix.size());
    const auto separator = rest.find(':');
    if(separator == hpp::string_view::npos)
    {
        return false;
    }
    bgfx::TextureFormat::Enum format{};
    std::uint32_t caps{};
    if(!find_texture_format(rest.substr(0, separator), format) || !find_format_access(rest.substr(separator + 1), caps))
    {
        return false;
    }
    mask.dropped_format_caps.emplace_back(format, caps);
    return true;
}

/// Reads "bindings<count>" into @p mask; false when no count follows.
auto parse_bindings_name(hpp::string_view name, device_caps_mask& mask) -> bool
{
    constexpr hpp::string_view prefix = "bindings";
    constexpr std::size_t MAX_COUNT_DIGITS = 3;
    const std::string count_text(name.substr(prefix.size()));
    const bool is_count = !count_text.empty() && count_text.size() <= MAX_COUNT_DIGITS &&
                          std::all_of(count_text.begin(),
                                      count_text.end(),
                                      [](unsigned char c)
                                      {
                                          return std::isdigit(c) != 0;
                                      });
    if(!is_count)
    {
        return false;
    }
    mask.max_compute_bindings = static_cast<std::uint8_t>(std::clamp(std::stoi(count_text), 1, int(UINT8_MAX)));
    return true;
}

auto parse_mask_name(hpp::string_view name, device_caps_mask& mask) -> bool
{
    if(name.starts_with("format:"))
    {
        return parse_format_name(name, mask);
    }
    if(name.starts_with("bindings"))
    {
        return parse_bindings_name(name, mask);
    }
    const std::array<std::pair<const char*, bool*>, 9> flags = {{
        {"compute", &mask.drops_compute},
        {"draw_indirect", &mask.drops_draw_indirect},
        {"dispatch_indirect", &mask.drops_dispatch_indirect},
        {"image_rw", &mask.drops_image_read_write},
        {"blend_independent", &mask.drops_blend_independent},
        {"typed_image_loads", &mask.drops_typed_image_loads},
        {"extended_storage_formats", &mask.drops_extended_storage_formats},
        {"vertex_storage_buffers", &mask.drops_vertex_storage_buffers},
        {"depth_blit", &mask.drops_depth_blit},
    }};
    const auto found = std::find_if(flags.begin(),
                                    flags.end(),
                                    [&](const auto& flag)
                                    {
                                        return name == hpp::string_view(flag.first);
                                    });
    if(found == flags.end())
    {
        return false;
    }
    *found->second = true;
    return true;
}

void clear_format_caps(device_caps& caps, bgfx::TextureFormat::Enum format, std::uint32_t cleared)
{
    caps.formats[format] &= ~cleared;
}
} // namespace

auto device_caps::has_format_caps(bgfx::TextureFormat::Enum format, std::uint32_t caps) const -> bool
{
    if(format >= bgfx::TextureFormat::Count)
    {
        return false;
    }
    return (formats[format] & caps) == caps;
}

auto device_caps::can_write_image(bgfx::TextureFormat::Enum format, texture_dimension dimension) const -> bool
{
    return has_compute && has_format_caps(format, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE | get_dimension_caps(dimension));
}

auto device_caps::can_read_image(bgfx::TextureFormat::Enum format, texture_dimension dimension) const -> bool
{
    return has_compute && has_format_caps(format, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ | get_dimension_caps(dimension));
}

auto device_caps::can_read_write_image(bgfx::TextureFormat::Enum format) const -> bool
{
    const bool has_access =
        has_format_caps(format, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ | BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE);
    return has_compute && has_access &&
           (is_single_channel_32_bit(format) || (has_image_read_write && allows_read_write_format(renderer, format)));
}

auto device_caps::can_render_to(bgfx::TextureFormat::Enum format, std::uint8_t samples) const -> bool
{
    return has_format_caps(format, get_render_caps(samples));
}

auto device_caps::can_generate_mips(bgfx::TextureFormat::Enum format) const -> bool
{
    return has_format_caps(format, BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN);
}

auto device_caps::can_filter(bgfx::TextureFormat::Enum format) const -> bool
{
    const bool is_32_bit_float = format == bgfx::TextureFormat::R32F || format == bgfx::TextureFormat::RG32F ||
                                 format == bgfx::TextureFormat::RGBA32F;
    if(!is_32_bit_float)
    {
        return true;
    }
    switch(renderer)
    {
        case bgfx::RendererType::WebGPU:
        case bgfx::RendererType::OpenGLES:
            // bgfx's WebGPU backend samples them unfiltered; OpenGL ES filters them only with
            // OES_texture_float_linear, which bgfx does not report.
            return false;
        case bgfx::RendererType::Vulkan:
            // bgfx reports no filtering cap; it drops mip generation from a format without linear filtering.
            return can_generate_mips(format);
        default:
            return true;
    }
}

auto device_caps_mask::is_empty() const -> bool
{
    return !drops_compute && !drops_draw_indirect && !drops_dispatch_indirect && !drops_image_read_write &&
           !drops_blend_independent && !drops_typed_image_loads && !drops_extended_storage_formats &&
           !drops_vertex_storage_buffers && !drops_depth_blit && max_compute_bindings == 0 &&
           dropped_format_caps.empty();
}

auto parse_device_caps_mask(hpp::string_view text, std::vector<std::string>& unknown_names) -> device_caps_mask
{
    device_caps_mask mask;
    const std::string lower = to_lower_ascii(text);
    std::size_t start = 0;
    while(start <= lower.size())
    {
        const std::size_t end = std::min(lower.find(',', start), lower.size());
        hpp::string_view name(lower.data() + start, end - start);
        const auto first = name.find_first_not_of(' ');
        name = first == hpp::string_view::npos ? hpp::string_view{} : name.substr(first);
        name = name.substr(0, name.find_last_not_of(' ') + 1);
        if(!name.empty() && !parse_mask_name(name, mask))
        {
            unknown_names.emplace_back(name);
        }
        start = end + 1;
    }
    return mask;
}

auto make_device_caps(const bgfx::Caps& caps) -> device_caps
{
    device_caps result;
    result.renderer = caps.rendererType;
    result.vendor_id = caps.vendorId;
    result.device_id = caps.deviceId;
    result.has_compute = 0 != (caps.supported & BGFX_CAPS_COMPUTE);
    result.has_draw_indirect = 0 != (caps.supported & BGFX_CAPS_DRAW_INDIRECT);
    // bgfx has no cap of its own for this: every backend with compute dispatches indirectly.
    result.has_dispatch_indirect = result.has_compute;
    result.has_image_read_write = 0 != (caps.supported & BGFX_CAPS_IMAGE_RW);
    result.has_blend_independent = 0 != (caps.supported & BGFX_CAPS_BLEND_INDEPENDENT);
    result.has_vertex_storage_buffers = caps.rendererType != bgfx::RendererType::OpenGLES;
    result.has_depth_blit = caps.rendererType != bgfx::RendererType::OpenGLES;
    result.max_compute_bindings =
        static_cast<std::uint8_t>(std::min<std::uint32_t>(caps.limits.maxComputeBindings, UINT8_MAX));
    result.max_storage_textures_per_stage = result.max_compute_bindings;
    result.max_texture_size = caps.limits.maxTextureSize;
    result.max_frame_buffer_attachments = caps.limits.maxFBAttachments;
    std::copy(std::begin(caps.formats), std::end(caps.formats), result.formats.begin());
    return result;
}

auto apply_device_caps_mask(device_caps caps, const device_caps_mask& mask) -> device_caps
{
    caps.has_compute = caps.has_compute && !mask.drops_compute;
    caps.has_draw_indirect = caps.has_draw_indirect && !mask.drops_draw_indirect;
    caps.has_dispatch_indirect = caps.has_dispatch_indirect && caps.has_compute && !mask.drops_dispatch_indirect;
    caps.has_image_read_write = caps.has_image_read_write && !mask.drops_image_read_write;
    caps.has_blend_independent = caps.has_blend_independent && !mask.drops_blend_independent;
    caps.has_vertex_storage_buffers = caps.has_vertex_storage_buffers && !mask.drops_vertex_storage_buffers;
    caps.has_depth_blit = caps.has_depth_blit && !mask.drops_depth_blit;
    if(mask.max_compute_bindings != 0)
    {
        caps.max_compute_bindings = std::min(caps.max_compute_bindings, mask.max_compute_bindings);
        caps.max_storage_textures_per_stage = std::min(caps.max_storage_textures_per_stage, caps.max_compute_bindings);
    }
    if(mask.drops_typed_image_loads)
    {
        for(int index = 0; index < bgfx::TextureFormat::Count; ++index)
        {
            const auto format = static_cast<bgfx::TextureFormat::Enum>(index);
            if(!is_single_channel_32_bit(format))
            {
                clear_format_caps(caps, format, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ);
            }
        }
    }
    if(mask.drops_extended_storage_formats)
    {
        for(const auto format : EXTENDED_STORAGE_FORMATS)
        {
            clear_format_caps(caps, format, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ | BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE);
        }
    }
    for(const auto& [format, cleared] : mask.dropped_format_caps)
    {
        clear_format_caps(caps, format, cleared);
    }
    return caps;
}

void set_device_caps_mask(const device_caps_mask& mask)
{
    device_caps_mask_ = mask;
}

auto get_device_caps_mask() -> const device_caps_mask&
{
    return device_caps_mask_;
}

void init_device_caps()
{
    real_device_caps_ = make_device_caps(*bgfx::getCaps());
#if UNRAVEL_PLATFORM_EMSCRIPTEN
    // The page may have created its WebGPU device and still chosen WebGL 2: its limit holds for WebGPU only.
    const int browser_limit = real_device_caps_.renderer == bgfx::RendererType::WebGPU
                                  ? unravel_get_webgpu_storage_textures_per_stage()
                                  : 0;
    if(browser_limit > 0)
    {
        real_device_caps_.max_storage_textures_per_stage =
            static_cast<std::uint8_t>(std::min<int>(browser_limit, real_device_caps_.max_storage_textures_per_stage));
    }
#endif
    device_caps_ = apply_device_caps_mask(real_device_caps_, device_caps_mask_);
}

auto get_device_caps() -> const device_caps&
{
    return device_caps_;
}

auto get_real_device_caps() -> const device_caps&
{
    return real_device_caps_;
}

auto is_compute_required(bgfx::RendererType::Enum renderer) -> bool
{
    return renderer != bgfx::RendererType::OpenGLES;
}

auto get_missing_minimum_requirements(const device_caps& caps) -> std::vector<std::string>
{
    std::vector<std::string> missing;
    if(is_compute_required(caps.renderer) && !caps.has_compute)
    {
        missing.emplace_back("It has no compute shaders.");
    }
    if(!caps.can_render_to(bgfx::TextureFormat::RGBA16F))
    {
        missing.emplace_back("It cannot render to 16-bit float color targets.");
    }
    if(!caps.can_render_to(bgfx::TextureFormat::D32F))
    {
        missing.emplace_back("It cannot render to 32-bit float depth buffers.");
    }
    if(caps.max_frame_buffer_attachments < MIN_FRAME_BUFFER_ATTACHMENTS)
    {
        missing.emplace_back("It cannot render to four color targets at once.");
    }
    if(caps.has_compute && !caps.can_write_image(bgfx::TextureFormat::R32F))
    {
        missing.emplace_back("Its compute shaders cannot write 32-bit float images.");
    }
    return missing;
}

auto get_vendor_name(std::uint16_t vendor_id) -> const char*
{
    switch(vendor_id)
    {
        case VENDOR_SOFTWARE:
            return "Software rasterizer";
        case VENDOR_AMD:
            return "AMD";
        case VENDOR_IMAGINATION:
            return "Imagination";
        case VENDOR_APPLE:
            return "Apple";
        case VENDOR_NVIDIA:
            return "NVIDIA";
        case VENDOR_ARM:
            return "ARM";
        case VENDOR_MICROSOFT:
            return "Microsoft";
        case VENDOR_SAMSUNG:
            return "Samsung";
        case VENDOR_QUALCOMM:
            return "Qualcomm";
        case VENDOR_INTEL:
            return "Intel";
        default:
            return "Unknown vendor";
    }
}

auto describe_device_caps(const device_caps& caps) -> std::string
{
    char device_id[16];
    std::snprintf(device_id, sizeof(device_id), "0x%04x", caps.device_id);
    std::string text = std::string(bgfx::getRendererName(caps.renderer)) + " on " + get_vendor_name(caps.vendor_id) +
                       " (device " + device_id + "): compute " + (caps.has_compute ? "yes" : "no") +
                       ", compute bindings " + std::to_string(caps.max_compute_bindings) + ", storage images per stage " +
                       std::to_string(caps.max_storage_textures_per_stage) + ", read-write images " +
                       (caps.has_image_read_write ? "yes" : "no") + ", indirect draws " +
                       (caps.has_draw_indirect ? "yes" : "no") + ", indirect dispatches " +
                       (caps.has_dispatch_indirect ? "yes" : "no") + ", independent blend " +
                       (caps.has_blend_independent ? "yes" : "no") + ", vertex storage buffers " +
                       (caps.has_vertex_storage_buffers ? "yes" : "no") + ", depth blits " +
                       (caps.has_depth_blit ? "yes" : "no") + ".";
    return text;
}

} // namespace gfx
