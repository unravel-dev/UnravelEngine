#include "display_manager.h"

#include <engine/events.h>
#include <engine/rendering/renderer.h>

#include <logging/logging.h>
#include <ospp/window.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <simulation/simulation.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <tuple>

namespace unravel
{
namespace
{
/// The player's display options. In the user's folder: the install folder may not be writable.
constexpr const char* DISPLAY_OPTIONS_PATH = "user:/settings/display.cfg";
constexpr const char* WINDOW_MODE_OPTION = "window-mode";
constexpr const char* RESOLUTION_OPTION = "resolution";
constexpr const char* DISPLAY_OPTION = "display";
constexpr const char* MAX_FPS_OPTION = "max-fps";
/// --display when the command line names no display.
constexpr int NO_DISPLAY = -1;
/// --max-fps when the command line sets no cap.
constexpr int NO_MAX_FPS = -1;

struct window_mode_name
{
    window_mode mode;
    const char* name;
};

constexpr std::array<window_mode_name, 3> WINDOW_MODE_NAMES{{
    {window_mode::windowed, "windowed"},
    {window_mode::borderless_fullscreen, "borderless"},
    {window_mode::exclusive_fullscreen, "exclusive"},
}};

auto is_empty_size(const usize32_t& size) -> bool
{
    return size.width == 0 || size.height == 0;
}

/// A display index, or the primary display for one that is not there (unplugged since the options were saved).
auto resolve_display(int display) -> int
{
    const int count = os::display::get_available_displays_count();
    return display >= 0 && display < count ? display : os::display::get_primary_display_index();
}

/// Reads an unsigned number from `begin`; the end of the number, or nullptr without one.
auto read_number(const char* begin, const char* end, std::uint32_t& value) -> const char*
{
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} ? result.ptr : nullptr;
}

auto get_distance(std::uint32_t lhs, std::uint32_t rhs) -> std::uint32_t
{
    return lhs > rhs ? lhs - rhs : rhs - lhs;
}

auto get_max_fps_text(std::uint32_t max_fps) -> std::string
{
    return max_fps > 0 ? std::to_string(max_fps) : std::string("uncapped");
}

/// Sets an option; false when it already had the value.
template<typename T>
auto update_option(T& option, const T& value) -> bool
{
    if(option == value)
    {
        return false;
    }
    option = value;
    return true;
}

/// Leaves fullscreen and gives the window its size, centered in the display's usable area.
void apply_windowed(os::window& window, display_options& options)
{
    window.set_fullscreen(false);
    window.clear_fullscreen_mode();
    if(window.is_maximized())
    {
        window.restore();
    }
    const auto usable = os::display::get_usable_bounds(options.display);
    const auto frame = window.get_frame_size();
    const auto frame_width = static_cast<std::uint32_t>(std::max(frame.left + frame.right, 0));
    const auto frame_height = static_cast<std::uint32_t>(std::max(frame.top + frame.bottom, 0));
    const usize32_t available{usable.w - std::min(usable.w, frame_width), usable.h - std::min(usable.h, frame_height)};
    options.size = fit_windowed_size(options.size, available);
    // The size after the move: a display of another scale rescales the window that moves onto it.
    window.set_position(usable.x + frame.left + static_cast<int>(available.width - options.size.width) / 2,
                        usable.y + frame.top + static_cast<int>(available.height - options.size.height) / 2);
    window.set_size(options.size.width, options.size.height);
}

/// Covers the display at the desktop's resolution. A fullscreen window moves to another display through a
/// window: fullscreen goes to the display at the window's position.
void apply_borderless_fullscreen(os::window& window, const display_options& options)
{
    if(window.is_fullscreen() && window.get_display_index() != options.display)
    {
        window.set_fullscreen(false);
    }
    window.clear_fullscreen_mode();
    if(!window.is_fullscreen())
    {
        const auto bounds = os::display::get_bounds(options.display);
        window.set_position(bounds.x, bounds.y);
    }
    window.set_fullscreen(true);
}

} // namespace

SAVE_INLINE(display_options)
{
    const std::string mode = get_window_mode_name(obj.mode);
    try_save(ar, ser20::make_nvp("mode", mode));
    try_save(ar, ser20::make_nvp("display", obj.display));
    try_save(ar, ser20::make_nvp("width", obj.size.width));
    try_save(ar, ser20::make_nvp("height", obj.size.height));
    try_save(ar, ser20::make_nvp("refresh_rate", obj.refresh_rate));
    try_save(ar, ser20::make_nvp("vsync", obj.vsync));
    try_save(ar, ser20::make_nvp("max_fps", obj.max_fps));
}

LOAD_INLINE(display_options)
{
    std::string mode;
    if(try_load(ar, ser20::make_nvp("mode", mode)))
    {
        parse_window_mode(mode, obj.mode);
    }
    try_load(ar, ser20::make_nvp("display", obj.display));
    try_load(ar, ser20::make_nvp("width", obj.size.width));
    try_load(ar, ser20::make_nvp("height", obj.size.height));
    try_load(ar, ser20::make_nvp("refresh_rate", obj.refresh_rate));
    try_load(ar, ser20::make_nvp("vsync", obj.vsync));
    try_load(ar, ser20::make_nvp("max_fps", obj.max_fps));
}

auto get_window_mode_name(window_mode mode) -> const char*
{
    const auto it = std::find_if(WINDOW_MODE_NAMES.begin(),
                                 WINDOW_MODE_NAMES.end(),
                                 [mode](const window_mode_name& entry)
                                 {
                                     return entry.mode == mode;
                                 });
    return it != WINDOW_MODE_NAMES.end() ? it->name : "";
}

auto parse_window_mode(const std::string& text, window_mode& mode) -> bool
{
    const auto it = std::find_if(WINDOW_MODE_NAMES.begin(),
                                 WINDOW_MODE_NAMES.end(),
                                 [&text](const window_mode_name& entry)
                                 {
                                     return text == entry.name;
                                 });
    if(it == WINDOW_MODE_NAMES.end())
    {
        return false;
    }
    mode = it->mode;
    return true;
}

auto parse_resolution(const std::string& text, usize32_t& size, std::uint32_t& refresh_rate) -> bool
{
    const char* end = text.data() + text.size();
    usize32_t parsed{};
    std::uint32_t parsed_rate = 0;
    const char* next = read_number(text.data(), end, parsed.width);
    if(next == nullptr || next == end || (*next != 'x' && *next != 'X'))
    {
        return false;
    }
    next = read_number(next + 1, end, parsed.height);
    if(next != nullptr && next != end && *next == '@')
    {
        next = read_number(next + 1, end, parsed_rate);
    }
    if(next != end || is_empty_size(parsed))
    {
        return false;
    }
    size = parsed;
    refresh_rate = parsed_rate;
    return true;
}

auto save_display_options(const fs::path& file, const display_options& options) -> bool
{
    fs::error_code err;
    fs::create_directories(file.parent_path(), err);
    std::ofstream stream(file);
    if(!stream.good())
    {
        return false;
    }
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("display_options", options));
    }
    return stream.good();
}

auto load_display_options(const fs::path& file, display_options& options) -> bool
{
    std::ifstream stream(file);
    if(!stream.good())
    {
        return false;
    }
    // A file cut short by a crash or a full disk must not stop the game: it starts with what it had.
    try
    {
        display_options loaded = options;
        auto ar = ser20::create_iarchive_associative(stream);
        if(!try_load(ar, ser20::make_nvp("display_options", loaded)))
        {
            return false;
        }
        options = loaded;
        return true;
    }
    catch(const std::exception&)
    {
        return false;
    }
}

auto sort_display_modes(std::vector<os::display::mode> modes) -> std::vector<os::display::mode>
{
    const auto key = [](const os::display::mode& mode)
    {
        return std::make_tuple(mode.w, mode.h, mode.refresh_rate);
    };
    std::sort(modes.begin(),
              modes.end(),
              [&key](const os::display::mode& lhs, const os::display::mode& rhs)
              {
                  return key(lhs) < key(rhs);
              });
    const auto last = std::unique(modes.begin(),
                                  modes.end(),
                                  [&key](const os::display::mode& lhs, const os::display::mode& rhs)
                                  {
                                      return key(lhs) == key(rhs);
                                  });
    modes.erase(last, modes.end());
    return modes;
}

auto find_closest_display_mode(const std::vector<os::display::mode>& modes,
                               const usize32_t& size,
                               std::uint32_t refresh_rate,
                               std::uint32_t desktop_refresh_rate) -> os::display::mode
{
    const std::uint32_t target_rate = refresh_rate > 0 ? refresh_rate : desktop_refresh_rate;
    const auto distance = [&](const os::display::mode& mode)
    {
        const std::uint64_t size_distance = std::uint64_t(get_distance(mode.w, size.width)) + get_distance(mode.h, size.height);
        return std::make_tuple(size_distance, get_distance(mode.refresh_rate, target_rate));
    };
    const auto closest = std::min_element(modes.begin(),
                                          modes.end(),
                                          [&distance](const os::display::mode& lhs, const os::display::mode& rhs)
                                          {
                                              return distance(lhs) < distance(rhs);
                                          });
    return closest != modes.end() ? *closest : os::display::mode{};
}

auto fit_windowed_size(const usize32_t& requested, const usize32_t& available) -> usize32_t
{
    const usize32_t size = is_empty_size(requested) ? usize32_t{DEFAULT_WINDOWED_WIDTH, DEFAULT_WINDOWED_HEIGHT} : requested;
    if(is_empty_size(available))
    {
        return size;
    }
    return {std::min(size.width, available.width), std::min(size.height, available.height)};
}

display_manager::display_manager(rtti::context& ctx, cmd_line::parser& parser)
{
    parser.set_optional<std::string>("",
                                     WINDOW_MODE_OPTION,
                                     "",
                                     "Show the game windowed, borderless (a window over the whole display) or exclusive "
                                     "(the display switches to the resolution), for this run only.");
    parser.set_optional<std::string>("",
                                     RESOLUTION_OPTION,
                                     "",
                                     "The window's size, or the display's resolution in exclusive fullscreen, as "
                                     "WIDTHxHEIGHT or WIDTHxHEIGHT@HZ, for this run only.");
    parser.set_optional<int>("",
                             DISPLAY_OPTION,
                             NO_DISPLAY,
                             "The display to show the game on, from 0, for this run only.");
    parser.set_optional<int>("",
                             MAX_FPS_OPTION,
                             NO_MAX_FPS,
                             "The most frames per second, 0 for no cap, for this run only.");
    ctx.get_cached<events>().on_os_event.connect(sentinel_, this, &display_manager::on_os_event);
}

auto display_manager::create_main_window(rtti::context& ctx, const cmd_line::parser& parser, const std::string& title)
    -> bool
{
    load_saved_options();
    options_ = saved_options_;
    apply_command_line(parser);
    auto& rend = ctx.get_cached<renderer>();
    std::string geometry;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool is_maximized = false;
    if(parser.try_get("window", geometry) &&
       renderer::parse_window_geometry(geometry, x, y, width, height, is_maximized))
    {
        options_.mode = window_mode::windowed;
        options_.size = {width, height};
        rend.create_window(title, x, y, width, height, os::window::resizable | (is_maximized ? os::window::maximized : 0u));
        return rend.get_main_window() != nullptr;
    }
    options_.display = resolve_display(options_.display);
    const auto bounds = os::display::get_bounds(options_.display);
    rend.create_window(title, bounds.x, bounds.y, bounds.w, bounds.h, os::window::resizable);
    auto* main_window = rend.get_main_window();
    if(main_window == nullptr)
    {
        return false;
    }
    apply_window(main_window->get_window());
    APPLOG_INFO("Display: {} on display {}, {}x{}, vsync {}, max fps {}.",
                get_window_mode_name(options_.mode),
                options_.display,
                options_.size.width,
                options_.size.height,
                options_.vsync ? "on" : "off",
                get_max_fps_text(options_.max_fps));
    return true;
}

void display_manager::init(rtti::context& ctx)
{
    auto& rend = ctx.get_cached<renderer>();
    if(rend.get_vsync() != options_.vsync)
    {
        rend.set_vsync(options_.vsync);
    }
    ctx.get_cached<simulation>().set_max_fps(options_.max_fps);
}

auto display_manager::deinit(rtti::context& /*ctx*/) -> bool
{
    if(has_unsaved_window_)
    {
        save();
    }
    return true;
}

auto display_manager::get_options() const -> const display_options&
{
    return options_;
}

void display_manager::set_window(rtti::context& ctx, window_mode mode, const usize32_t& size, std::uint32_t refresh_rate)
{
    auto* main_window = ctx.get_cached<renderer>().get_main_window();
    if(main_window == nullptr)
    {
        return;
    }
    const bool is_unchanged = mode == options_.mode && size == options_.size && refresh_rate == options_.refresh_rate;
    if(!is_unchanged)
    {
        options_.mode = mode;
        options_.size = size;
        options_.refresh_rate = refresh_rate;
        apply_window(main_window->get_window());
    }
    keep_window_options();
}

void display_manager::set_display(rtti::context& ctx, int display)
{
    auto* main_window = ctx.get_cached<renderer>().get_main_window();
    if(main_window == nullptr)
    {
        return;
    }
    options_.display = resolve_display(display);
    if(options_.display != main_window->get_window().get_display_index())
    {
        apply_window(main_window->get_window());
    }
    keep_window_options();
}

void display_manager::set_vsync(rtti::context& ctx, bool vsync)
{
    // The options this run shows may come from the command line; the saved ones are what the next run starts with.
    if(update_option(options_.vsync, vsync))
    {
        ctx.get_cached<renderer>().set_vsync(vsync);
    }
    if(update_option(saved_options_.vsync, vsync))
    {
        save();
    }
}

void display_manager::set_max_fps(rtti::context& ctx, std::uint32_t max_fps)
{
    if(update_option(options_.max_fps, max_fps))
    {
        ctx.get_cached<simulation>().set_max_fps(max_fps);
    }
    if(update_option(saved_options_.max_fps, max_fps))
    {
        save();
    }
}

void display_manager::set_run_in_background(rtti::context& ctx, bool run_in_background)
{
    ctx.get_cached<simulation>().set_max_inactive_fps(run_in_background ? 0 : MINIMIZED_MAX_FPS);
}

void display_manager::on_os_event(rtti::context& ctx, os::event& e)
{
    if(e.type != os::events::window ||
       (e.window.type != os::window_event_id::resized && e.window.type != os::window_event_id::moved))
    {
        return;
    }
    auto* main_window = ctx.get_cached<renderer>().get_main_window();
    if(main_window != nullptr && main_window->get_window().get_id() == e.window.window_id)
    {
        remember_window(main_window->get_window());
    }
}

void display_manager::remember_window(const os::window& window)
{
    const bool is_resizable = !window.is_fullscreen() && !window.is_maximized() && !window.is_minimized();
    if(options_.mode != window_mode::windowed || !is_resizable)
    {
        return;
    }
    const auto size = window.get_size();
    options_.size = {size.w, size.h};
    options_.display = window.get_display_index();
    // A window the command line asked for leaves the saved options alone.
    const bool is_saved_window = saved_options_.mode == window_mode::windowed;
    if(is_saved_window && (saved_options_.size != options_.size || saved_options_.display != options_.display))
    {
        saved_options_.size = options_.size;
        saved_options_.display = options_.display;
        has_unsaved_window_ = true;
    }
}

void display_manager::apply_window(os::window& window)
{
    options_.display = resolve_display(options_.display);
    switch(options_.mode)
    {
        case window_mode::windowed:
            apply_windowed(window, options_);
            return;
        case window_mode::borderless_fullscreen:
            apply_borderless_fullscreen(window, options_);
            return;
        case window_mode::exclusive_fullscreen:
            apply_exclusive_fullscreen(window);
            return;
    }
}

void display_manager::apply_exclusive_fullscreen(os::window& window)
{
    const auto desktop = os::display::get_desktop_mode(options_.display);
    const usize32_t requested = is_empty_size(options_.size) ? usize32_t{desktop.w, desktop.h} : options_.size;
    const auto modes = os::display::get_available_modes(options_.display);
    const auto mode = find_closest_display_mode(modes, requested, options_.refresh_rate, desktop.refresh_rate);
    if(mode.w == 0 || mode.h == 0 || !window.set_fullscreen_mode(options_.display, mode))
    {
        APPLOG_WARNING("Display {} has no resolution to switch to. The game covers it at the desktop's resolution.",
                       options_.display);
        options_.mode = window_mode::borderless_fullscreen;
        apply_borderless_fullscreen(window, options_);
        return;
    }
    options_.size = {mode.w, mode.h};
    options_.refresh_rate = mode.refresh_rate;
    window.set_fullscreen(true);
}

void display_manager::load_saved_options()
{
    saved_options_ = {};
    saved_options_.display = os::display::get_primary_display_index();
    const auto file = fs::resolve_protocol(DISPLAY_OPTIONS_PATH);
    fs::error_code err;
    if(fs::exists(file, err) && !load_display_options(file, saved_options_))
    {
        APPLOG_WARNING("The display options in {} could not be read. The game starts with the defaults.",
                       file.generic_string());
    }
    saved_options_.display = resolve_display(saved_options_.display);
}

void display_manager::apply_command_line(const cmd_line::parser& parser)
{
    std::string text;
    if(parser.try_get(WINDOW_MODE_OPTION, text) && !text.empty() && !parse_window_mode(text, options_.mode))
    {
        APPLOG_WARNING("--{} {} is not windowed, borderless or exclusive.", WINDOW_MODE_OPTION, text);
    }
    text.clear();
    if(parser.try_get(RESOLUTION_OPTION, text) && !text.empty() &&
       !parse_resolution(text, options_.size, options_.refresh_rate))
    {
        APPLOG_WARNING("--{} {} is not WIDTHxHEIGHT or WIDTHxHEIGHT@HZ.", RESOLUTION_OPTION, text);
    }
    int display = NO_DISPLAY;
    if(parser.try_get(DISPLAY_OPTION, display) && display != NO_DISPLAY)
    {
        options_.display = display;
    }
    bool is_vsync_off = false;
    if(parser.try_get("novsync", is_vsync_off) && is_vsync_off)
    {
        options_.vsync = false;
    }
    int max_fps = NO_MAX_FPS;
    if(parser.try_get(MAX_FPS_OPTION, max_fps) && max_fps >= 0)
    {
        options_.max_fps = static_cast<std::uint32_t>(max_fps);
    }
}

void display_manager::keep_window_options()
{
    const bool is_kept = saved_options_.mode == options_.mode && saved_options_.display == options_.display &&
                         saved_options_.size == options_.size && saved_options_.refresh_rate == options_.refresh_rate;
    if(is_kept && !has_unsaved_window_)
    {
        return;
    }
    saved_options_.mode = options_.mode;
    saved_options_.display = options_.display;
    saved_options_.size = options_.size;
    saved_options_.refresh_rate = options_.refresh_rate;
    has_unsaved_window_ = false;
    save();
}

void display_manager::save() const
{
    const auto file = fs::resolve_protocol(DISPLAY_OPTIONS_PATH);
    if(!save_display_options(file, saved_options_))
    {
        APPLOG_WARNING("The display options could not be saved in {}.", file.generic_string());
    }
}

} // namespace unravel
