#pragma once

#include "mcp_tool_registry.h"

#include <filesystem/filesystem.h>
#include <graphics/frame_buffer.h>

#include <chrono>
#include <context/context.hpp>
#include <functional>
#include <string>
#include <vector>

namespace unravel
{
class mcp_manager;
class panel_base;
}

namespace unravel::mcp
{

auto encode_base64(const std::vector<uint8_t>& bytes) -> std::string;

/// The folder MCP writes its own output files to (captures, measurements): <temp>/unravel_mcp, created
/// on first use. Never the project: those files are not assets.
auto mcp_output_directory() -> fs::path;


struct capture_options
{
    /// Frames rendered after the request before the capture, e.g. to let temporal AA and GI settle after
    /// a change. The capture always waits for at least one frame rendered after the request.
    uint32_t settle_frames{0};
    /// Downscale factor in (0, 1]; uses bimg linear resize. Prefer 0.5 to cut tokens.
    double scale{1.0};
    /// The panel that owns the framebuffer. The capture keeps it rendering until the blit, also while its
    /// tab is hidden; a hidden panel's view, torn down while hidden, first renders warm-up frames so it is
    /// not captured without history. Called on the main thread.
    std::function<panel_base&(rtti::context&)> resolve_panel;
};

/// Action + wait: blit an offscreen FBO color attachment into a READ_BACK
/// texture (bgfx::requestScreenShot only supports window surfaces), wait for
/// GPU readback while frames pump, scale/encode via bimg PNG, return image tool_result.
auto capture_fbo_screenshot(mcp_manager& mcp,
                            rtti::context& ctx,
                            const std::function<gfx::frame_buffer::ptr(rtti::context&)>& resolve_fbo,
                            const std::string& tag,
                            const capture_options& options = {}) -> tool_result;

} // namespace unravel::mcp
