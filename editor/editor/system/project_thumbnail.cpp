#include "project_thumbnail.h"

#include <editor/hub/hub.h>
#include <editor/hub/panels/game_panel/game_panel.h>
#include <editor/hub/panels/panel.h>
#include <editor/hub/panels/scene_panel/scene_panel.h>
#include <editor/system/project_manager.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/events.h>
#include <engine/threading/threader.h>
#include <graphics/render_pass.h>
#include <graphics/utils/bgfx_utils.h>
#include <logging/logging.h>

#include <bx/file.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace unravel
{
namespace project_thumbnail_detail
{
namespace
{
constexpr const char* PROJECT_THUMBNAIL_FILE_NAME = "thumbnail.png";
constexpr const char* PROJECT_THUMBNAIL_CACHE_DIRECTORY = "editor/.cache";
// Twice the largest a card draws it at on a 1x screen, so it stays sharp at 2x.
constexpr uint32_t PROJECT_THUMBNAIL_MAX_WIDTH = 640;
constexpr uint32_t PROJECT_THUMBNAIL_MAX_HEIGHT = 360;
constexpr uint32_t PROJECT_THUMBNAIL_CHANNELS = 4;
constexpr uint32_t PROJECT_THUMBNAIL_COLOR_CHANNELS = 3;
constexpr uint8_t PROJECT_THUMBNAIL_OPAQUE = 255;
// The lighting of a scene that just opened takes a few seconds to settle.
constexpr std::chrono::seconds PROJECT_THUMBNAIL_SETTLE_TIME{4};
// Frames the game view renders before the capture: a hidden view starts without history, and its
// temporal filters and lighting need these to converge.
constexpr int PROJECT_THUMBNAIL_WARM_FRAMES = 30;
// Frames a capture keeps trying when there is nothing to read, before it gives up.
constexpr int PROJECT_THUMBNAIL_GIVE_UP_FRAMES = -120;

struct image_size
{
    uint32_t width{};
    uint32_t height{};
};

auto fit_thumbnail_size(const ReadbackImage& image) -> image_size
{
    const float scale = std::min({1.0f,
                                  float(PROJECT_THUMBNAIL_MAX_WIDTH) / float(image.width),
                                  float(PROJECT_THUMBNAIL_MAX_HEIGHT) / float(image.height)});
    return {std::max(1u, static_cast<uint32_t>(std::lround(float(image.width) * scale))),
            std::max(1u, static_cast<uint32_t>(std::lround(float(image.height) * scale)))};
}

/// The first and one past the last source row (or column) under target row (or column) index.
auto get_source_span(uint32_t index, uint32_t target_count, uint32_t source_count) -> std::pair<uint32_t, uint32_t>
{
    const uint32_t first = static_cast<uint32_t>(uint64_t(index) * source_count / target_count);
    const uint32_t last = static_cast<uint32_t>(uint64_t(index + 1) * source_count / target_count);
    return {first, std::max(last, first + 1)};
}

/// Every texel the average of the source texels under it, so a large reduction stays smooth. The
/// alpha is made opaque: what the view leaves in it is no transparency.
auto downscale(const ReadbackImage& image, const image_size& size) -> std::vector<uint8_t>
{
    std::vector<uint8_t> result(size_t(size.width) * size.height * PROJECT_THUMBNAIL_CHANNELS);
    for(uint32_t y = 0; y < size.height; ++y)
    {
        const auto [row_first, row_last] = get_source_span(y, size.height, image.height);
        for(uint32_t x = 0; x < size.width; ++x)
        {
            const auto [column_first, column_last] = get_source_span(x, size.width, image.width);
            uint32_t sums[PROJECT_THUMBNAIL_COLOR_CHANNELS]{};
            for(uint32_t row = row_first; row < row_last; ++row)
            {
                const uint8_t* texel = image.pixels.data() + (size_t(row) * image.width + column_first) * PROJECT_THUMBNAIL_CHANNELS;
                for(uint32_t column = column_first; column < column_last; ++column, texel += PROJECT_THUMBNAIL_CHANNELS)
                {
                    sums[0] += texel[0];
                    sums[1] += texel[1];
                    sums[2] += texel[2];
                }
            }
            const uint32_t count = (row_last - row_first) * (column_last - column_first);
            uint8_t* target = result.data() + (size_t(y) * size.width + x) * PROJECT_THUMBNAIL_CHANNELS;
            for(uint32_t channel = 0; channel < PROJECT_THUMBNAIL_COLOR_CHANNELS; ++channel)
            {
                target[channel] = static_cast<uint8_t>((sums[channel] + count / 2) / count);
            }
            target[PROJECT_THUMBNAIL_COLOR_CHANNELS] = PROJECT_THUMBNAIL_OPAQUE;
        }
    }
    return result;
}

void write_png(const fs::path& path, const image_size& size, const std::vector<uint8_t>& pixels, bool is_bottom_up)
{
    bx::FileWriter writer;
    if(!bx::open(&writer, bx::FilePath(path.string().c_str())))
    {
        throw std::runtime_error("cannot open the file");
    }
    bx::Error error;
    bimg::imageWritePng(&writer,
                        size.width,
                        size.height,
                        size.width * PROJECT_THUMBNAIL_CHANNELS,
                        pixels.data(),
                        bimg::TextureFormat::RGBA8,
                        is_bottom_up,
                        &error);
    bx::close(&writer);
    if(!error.isOk())
    {
        throw std::runtime_error("cannot encode the image");
    }
}

void save_thumbnail(const ReadbackImage& image, const fs::path& target)
{
    const image_size size = fit_thumbnail_size(image);
    const std::vector<uint8_t> pixels = downscale(image, size);
    fs::error_code error;
    fs::create_directories(target.parent_path(), error);
    asset_writer::atomic_write_file(target,
                                    [&](const fs::path& temp)
                                    {
                                        write_png(temp, size, pixels, image.isBottomUp);
                                    },
                                    error);
    if(error)
    {
        APPLOG_WARNING("Cannot save the project thumbnail {}: {}", target.string(), error.message());
    }
}
} // namespace
} // namespace project_thumbnail_detail

namespace thumbnail_detail = project_thumbnail_detail;

auto project_thumbnail::get_custom_path(const fs::path& project_dir) -> fs::path
{
    return project_dir / thumbnail_detail::PROJECT_THUMBNAIL_FILE_NAME;
}

auto project_thumbnail::get_captured_path(const fs::path& project_dir) -> fs::path
{
    return project_dir / thumbnail_detail::PROJECT_THUMBNAIL_CACHE_DIRECTORY / thumbnail_detail::PROJECT_THUMBNAIL_FILE_NAME;
}

auto project_thumbnail::find(const fs::path& project_dir) -> fs::path
{
    for(const fs::path& path : {get_custom_path(project_dir), get_captured_path(project_dir)})
    {
        fs::error_code error;
        if(fs::is_regular_file(path, error))
        {
            return path;
        }
    }
    return {};
}

auto project_thumbnail::set_custom(const fs::path& project_dir, const fs::path& image_path) -> bool
{
    fs::error_code error;
    if(!asset_writer::atomic_copy_file(image_path, get_custom_path(project_dir), error))
    {
        APPLOG_WARNING("Cannot set the thumbnail of {}: {}", project_dir.string(), error.message());
        return false;
    }
    return true;
}

auto project_thumbnail::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ev = ctx.get_cached<events>();
    ev.on_project_opened.connect(sentinel_, this, &project_thumbnail::on_project_opened);
    ev.on_frame_update.connect(sentinel_, this, &project_thumbnail::on_frame_update);
    ev.on_frame_end.connect(sentinel_, this, &project_thumbnail::on_frame_end);
    return true;
}

auto project_thumbnail::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    // A readback still in flight finds the service gone and drops its image.
    sentinel_.reset();
    return true;
}

void project_thumbnail::request_capture()
{
    if(state_ == capture_state::idle)
    {
        state_ = capture_state::waiting;
        capture_time_ = clock::now();
    }
}

void project_thumbnail::on_project_opened(rtti::context& ctx)
{
    state_ = capture_state::idle;
    const fs::path project_dir = fs::resolve_protocol("app:/");
    if(find(project_dir).empty())
    {
        state_ = capture_state::waiting;
        capture_time_ = clock::now() + thumbnail_detail::PROJECT_THUMBNAIL_SETTLE_TIME;
    }
}

void project_thumbnail::on_frame_update(rtti::context& ctx, const frame_times& /*times*/)
{
    if(state_ == capture_state::waiting && clock::now() >= capture_time_)
    {
        state_ = capture_state::warming;
        warm_frames_left_ = thumbnail_detail::PROJECT_THUMBNAIL_WARM_FRAMES;
    }
    if(state_ == capture_state::warming)
    {
        // Renders this frame even while the Game tab is hidden; the flag lasts one frame.
        ctx.get_cached<hub>().get_panels().get_game_panel().set_visible_force(true);
    }
}

void project_thumbnail::on_frame_end(rtti::context& ctx, const frame_times& /*times*/)
{
    if(state_ != capture_state::warming || --warm_frames_left_ > 0)
    {
        return;
    }
    const bool has_project = ctx.get_cached<project_manager>().has_open_project();
    if(!has_project || capture(ctx) || warm_frames_left_ <= thumbnail_detail::PROJECT_THUMBNAIL_GIVE_UP_FRAMES)
    {
        state_ = capture_state::idle;
    }
}

auto project_thumbnail::capture(rtti::context& ctx) -> bool
{
    auto& panels = ctx.get_cached<hub>().get_panels();
    gfx::frame_buffer::ptr output = panels.get_game_panel().get_output(ctx);
    if(!output || !output->is_valid())
    {
        output = panels.get_scene_panel().get_output();
    }
    if(!output || !output->is_valid())
    {
        return false;
    }
    const auto& texture = output->get_texture(0);
    if(!texture || !texture->is_valid())
    {
        return false;
    }
    auto* pool = ctx.get_cached<threader>().pool.get();
    const fs::path target = get_captured_path(fs::resolve_protocol("app:/"));
    const std::weak_ptr<int> service = sentinel_;
    gfx::render_pass pass("Project Thumbnail Capture");
    return readTexture(pass.id,
                       texture->native_handle(),
                       texture->info.width,
                       texture->info.height,
                       [service, pool, target](ReadbackImage&& image)
                       {
                           if(service.expired())
                           {
                               return;
                           }
                           pool->schedule(tpp::job_desc{"Saving Project Thumbnail", target.generic_string()},
                                          [image = std::move(image), target]()
                                          {
                                              thumbnail_detail::save_thumbnail(image, target);
                                          });
                       });
}

} // namespace unravel
