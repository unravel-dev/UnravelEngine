#pragma once
#include <engine/engine_export.h>

#include "render_window.h"
#include <graphics/shader.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace unravel
{

struct ENGINE_EXPORT renderer
{
    using render_window_t = std::unique_ptr<render_window>;

    renderer(rtti::context& ctx, cmd_line::parser& parser);
    ~renderer();

    auto init(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    auto create_window_for_display(int index, const std::string& title, uint32_t flags)
        -> const std::unique_ptr<render_window>&;
    /**
     * @brief Creates the main window at an explicit position and size (e.g. restart restore).
     */
    auto create_window(const std::string& title,
                       int32_t x,
                       int32_t y,
                       uint32_t width,
                       uint32_t height,
                       uint32_t flags) -> const std::unique_ptr<render_window>&;
    void set_main_window(os::window&& window);
    auto get_main_window() const -> render_window*;
    void close_main_window();
    void request_screenshot(const std::string& file);

    /**
     * @brief Replaces any prior --window / -W args with the current main window geometry.
     *        Format: --window=x,y,w,h,maximized (single argv token; maximized is 0 or 1).
     */
    void prepare_restart(std::vector<std::string>& arguments);

    /**
     * @brief Parses a window geometry string "x,y,w,h" or "x,y,w,h,maximized".
     */
    static auto parse_window_geometry(const std::string& value,
                                      int32_t& x,
                                      int32_t& y,
                                      uint32_t& width,
                                      uint32_t& height,
                                      bool& maximized) -> bool;

    auto get_vsync() const -> bool;
    void set_vsync(bool vsync);

protected:
    auto init_backend(const cmd_line::parser& parser) -> bool;

    void on_os_event(rtti::context& ctx, os::event& e);
    void frame_begin(rtti::context& ctx, delta_t dt);
    void frame_end(rtti::context& ctx, delta_t dt);

    auto get_renderer_type(const cmd_line::parser& parser) const -> gfx::renderer_type;
    auto get_reset_flags(const cmd_line::parser& parser) const -> uint32_t;
    auto get_reset_flags(bool vsync) const -> uint32_t;

    uint32_t reset_flags_{};
    /// engine windows
    std::unique_ptr<os::window> init_window_{};
    std::unique_ptr<render_window> render_window_{};
    std::string request_screenshot_{};

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
