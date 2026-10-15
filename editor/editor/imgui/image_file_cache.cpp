#include "image_file_cache.h"

#include <graphics/utils/bgfx_utils.h>
#include <imgui/imgui.h>
#include <logging/logging.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace unravel
{
namespace image_file_cache_detail
{
namespace
{
// Reading and decoding a thumbnail takes a few milliseconds; two of them leave the frame smooth.
constexpr int IMAGE_FILE_CACHE_READS_PER_FRAME = 2;
constexpr uint32_t IMAGE_FILE_CACHE_CHANNELS = 4;
constexpr uint64_t IMAGE_FILE_CACHE_FLAGS = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

struct mip_size
{
    uint32_t width{};
    uint32_t height{};
};

auto get_next_mip_size(const mip_size& size) -> mip_size
{
    return {std::max(1u, size.width / 2), std::max(1u, size.height / 2)};
}

/// Appends the level below the one at source_offset: every texel the average of the up to 2 x 2
/// texels above it.
void append_half_level(std::vector<uint8_t>& chain, size_t source_offset, const mip_size& source)
{
    const mip_size target = get_next_mip_size(source);
    const size_t target_offset = chain.size();
    chain.resize(target_offset + size_t(target.width) * target.height * IMAGE_FILE_CACHE_CHANNELS);
    for(uint32_t y = 0; y < target.height; ++y)
    {
        const uint32_t y0 = std::min(y * 2, source.height - 1);
        const uint32_t y1 = std::min(y * 2 + 1, source.height - 1);
        for(uint32_t x = 0; x < target.width; ++x)
        {
            const uint32_t x0 = std::min(x * 2, source.width - 1);
            const uint32_t x1 = std::min(x * 2 + 1, source.width - 1);
            for(uint32_t channel = 0; channel < IMAGE_FILE_CACHE_CHANNELS; ++channel)
            {
                const auto texel = [&](uint32_t sx, uint32_t sy) -> uint32_t
                {
                    return chain[source_offset + (size_t(sy) * source.width + sx) * IMAGE_FILE_CACHE_CHANNELS + channel];
                };
                const uint32_t sum = texel(x0, y0) + texel(x1, y0) + texel(x0, y1) + texel(x1, y1);
                chain[target_offset + (size_t(y) * target.width + x) * IMAGE_FILE_CACHE_CHANNELS + channel] =
                    static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
}

/// The RGBA8 pixels of the image followed by its whole mip chain, as bgfx takes them.
auto build_mip_chain(const bimg::ImageContainer& image) -> std::vector<uint8_t>
{
    const auto* pixels = static_cast<const uint8_t*>(image.m_data);
    std::vector<uint8_t> chain(pixels, pixels + size_t(image.m_width) * image.m_height * IMAGE_FILE_CACHE_CHANNELS);
    mip_size size{image.m_width, image.m_height};
    size_t offset = 0;
    while(size.width > 1 || size.height > 1)
    {
        const size_t level_offset = chain.size();
        append_half_level(chain, offset, size);
        offset = level_offset;
        size = get_next_mip_size(size);
    }
    return chain;
}

auto read_image(const fs::path& path) -> gfx::texture::ptr
{
    bimg::ImageContainer* image = imageLoad(bx::FilePath(path.string().c_str()), bgfx::TextureFormat::RGBA8);
    if(image == nullptr)
    {
        APPLOG_WARNING("Cannot read the image {}", path.string());
        return {};
    }
    const std::vector<uint8_t> chain = build_mip_chain(*image);
    const auto width = static_cast<uint16_t>(image->m_width);
    const auto height = static_cast<uint16_t>(image->m_height);
    bimg::imageFree(image);
    const bgfx::Memory* memory = bgfx::copy(chain.data(), static_cast<uint32_t>(chain.size()));
    auto texture = std::make_shared<gfx::texture>(width, height, true, 1, bgfx::TextureFormat::RGBA8, IMAGE_FILE_CACHE_FLAGS, memory);
    return texture->is_valid() ? texture : nullptr;
}
} // namespace
} // namespace image_file_cache_detail

auto image_file_cache::get(const fs::path& path) -> gfx::texture::ptr
{
    if(path.empty())
    {
        return {};
    }
    entry& cached = entries_[path.generic_string()];
    if(cached.is_checked)
    {
        return cached.texture;
    }
    fs::error_code error;
    const fs::file_time_type write_time = fs::last_write_time(path, error);
    if(error)
    {
        cached = {};
        cached.is_checked = true;
        return {};
    }
    const bool is_current = cached.texture != nullptr && cached.write_time == write_time;
    if(!is_current)
    {
        // An older image of the file stays up until the new one is read.
        if(!take_read_budget())
        {
            return cached.texture;
        }
        cached.texture = image_file_cache_detail::read_image(path);
        cached.write_time = write_time;
    }
    cached.is_checked = true;
    return cached.texture;
}

void image_file_cache::refresh()
{
    for(auto& [key, cached] : entries_)
    {
        cached.is_checked = false;
    }
}

void image_file_cache::clear()
{
    entries_.clear();
}

auto image_file_cache::take_read_budget() -> bool
{
    const int frame = ImGui::GetFrameCount();
    if(frame != budget_frame_)
    {
        budget_frame_ = frame;
        reads_in_frame_ = 0;
    }
    if(reads_in_frame_ >= image_file_cache_detail::IMAGE_FILE_CACHE_READS_PER_FRAME)
    {
        return false;
    }
    ++reads_in_frame_;
    return true;
}

} // namespace unravel
