#include "mcp_async.h"

#include "mcp_protocol.h"

#include <editor/system/mcp_manager.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <logging/logging.h>
#include <ser20/external/base64.hpp>
#include <uuid/uuid.h>

#include <bimg/bimg.h>
#include <bx/file.h>

#include <fstream>
#include <memory>
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
    gfx::texture_handle blit_tex = BGFX_INVALID_HANDLE;
    std::string error;
};

auto write_rgba_png(const std::filesystem::path& path,
                    uint16_t width,
                    uint16_t height,
                    const std::vector<uint8_t>& pixels,
                    std::string& error) -> bool
{
    bx::FileWriter writer;
    if(!bx::open(&writer, path.generic_string().c_str()))
    {
        error = "Failed to open PNG for write: " + path.generic_string();
        return false;
    }

    const uint32_t pitch = static_cast<uint32_t>(width) * 4u;
    bimg::imageWritePng(&writer,
                        width,
                        height,
                        pitch,
                        pixels.data(),
                        bimg::TextureFormat::RGBA8,
                        false,
                        nullptr);
    bx::close(&writer);
    return true;
}

} // namespace

auto wait_for_file(const std::filesystem::path& path,
                   std::chrono::milliseconds timeout,
                   std::chrono::milliseconds poll_interval) -> bool
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while(std::chrono::steady_clock::now() < deadline)
    {
        std::error_code ec;
        if(std::filesystem::exists(path, ec) && !ec)
        {
            const auto size = std::filesystem::file_size(path, ec);
            if(!ec && size > 0)
            {
                // Allow a short settle so the writer finishes closing the file.
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                return true;
            }
        }
        std::this_thread::sleep_for(poll_interval);
    }
    return false;
}

auto read_file_bytes(const std::filesystem::path& path, std::string& error) -> std::vector<uint8_t>
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if(!file)
    {
        error = "Failed to open file: " + path.string();
        return {};
    }

    const auto size = file.tellg();
    if(size <= 0)
    {
        error = "Empty file: " + path.string();
        return {};
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if(!file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        error = "Failed to read file: " + path.string();
        return {};
    }
    return bytes;
}

auto encode_base64(const std::vector<uint8_t>& bytes) -> std::string
{
    return ser20::base64::encode(bytes.data(), bytes.size());
}

auto make_temp_screenshot_stem(const std::string& tag) -> std::filesystem::path
{
    const auto dir = std::filesystem::temp_directory_path() / "unravel_mcp";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto name = fmt::format("ss_{}_{}", tag, hpp::to_string(generate_uuid()));
    return dir / name;
}

auto capture_fbo_screenshot(mcp_manager& mcp,
                            rtti::context& ctx,
                            const std::function<gfx::frame_buffer::ptr(rtti::context&)>& resolve_fbo,
                            const std::string& tag,
                            std::chrono::milliseconds wait_timeout) -> tool_result
{
    // bgfx::requestScreenShot only works for *window* framebuffers. Scene/Game
    // OBUFFER targets are offscreen, so we blit into a READ_BACK texture instead.
    const auto stem = make_temp_screenshot_stem(tag);
    const auto png_path = std::filesystem::path(stem.generic_string() + ".png");
    auto state = std::make_shared<fbo_capture_state>();

    const auto submitted = mcp.invoke_on_main(
        [&ctx, &resolve_fbo, state]() -> bool
        {
            auto fbo = resolve_fbo(ctx);
            if(!fbo || !fbo->is_valid())
            {
                state->error = "Viewport framebuffer is not available yet (panel may not have rendered)";
                return false;
            }

            const auto& src_tex = fbo->get_texture(0);
            if(!src_tex || !bgfx::isValid(src_tex->native_handle()))
            {
                state->error = "Viewport color texture is not available";
                return false;
            }

            state->width = src_tex->info.width;
            state->height = src_tex->info.height;
            if(state->width == 0 || state->height == 0)
            {
                state->error = "Viewport framebuffer has zero size";
                return false;
            }

            constexpr auto format = gfx::texture_format::RGBA8;
            const uint64_t flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_U_CLAMP |
                                   BGFX_SAMPLER_V_CLAMP;
            state->blit_tex = gfx::create_texture_2d(state->width, state->height, false, 1, format, flags);
            if(!bgfx::isValid(state->blit_tex))
            {
                state->error = "Failed to create readback texture";
                return false;
            }

            gfx::texture_info info{};
            gfx::calc_texture_size(info, state->width, state->height, 1, false, false, 1, format);
            state->pixels.resize(info.storageSize);

            gfx::render_pass pass("mcp_fbo_capture");
            pass.touch();
            gfx::blit(pass.id, state->blit_tex, 0, 0, src_tex->native_handle());
            state->ready_frame = gfx::read_texture(state->blit_tex, state->pixels.data());
            return true;
        },
        std::chrono::milliseconds(10000));

    if(!submitted)
    {
        return {.text = "Timed out requesting capture on main thread", .is_error = true};
    }
    if(!*submitted)
    {
        return {.text = state->error.empty() ? "Failed to request capture" : state->error, .is_error = true};
    }

    const auto deadline = std::chrono::steady_clock::now() + wait_timeout;
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
        mcp.invoke_on_main(
            [state]() -> bool
            {
                if(bgfx::isValid(state->blit_tex))
                {
                    gfx::destroy(state->blit_tex);
                    state->blit_tex = BGFX_INVALID_HANDLE;
                }
                return true;
            });
        return {.text = fmt::format("Timed out waiting for GPU readback (frame {})", state->ready_frame),
                .is_error = true};
    }

    std::string write_error;
    const auto wrote = mcp.invoke_on_main(
        [state, &png_path, &write_error]() -> bool
        {
            const bool ok = write_rgba_png(png_path, state->width, state->height, state->pixels, write_error);
            if(bgfx::isValid(state->blit_tex))
            {
                gfx::destroy(state->blit_tex);
                state->blit_tex = BGFX_INVALID_HANDLE;
            }
            return ok;
        },
        std::chrono::milliseconds(10000));

    if(!wrote)
    {
        return {.text = "Timed out writing capture PNG on main thread", .is_error = true};
    }
    if(!*wrote)
    {
        return {.text = write_error.empty() ? "Failed to write capture PNG" : write_error, .is_error = true};
    }

    std::string read_error;
    auto bytes = read_file_bytes(png_path, read_error);
    std::error_code ec;
    std::filesystem::remove(png_path, ec);

    if(bytes.empty())
    {
        return {.text = read_error.empty() ? "Failed to read capture PNG" : read_error, .is_error = true};
    }

    tool_result result;
    result.text = fmt::format(R"({{"source":{},"width":{},"height":{}}})",
                              make_json_string(tag),
                              state->width,
                              state->height);
    result.image_base64 = encode_base64(bytes);
    result.image_mime = "image/png";
    result.is_error = false;
    return result;
}

} // namespace unravel::mcp
