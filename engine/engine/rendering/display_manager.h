#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>
#include <filesystem/filesystem.h>

#include <ospp/display_mode.h>
#include <ospp/event.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace os
{
class window;
}

namespace unravel
{

/// How the game's main window covers its display.
enum class window_mode : std::uint8_t
{
    /// A window with a frame, at the chosen size.
    windowed,
    /// A window without a frame over the whole display, at the desktop's resolution.
    borderless_fullscreen,
    /// The display switches to the chosen resolution and refresh rate, and the window covers it.
    exclusive_fullscreen,
};

/// The player's display choices.
struct display_options
{
    window_mode mode{window_mode::borderless_fullscreen};
    /// The display the window is on, as an index of os::display.
    int display{};
    /// Windowed: the window's size. Exclusive fullscreen: the display's resolution. Empty for the default: the
    /// display's desktop resolution, or DEFAULT_WINDOWED_WIDTH x DEFAULT_WINDOWED_HEIGHT for a window.
    usize32_t size{};
    /// Exclusive fullscreen: the refresh rate in Hz; 0 for the desktop's.
    std::uint32_t refresh_rate{};
    bool vsync{true};
    /// The most frames per second; 0 for no cap. With vsync the display's refresh rate limits them too.
    std::uint32_t max_fps{};
};

/// The size of a window when no size was chosen.
constexpr std::uint32_t DEFAULT_WINDOWED_WIDTH = 1280;
constexpr std::uint32_t DEFAULT_WINDOWED_HEIGHT = 720;
/// The most frames per second of a minimized game that does not run in the background: it shows nothing, so its
/// frames only keep the game going.
constexpr std::uint32_t MINIMIZED_MAX_FPS = 20;

/// The name of a window mode as files and the command line write it: windowed, borderless or exclusive.
ENGINE_EXPORT auto get_window_mode_name(window_mode mode) -> const char*;

/// Reads a window mode name (get_window_mode_name); false for any other text.
ENGINE_EXPORT auto parse_window_mode(const std::string& text, window_mode& mode) -> bool;

/// Reads a resolution written as WIDTHxHEIGHT or WIDTHxHEIGHT@HZ (1920x1080@144); without the rate it is 0.
ENGINE_EXPORT auto parse_resolution(const std::string& text, usize32_t& size, std::uint32_t& refresh_rate) -> bool;

/// Writes the options as JSON, creating the file's folder; false when the file cannot be written.
ENGINE_EXPORT auto save_display_options(const fs::path& file, const display_options& options) -> bool;

/// Reads options save_display_options wrote over `options`; fields the file lacks keep their values.
ENGINE_EXPORT auto load_display_options(const fs::path& file, display_options& options) -> bool;

/// The modes in display order: each size and refresh rate once, from the smallest size and the lowest rate.
ENGINE_EXPORT auto sort_display_modes(std::vector<os::display::mode> modes) -> std::vector<os::display::mode>;

/**
 * @brief The mode closest to a requested size and refresh rate: the nearest size first, then the nearest rate.
 * @param modes The modes to choose from; empty gives an empty mode.
 * @param refresh_rate The rate in Hz; 0 for `desktop_refresh_rate`.
 */
ENGINE_EXPORT auto find_closest_display_mode(const std::vector<os::display::mode>& modes,
                                             const usize32_t& size,
                                             std::uint32_t refresh_rate,
                                             std::uint32_t desktop_refresh_rate) -> os::display::mode;

/// The size of a window that fits `available`: `requested` (the default size when empty), each side at most the
/// available one.
ENGINE_EXPORT auto fit_windowed_size(const usize32_t& requested, const usize32_t& available) -> usize32_t;

/**
 * @brief The player's display options: how the main window covers which display, vsync and the frame cap.
 *
 * Loaded from the user's folder before the main window is created and saved there when a script changes them, so
 * the game starts the way the player left it; a window the player resizes or moves keeps its size and display. The
 * command line changes them for one run. Only the player has one: the editor's window is not the game's to change.
 */
class ENGINE_EXPORT display_manager
{
public:
    display_manager(rtti::context& ctx, cmd_line::parser& parser);

    /**
     * @brief Creates the main window from the saved options and the command line, for renderer::init. The window
     *        starts hidden; fullscreen applies when the host shows it.
     */
    auto create_main_window(rtti::context& ctx, const cmd_line::parser& parser, const std::string& title) -> bool;

    /// Applies the frame pacing: vsync, which needs the graphics backend, and the frame cap.
    void init(rtti::context& ctx);

    /// Saves the size and display the player gave the window.
    auto deinit(rtti::context& ctx) -> bool;

    /// The options the window and the renderer show.
    auto get_options() const -> const display_options&;

    /**
     * @brief Changes the window's mode, size and refresh rate and saves them.
     * @param size Windowed: the window's size. Exclusive fullscreen: the display's closest resolution. Empty: the
     *        default (display_options::size).
     * @param refresh_rate Exclusive fullscreen: the closest rate in Hz; 0 for the desktop's.
     */
    void set_window(rtti::context& ctx, window_mode mode, const usize32_t& size, std::uint32_t refresh_rate);

    /// Moves the window to a display, as an index of os::display, and saves it.
    void set_display(rtti::context& ctx, int display);

    /// Turns vsync on or off and saves it.
    void set_vsync(rtti::context& ctx, bool vsync);

    /// Caps the frames per second, 0 for no cap, and saves it.
    void set_max_fps(rtti::context& ctx, std::uint32_t max_fps);

    /// Keeps the frame rate of a minimized window, or caps it at MINIMIZED_MAX_FPS, for this run. The project
    /// settings' standalone.run_in_background sets it at start.
    void set_run_in_background(rtti::context& ctx, bool run_in_background);

private:
    void on_os_event(rtti::context& ctx, os::event& e);
    /// Keeps the size and display of a window the player resized or moved.
    void remember_window(const os::window& window);
    /// Gives the window the mode, size and display of the options, as far as the display allows.
    void apply_window(os::window& window);
    void apply_exclusive_fullscreen(os::window& window);
    void load_saved_options();
    void apply_command_line(const cmd_line::parser& parser);
    /// Saves the window's options as the next run's.
    void keep_window_options();
    void save() const;

    /// Lifetime guard for the event connection.
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    /// What the window and the renderer show.
    display_options options_{};
    /// What the next run starts with: the saved options and this run's changes, without the command line's.
    display_options saved_options_{};
    /// Whether the player resized or moved the window since the last save.
    bool has_unsaved_window_{};
};

} // namespace unravel
