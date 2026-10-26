#pragma once
#include <engine/engine_export.h>

#include "render_window.h"
#include <graphics/shader.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace unravel
{
class gpu_pass_log;
class skinning_palette_buffer;

struct ENGINE_EXPORT renderer
{
    using render_window_t = std::unique_ptr<render_window>;
    /// Creates the main window through create_window or create_window_for_display.
    using main_window_factory = std::function<bool()>;

    renderer(rtti::context& ctx, cmd_line::parser& parser);
    ~renderer();

    /**
     * @brief Brings up the OS layer, lets the host create its main window, then initializes the
     *        graphics backend on that window: its backbuffer is the main window's surface, so no
     *        other window is needed to own the device.
     * @param create_main_window Must create the main window; init fails if it does not.
     */
    auto init(rtti::context& ctx, const cmd_line::parser& parser, const main_window_factory& create_main_window)
        -> bool;

    /// Whether init() got as far as bringing the graphics backend up. Teardown has to know:
    /// the renderer is constructed by engine::create, and a headless run - the test runner -
    /// never initialises it, so its destructor must not talk to a library that is not there.
    auto is_initialized() const -> bool
    {
        return is_initialized_;
    }
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Creates the main window over the usable bounds of a display. Only valid from the
     *        init() window factory. The window starts hidden; the host shows it when it is ready
     *        to draw, and a pending maximize or fullscreen applies then.
     */
    auto create_window_for_display(int index, const std::string& title, uint32_t flags)
        -> const std::unique_ptr<render_window>&;
    /**
     * @brief Creates the main window at an explicit position and size (e.g. restart restore).
     *        Only valid from the init() window factory; the window starts hidden.
     */
    auto create_window(const std::string& title,
                       int32_t x,
                       int32_t y,
                       uint32_t width,
                       uint32_t height,
                       uint32_t flags) -> const std::unique_ptr<render_window>&;
    void set_main_window(os::window&& window);
    auto get_main_window() const -> render_window*;
    /**
     * @brief Closes the main window: get_main_window() returns null from then on. The window
     *        carries the backbuffer, so it is only hidden and lives until the backend shuts down.
     */
    void close_main_window();
    /**
     * @brief Brings the main window's surface and the backbuffer to the window's current size and
     *        native window. Runs every frame, so it does not depend on a resize event reaching the
     *        renderer: a host's loading screen polls OS events itself. Android gives the window a
     *        new native window when the application returns from the background.
     */
    void sync_main_surface();
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

    /// The bone palettes of this frame's instanced skinned draws, uploaded once in frame_end.
    auto get_skinning_palettes() -> skinning_palette_buffer&;

protected:
    auto init_backend(const cmd_line::parser& parser) -> bool;
    /**
     * @brief Starts the graphics backend on the main window's current native window and size,
     *        again when the window lost its surface during the start (Android).
     */
    auto start_backend(bgfx::Init& init_data) -> bool;

    void on_os_event(rtti::context& ctx, os::event& e);
    /// Follows the fingers on the screen: the fourth one down switches the statistics overlay, once per touch.
    void on_finger_event(const os::event& e);
    /// Sets the graphics backend's debug features: the statistics overlay and the per-pass GPU timers.
    void set_debug_flags(uint32_t flags);
    void on_app_enter_background(rtti::context& ctx);
    /// Makes every cached shadow map stale (shadow::invalidate_cached_maps).
    void invalidate_cached_shadow_maps(rtti::context& ctx);
    void frame_begin(rtti::context& ctx, delta_t dt);
    void frame_end(rtti::context& ctx, delta_t dt);

    auto get_renderer_type(const cmd_line::parser& parser) const -> bgfx::RendererType::Enum;
    auto get_reset_flags(const cmd_line::parser& parser) const -> uint32_t;
    auto get_reset_flags(bool vsync) const -> uint32_t;

    uint32_t reset_flags_{};
    /// The main window; bgfx is initialized on it.
    std::unique_ptr<render_window> render_window_{};
    /// The main window after close_main_window, kept hidden until the backend shuts down.
    std::unique_ptr<render_window> closed_main_window_{};
    /// The size the backbuffer was last initialized or reset to.
    usize32_t backbuffer_size_{};
    /// The native window the backbuffer was last initialized or reset on.
    void* backbuffer_window_{};
    /// How many times the application entered the background.
    uint32_t background_count_{};
    std::string request_screenshot_{};

    /// Set once init() has brought the backend up; read by the destructor.
    bool is_initialized_{};
    /// Logs the GPU time of every render pass (--gfx-profile) or the frame times alone (--gfx-frame-log);
    /// null when off.
    std::unique_ptr<gpu_pass_log> gpu_pass_log_{};
    /// Created with the renderer, its GPU buffer on first use; released before the backend shuts down.
    std::unique_ptr<skinning_palette_buffer> skinning_palettes_{};
    /// The graphics backend's debug features (BGFX_DEBUG_*) and the scale of the overlay's text.
    uint32_t debug_flags_{};
    uint8_t debug_text_scale_{1};
    /// Whether a four-finger tap switches the statistics overlay (--gfx-stats-gesture).
    bool can_toggle_stats_by_touch_{};
    /// The fingers on the screen, while a four-finger tap can switch the overlay.
    std::vector<uint64_t> touching_fingers_;
    /// Whether the fingers on the screen switched the overlay already: lifting them all allows the next switch.
    bool has_toggled_stats_{};

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
