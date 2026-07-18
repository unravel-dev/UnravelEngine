#pragma once

#include "mcp_tool_registry.h"

#include <graphics/frame_buffer.h>

#include <chrono>
#include <context/context.hpp>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace unravel
{
class mcp_manager;
}

namespace unravel::mcp
{

/// Poll until `path` exists and is non-empty, or timeout elapses.
/// Call from an HTTP/worker thread — never from the main/render thread.
auto wait_for_file(const std::filesystem::path& path,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds(3000),
                   std::chrono::milliseconds poll_interval = std::chrono::milliseconds(32)) -> bool;

auto read_file_bytes(const std::filesystem::path& path, std::string& error) -> std::vector<uint8_t>;

auto encode_base64(const std::vector<uint8_t>& bytes) -> std::string;

auto make_temp_screenshot_stem(const std::string& tag) -> std::filesystem::path;

/// Action + wait: blit an offscreen FBO color attachment into a READ_BACK
/// texture (bgfx::requestScreenShot only supports window surfaces), wait for
/// GPU readback while frames pump, write PNG, and return an image tool_result.
auto capture_fbo_screenshot(mcp_manager& mcp,
                            rtti::context& ctx,
                            const std::function<gfx::frame_buffer::ptr(rtti::context&)>& resolve_fbo,
                            const std::string& tag,
                            std::chrono::milliseconds wait_timeout = std::chrono::milliseconds(3000))
    -> tool_result;

} // namespace unravel::mcp
