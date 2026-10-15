#include "mcp_async.h"

#include "mcp_protocol.h"

#include <editor/hub/panels/panel_base.h>
#include <editor/system/mcp_manager.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <logging/logging.h>
#include <ser20/external/base64.hpp>

#include <bimg/bimg.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/readerwriter.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>

namespace unravel::mcp
{
namespace
{

struct fbo_capture_state
{
    std::vector<uint8_t> pixels;
    uint32_t ready_frame{0};
    uint16_t width{0};
    uint16_t height{0};
    /// Rows arrive bottom-up: OpenGL render targets keep their origin at the bottom left.
    bool is_bottom_up{false};
    bgfx::TextureHandle blit_tex = BGFX_INVALID_HANDLE;
    std::string error;
};

/// How long a capture waits for its readback however short the caller's wait_ms: the READ_BACK buffer must outlive
/// bgfx's copy, which the render thread performs when it executes the frame the read was issued in.
constexpr auto readback_guard_timeout = std::chrono::milliseconds(10000);

/// How long a capture waits for the panel to render a requested frame: the first render of a hidden view can
/// build its whole pipeline.
constexpr auto render_guard_timeout = std::chrono::milliseconds(5000);

/// Time allowed per frame a capture waits for, on top of render_guard_timeout, at a 10 fps worst case.
constexpr auto frame_budget = std::chrono::milliseconds(100);

/// Frames each request keeps the panel rendering: enough to bridge the gap between two polls from the worker.
constexpr uint32_t capture_hold_frames = 8;

/// Frames a hidden panel renders before it is captured: its view was torn down while hidden, and temporal AA and GI
/// need this much history to settle.
constexpr uint32_t hidden_warmup_frames = 16;

/// Outcome of one attempt to submit the capture blit on the main thread.
enum class blit_status
{
    waiting_for_render,
    submitted,
    failed,
};

/// Keeps a capture whose readback never landed alive for the rest of the session: its pixel buffer and blit texture
/// may still be the target of a pending copy, and freeing them would hand the render thread freed memory.
auto abandon_capture_state(std::shared_ptr<fbo_capture_state> state) -> void
{
    static std::mutex abandoned_mutex;
    static std::vector<std::shared_ptr<fbo_capture_state>> abandoned;
    std::lock_guard<std::mutex> lock(abandoned_mutex);
    abandoned.push_back(std::move(state));
}

/// Blits the framebuffer's color into a READ_BACK texture and requests its readback. Main thread only.
auto submit_fbo_blit(rtti::context& ctx,
                     const std::function<gfx::frame_buffer::ptr(rtti::context&)>& resolve_fbo,
                     fbo_capture_state& state) -> bool
{
    auto fbo = resolve_fbo(ctx);
    if(!fbo || !fbo->is_valid())
    {
        state.error = "Viewport framebuffer is not available yet (panel may not have rendered)";
        return false;
    }

    const auto& src_tex = fbo->get_texture(0);
    if(!src_tex || !bgfx::isValid(src_tex->native_handle()))
    {
        state.error = "Viewport color texture is not available";
        return false;
    }

    state.width = src_tex->info.width;
    state.height = src_tex->info.height;
    state.is_bottom_up = bgfx::getCaps()->originBottomLeft;
    if(state.width == 0 || state.height == 0)
    {
        state.error = "Viewport framebuffer has zero size";
        return false;
    }

    constexpr auto format = bgfx::TextureFormat::RGBA8;
    const uint64_t flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_U_CLAMP |
                           BGFX_SAMPLER_V_CLAMP;
    state.blit_tex = bgfx::createTexture2D(state.width, state.height, false, 1, format, flags);
    if(!bgfx::isValid(state.blit_tex))
    {
        state.error = "Failed to create readback texture";
        return false;
    }

    bgfx::TextureInfo info{};
    bgfx::calcTextureSize(info, state.width, state.height, 1, false, false, 1, format);
    state.pixels.resize(info.storageSize);

    gfx::render_pass pass("mcp_fbo_capture");
    pass.touch();
    bgfx::blit(pass.id,
               bgfx::TextureRegion{.handle = state.blit_tex},
               bgfx::TextureRegion{.handle = src_tex->native_handle()});
    state.ready_frame = bgfx::read(bgfx::TextureRegion{.handle = state.blit_tex}, state.pixels.data());
    return true;
}

auto bimg_allocator() -> bx::AllocatorI*
{
    static bx::DefaultAllocator allocator;
    return &allocator;
}

/**
 * @brief Scale RGBA8 pixels with bimg (RGBA32F linear resize). scale in (0,1].
 */
auto scale_rgba8_bimg(const std::vector<uint8_t>& src,
                      uint16_t src_w,
                      uint16_t src_h,
                      double scale,
                      std::vector<uint8_t>& out_pixels,
                      uint16_t& out_w,
                      uint16_t& out_h,
                      std::string& error) -> bool
{
    if(src_w == 0 || src_h == 0 || src.size() < static_cast<size_t>(src_w) * src_h * 4u)
    {
        error = "Invalid source image for scale";
        return false;
    }
    if(scale <= 0.0)
    {
        scale = 1.0;
    }
    if(scale > 1.0)
    {
        scale = 1.0;
    }
    out_w = static_cast<uint16_t>(std::max(1.0, std::floor(static_cast<double>(src_w) * scale)));
    out_h = static_cast<uint16_t>(std::max(1.0, std::floor(static_cast<double>(src_h) * scale)));
    if(out_w == src_w && out_h == src_h)
    {
        out_pixels = src;
        return true;
    }

    auto* allocator = bimg_allocator();
    // Depth 0: bimg reserves a non-zero depth for volume textures.
    bimg::ImageContainer* src_rgba8 =
        bimg::imageAlloc(allocator, bimg::TextureFormat::RGBA8, src_w, src_h, 0, 1, false, false, src.data());
    if(!src_rgba8)
    {
        error = "bimg::imageAlloc failed for source RGBA8";
        return false;
    }

    bimg::ImageContainer* src_f32 = bimg::imageConvert(allocator, bimg::TextureFormat::RGBA32F, *src_rgba8);
    bimg::imageFree(src_rgba8);
    if(!src_f32)
    {
        error = "bimg::imageConvert to RGBA32F failed";
        return false;
    }

    bimg::ImageContainer* dst_f32 =
        bimg::imageAlloc(allocator, bimg::TextureFormat::RGBA32F, out_w, out_h, 0, 1, false, false);
    if(!dst_f32)
    {
        bimg::imageFree(src_f32);
        error = "bimg::imageAlloc failed for resized RGBA32F";
        return false;
    }

    if(!bimg::imageResizeRgba32fLinear(dst_f32, src_f32))
    {
        bimg::imageFree(src_f32);
        bimg::imageFree(dst_f32);
        error = "bimg::imageResizeRgba32fLinear failed";
        return false;
    }
    bimg::imageFree(src_f32);

    bimg::ImageContainer* dst_rgba8 = bimg::imageConvert(allocator, bimg::TextureFormat::RGBA8, *dst_f32);
    bimg::imageFree(dst_f32);
    if(!dst_rgba8)
    {
        error = "bimg::imageConvert to RGBA8 failed";
        return false;
    }

    bimg::ImageMip mip{};
    if(!bimg::imageGetRawData(*dst_rgba8, 0, 0, dst_rgba8->m_data, dst_rgba8->m_size, mip))
    {
        bimg::imageFree(dst_rgba8);
        error = "bimg::imageGetRawData failed after resize";
        return false;
    }

    out_pixels.assign(mip.m_data, mip.m_data + mip.m_size);
    out_w = static_cast<uint16_t>(mip.m_width);
    out_h = static_cast<uint16_t>(mip.m_height);
    bimg::imageFree(dst_rgba8);
    return true;
}

/// Encodes RGBA8 @p pixels as PNG bytes, top row first; @p is_bottom_up reverses rows stored bottom-up.
auto encode_rgba_png(uint16_t width,
                     uint16_t height,
                     const std::vector<uint8_t>& pixels,
                     bool is_bottom_up,
                     std::vector<uint8_t>& out,
                     std::string& error) -> bool
{
    bx::MemoryBlock memory(bimg_allocator());
    bx::MemoryWriter writer(&memory);
    const uint32_t pitch = static_cast<uint32_t>(width) * 4u;
    bx::Error err;
    bimg::imageWritePng(&writer, width, height, pitch, pixels.data(), bimg::TextureFormat::RGBA8, is_bottom_up, &err);
    if(!err.isOk())
    {
        error = "bimg::imageWritePng failed";
        return false;
    }
    // The block grows in chunks; the writer's end is how much of it holds the PNG.
    const auto written = static_cast<size_t>(writer.seek(0, bx::Whence::End));
    const auto* bytes = static_cast<const uint8_t*>(memory.more(0));
    out.assign(bytes, bytes + written);
    return true;
}

} // namespace

auto encode_base64(const std::vector<uint8_t>& bytes) -> std::string
{
    return ser20::base64::encode(bytes.data(), bytes.size());
}

auto mcp_output_directory() -> fs::path
{
    const auto dir = fs::temp_directory_path() / "unravel_mcp";
    fs::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

auto capture_fbo_screenshot(mcp_manager& mcp,
                            rtti::context& ctx,
                            const std::function<gfx::frame_buffer::ptr(rtti::context&)>& resolve_fbo,
                            const std::string& tag,
                            const capture_options& options) -> tool_result
{
    // bgfx::requestScreenShot only works for *window* framebuffers. Scene/Game
    // OBUFFER targets are offscreen, so we blit into a READ_BACK texture instead.
    auto state = std::make_shared<fbo_capture_state>();

    // A panel whose tab is hidden releases its framebuffer every frame it does not render. The hold keeps it
    // rendering; the blit waits for the last frame the capture asked for.
    uint32_t frames_to_wait = options.settle_frames;
    const auto requested_frame = mcp.invoke_on_main(
        [&ctx, &options, &frames_to_wait]() -> uint32_t
        {
            if(options.resolve_panel)
            {
                auto& panel = options.resolve_panel(ctx);
                frames_to_wait += panel.is_visible() ? 0 : hidden_warmup_frames;
                panel.hold_visible_force(capture_hold_frames + frames_to_wait);
            }
            return gfx::get_render_frame() + frames_to_wait;
        },
        std::chrono::milliseconds(10000));
    if(!requested_frame)
    {
        return {.text = "Timed out requesting a render on main thread", .is_error = true};
    }

    const auto submit_blit = [&ctx, &resolve_fbo, &options, state, last_frame = *requested_frame]() -> blit_status
    {
        if(options.resolve_panel)
        {
            // Renewed every attempt: the panel also renders the blit's frame, so the framebuffer outlives the blit.
            options.resolve_panel(ctx).hold_visible_force(capture_hold_frames);
        }
        if(gfx::get_render_frame() <= last_frame)
        {
            return blit_status::waiting_for_render;
        }
        const bool submitted = submit_fbo_blit(ctx, resolve_fbo, *state);
        return submitted ? blit_status::submitted : blit_status::failed;
    };

    const auto render_deadline = std::chrono::steady_clock::now() + render_guard_timeout + frame_budget * frames_to_wait;
    blit_status status = blit_status::waiting_for_render;
    while(status == blit_status::waiting_for_render)
    {
        const auto attempt = mcp.invoke_on_main(submit_blit, std::chrono::milliseconds(10000));
        if(!attempt)
        {
            return {.text = "Timed out requesting capture on main thread", .is_error = true};
        }
        status = *attempt;
        if(status == blit_status::waiting_for_render)
        {
            if(std::chrono::steady_clock::now() >= render_deadline)
            {
                return {.text = "Timed out waiting for the panel to render", .is_error = true};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    if(status == blit_status::failed)
    {
        return {.text = state->error.empty() ? "Failed to request capture" : state->error, .is_error = true};
    }

    // The READ_BACK buffer must outlive bgfx's copy (freeing it earlier crashed the editor on the render thread);
    // the loop still returns as soon as the readback lands.
    const auto deadline = std::chrono::steady_clock::now() + readback_guard_timeout;
    bool ready = false;
    while(std::chrono::steady_clock::now() < deadline)
    {
        auto frame_ready = mcp.invoke_on_main(
            [state]() -> bool
            {
                return gfx::get_render_frame() >= state->ready_frame;
            },
            std::chrono::milliseconds(2000));
        if(frame_ready && *frame_ready)
        {
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(32));
    }

    if(!ready)
    {
        // The readback never landed: its buffer and blit texture may still be the target of a pending copy.
        const uint32_t ready_frame = state->ready_frame;
        abandon_capture_state(std::move(state));
        return {.text = fmt::format("Timed out waiting for GPU readback (frame {})", ready_frame), .is_error = true};
    }

    mcp.invoke_on_main(
        [state]() -> bool
        {
            if(bgfx::isValid(state->blit_tex))
            {
                bgfx::destroy(state->blit_tex);
                state->blit_tex = BGFX_INVALID_HANDLE;
            }
            return true;
        },
        std::chrono::milliseconds(10000));

    uint16_t out_w = state->width;
    uint16_t out_h = state->height;
    std::vector<uint8_t> pixels;
    std::string scale_error;
    if(!scale_rgba8_bimg(state->pixels, state->width, state->height, options.scale, pixels, out_w, out_h, scale_error))
    {
        return {.text = scale_error, .is_error = true};
    }

    std::vector<uint8_t> bytes;
    std::string encode_error;
    if(!encode_rgba_png(out_w, out_h, pixels, state->is_bottom_up, bytes, encode_error))
    {
        return {.text = encode_error, .is_error = true};
    }

    tool_result result;
    result.text = fmt::format(R"({{"source":{},"width":{},"height":{},"format":"png","scale":{:.6g}}})",
                              make_json_string(tag),
                              out_w,
                              out_h,
                              static_cast<double>(out_w) / static_cast<double>(state->width));
    result.image_base64 = encode_base64(bytes);
    result.image_mime = "image/png";
    result.is_error = false;
    return result;
}

} // namespace unravel::mcp
