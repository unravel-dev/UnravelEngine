#include "mcp_input_names.h"
#include "mcp_input_utils.h"
#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>
#include <editor/system/mcp_manager.h>
#include <engine/events.h>
#include <engine/input/input.h>
#include <engine/rendering/renderer.h>

#include <ospp/event.h>
#include <ospp/mouse.h>

#include <hpp/string_view.hpp>

#include <array>

#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace unravel::mcp
{

namespace
{
constexpr auto k_frame_poll_interval = std::chrono::milliseconds(2);
/// A frame that never arrives means the editor is not pumping; fail rather than hang.
constexpr auto k_frame_wait_timeout = std::chrono::milliseconds(5000);
/// Frame budget on top of k_frame_wait_timeout for a wait of many frames, at a 10 fps worst case.
constexpr auto k_frame_budget = std::chrono::milliseconds(100);
/// Frames the Game panel gets to take focus before a sequence gives up on it.
constexpr uint64_t k_focus_frames = 4;
/// Longest hold one call can ask for.
constexpr int64_t k_max_hold_frames = 600;
/// The touch device input_send_touch's fingers come from, apart from a real screen's.
constexpr uint64_t k_mcp_touch_device = 0x4D4350;
/// Fingers input_send_touch tells apart.
constexpr int64_t k_max_finger_index = 9;

void push_mouse_button_event(uint32_t window_id, os::mouse::button button, bool down, input::coord position)
{
    os::event event{};
    event.type = os::events::mouse_button;
    event.button.window_id = window_id;
    event.button.button = button;
    event.button.state_id = down ? os::state::pressed : os::state::released;
    event.button.x = position.x;
    event.button.y = position.y;
    event.button.raw_x = position.x;
    event.button.raw_y = position.y;
    os::push_event(event);
}

/// The pointer position is polled, not taken from motion events; this event only tells the UI the
/// pointer moved, so it refreshes hover from the virtual position.
void push_mouse_motion_event(uint32_t window_id, input::coord position)
{
    os::event event{};
    event.type = os::events::mouse_motion;
    event.motion.window_id = window_id;
    event.motion.x = position.x;
    event.motion.y = position.y;
    os::push_event(event);
}

auto virtual_pointer(rtti::context& ctx) -> input::coord
{
    auto* manager = get_input_manager(ctx);
    const auto* position = manager != nullptr ? &manager->get_virtual_mouse_position() : nullptr;
    return position != nullptr && *position ? **position : input::coord{};
}

/// One key of a call, with the modifiers held around it.
struct key_request
{
    os::key::code code{};
    bool alt{false};
    bool ctrl{false};
    bool shift{false};
};

/// Modifier keys a key sequence holds down, left and right apart: letting go of one leaves the other held.
struct held_modifiers
{
    bool ctrl{};
    bool shift{};
    bool alt{};

    /// Records `code` going down or up when it is a modifier.
    void track(os::key::code code, bool down)
    {
        constexpr std::array<std::pair<os::key::code, size_t>, 6> sides{{{os::key::code::lctrl, 0},
                                                                         {os::key::code::rctrl, 1},
                                                                         {os::key::code::lshift, 2},
                                                                         {os::key::code::rshift, 3},
                                                                         {os::key::code::lalt, 4},
                                                                         {os::key::code::ralt, 5}}};
        for(const auto& [side_code, index] : sides)
        {
            if(code == side_code)
            {
                is_down[index] = down;
            }
        }
        ctrl = is_down[0] || is_down[1];
        shift = is_down[2] || is_down[3];
        alt = is_down[4] || is_down[5];
    }

private:
    std::array<bool, 6> is_down{};
};

/// One key event, flagged with the modifiers held: the UI reads Ctrl / Shift / Alt from the flags
/// of every key event, as a real keyboard reports them.
void push_key_event(uint32_t window_id, os::key::code code, bool down, const held_modifiers& held)
{
    os::event event{};
    event.type = down ? os::events::key_down : os::events::key_up;
    event.key.window_id = window_id;
    event.key.code = code;
    event.key.ctrl = held.ctrl;
    event.key.shift = held.shift;
    event.key.alt = held.alt;
    os::push_event(event);
}

/// The key codes a call presses, modifiers first: a modifier goes down as a key of its own, so the
/// action map and the UI see it held just as a real Ctrl+S would hold it.
auto key_sequence(const std::vector<key_request>& keys) -> std::vector<os::key::code>
{
    std::vector<os::key::code> codes;
    const auto add = [&codes](os::key::code code)
    {
        if(std::find(codes.begin(), codes.end(), code) == codes.end())
        {
            codes.push_back(code);
        }
    };
    for(const auto& key : keys)
    {
        if(key.ctrl)
        {
            add(os::key::code::lctrl);
        }
        if(key.shift)
        {
            add(os::key::code::lshift);
        }
        if(key.alt)
        {
            add(os::key::code::lalt);
        }
    }
    for(const auto& key : keys)
    {
        add(key.code);
    }
    return codes;
}

/// Presses (down) or lets go of (up) a sequence: presses in order, releases in reverse. The
/// modifiers of the sequence come first, so they are held around the other keys.
void push_key_sequence(uint32_t window_id, const std::vector<os::key::code>& codes, bool down)
{
    held_modifiers held;
    if(down)
    {
        for(const auto code : codes)
        {
            held.track(code, true);
            push_key_event(window_id, code, true, held);
        }
        return;
    }
    for(const auto code : codes)
    {
        held.track(code, true);
    }
    for(auto it = codes.rbegin(); it != codes.rend(); ++it)
    {
        held.track(*it, false);
        push_key_event(window_id, *it, false, held);
    }
}

/// Moves one "ctrl+" / "shift+" / "alt+" prefix of @p name into @p request, so "ctrl+s" reads as
/// {key: "s", ctrl: true}; false when @p name has none.
auto take_modifier_prefix(std::string& name, key_request& request) -> bool
{
    constexpr std::pair<hpp::string_view, bool key_request::*> modifiers[] = {{"ctrl+", &key_request::ctrl},
                                                                              {"control+", &key_request::ctrl},
                                                                              {"shift+", &key_request::shift},
                                                                              {"alt+", &key_request::alt}};
    for(const auto& [prefix, flag] : modifiers)
    {
        if(name.size() > prefix.size() && to_lower_ascii(name.substr(0, prefix.size())) == prefix)
        {
            request.*flag = true;
            name.erase(0, prefix.size());
            return true;
        }
    }
    return false;
}

auto read_key_request(const simdjson::dom::element& item, key_request& request, std::string& error) -> bool
{
    hpp::string_view name;
    simdjson::dom::object object;
    if(item.get_string().get(name) != simdjson::SUCCESS)
    {
        if(item.get_object().get(object) != simdjson::SUCCESS || object["key"].get_string().get(name) != simdjson::SUCCESS)
        {
            error = "Each key is a name or an object with a key field";
            return false;
        }
        read_bool(object, "alt", request.alt);
        read_bool(object, "ctrl", request.ctrl);
        read_bool(object, "shift", request.shift);
    }
    std::string key_name(name);
    // "ctrl + s" reads as "ctrl+s": spaces around a plus join a modifier to its key.
    for(auto plus = key_name.find('+'); plus != std::string::npos; plus = key_name.find('+', plus + 1))
    {
        while(plus > 0 && key_name[plus - 1] == ' ')
        {
            key_name.erase(--plus, 1);
        }
        while(plus + 1 < key_name.size() && key_name[plus + 1] == ' ')
        {
            key_name.erase(plus + 1, 1);
        }
    }
    while(take_modifier_prefix(key_name, request))
    {
    }
    return parse_key_name(key_name, request.code, error);
}

auto read_key_requests(const simdjson::dom::object& args, std::vector<key_request>& out, std::string& error) -> bool
{
    simdjson::dom::array items;
    if(args["keys"].get_array().get(items) != simdjson::SUCCESS)
    {
        error = "Pass keys as an array of key names or objects";
        return false;
    }
    for(auto item : items)
    {
        key_request request;
        if(!read_key_request(item, request, error))
        {
            return false;
        }
        out.push_back(request);
    }
    if(out.empty())
    {
        error = "keys is empty";
        return false;
    }
    return true;
}

auto read_focus_choice(const simdjson::dom::object& args) -> bool
{
    bool focus_game_panel = true;
    read_bool(args, "focus_game_panel", focus_game_panel);
    return focus_game_panel;
}

auto read_hold_frames(const simdjson::dom::object& args) -> uint64_t
{
    return static_cast<uint64_t>(std::clamp<int64_t>(read_int64_or(args, "frames", 1), 1, k_max_hold_frames));
}

auto input_state_json(rtti::context& ctx) -> std::string
{
    auto* manager = get_input_manager(ctx);
    if(manager == nullptr)
    {
        return R"({"available":false})";
    }
    const auto zone = manager->get_work_zone();
    const auto& virtual_pos = manager->get_virtual_mouse_position();
    const auto play = editor_actions::get_play_state(ctx);
    // The real cursor, in the same screen space as the work zone: the editor UI follows it, so a
    // synthesized press while it rests outside the Game panel is also a press there for the editor.
    const auto real_cursor = os::mouse::get_position();
    const bool real_cursor_in_view = real_cursor.x >= zone.x && real_cursor.x < zone.x + zone.w &&
                                     real_cursor.y >= zone.y && real_cursor.y < zone.y + zone.h;
    const auto game_pointer = manager->get_mouse().get_position();
    constexpr std::array<const char*, 3> lock_names{"none", "locked", "confined"};
    const auto& cursor = manager->get_cursor();
    const auto lock_mode = size_t(cursor.get_lock_mode());
    return fmt::format(
        R"({{"available":true,"input_allowed":{},"play_active":{},"input_frame":{},)"
        R"("work_zone":{{"x":{},"y":{},"w":{},"h":{}}},"virtual_mouse":{},)"
        R"("game_pointer":[{},{}],"real_cursor":[{},{}],"real_cursor_in_game_view":{},)"
        R"("cursor":{{"lock_state":"{}","visible":{},"lock_active":{},"hidden":{}}}}})",
        bool_to_json(manager->is_input_allowed()),
        bool_to_json(play.is_active),
        manager->get_input_frame(),
        zone.x,
        zone.y,
        zone.w,
        zone.h,
        virtual_pos ? fmt::format("[{},{}]", virtual_pos->x, virtual_pos->y) : "null",
        game_pointer.x,
        game_pointer.y,
        real_cursor.x,
        real_cursor.y,
        bool_to_json(real_cursor_in_view),
        lock_mode < lock_names.size() ? lock_names[lock_mode] : "none",
        bool_to_json(cursor.is_visible()),
        bool_to_json(cursor.is_lock_active()),
        bool_to_json(cursor.is_hidden()));
}

/// Keys and buttons MCP holds down (state down) until a later up. The end of play and clear release them: the game's
/// input forgets them on its own, but the editor UI keeps a synthesized press held until it sees the release.
struct held_input
{
    std::mutex mutex;
    uint32_t window_id{};
    std::vector<os::key::code> keys;
    std::vector<os::mouse::button> buttons;
    bool watches_play_end{};
};

auto get_held_input() -> held_input&
{
    static held_input held;
    return held;
}

/// Lets go of every held key and button. Main thread.
void release_held_input()
{
    auto& held = get_held_input();
    std::lock_guard<std::mutex> lock(held.mutex);
    push_key_sequence(held.window_id, held.keys, false);
    for(const auto button : held.buttons)
    {
        push_mouse_button_event(held.window_id, button, false, {});
    }
    held.keys.clear();
    held.buttons.clear();
}

/// Records keys and buttons going down (state down) or up, so release_held_input knows them. Main thread.
void track_held_input(rtti::context& ctx,
                      uint32_t window_id,
                      const std::vector<os::key::code>& keys,
                      const std::vector<os::mouse::button>& buttons,
                      bool down)
{
    auto& held = get_held_input();
    std::lock_guard<std::mutex> lock(held.mutex);
    held.window_id = window_id;
    const auto update = [down](auto& list, const auto& items)
    {
        for(const auto& item : items)
        {
            list.erase(std::remove(list.begin(), list.end(), item), list.end());
            if(down)
            {
                list.push_back(item);
            }
        }
    };
    update(held.keys, keys);
    update(held.buttons, buttons);
    if(down && !held.watches_play_end)
    {
        held.watches_play_end = true;
        ctx.get_cached<events>().on_play_end.connect(
            [](rtti::context&)
            {
                release_held_input();
            });
    }
}

/// Moves the virtual pointer for input_send_mouse: game pixels by default, or screen pixels.
auto move_pointer(rtti::context& ctx, uint32_t window_id, const simdjson::dom::object& args, std::string& error) -> bool
{
    double x = 0.0;
    double y = 0.0;
    if(args["x"].get(x) != simdjson::SUCCESS || args["y"].get(y) != simdjson::SUCCESS)
    {
        error = "Pass both x and y";
        return false;
    }
    std::string space = "game";
    read_string(args, "space", space);
    input::coord position{static_cast<int>(x), static_cast<int>(y)};
    if(space == "game")
    {
        return move_game_pointer(ctx, window_id, position, error);
    }
    auto* manager = get_input_manager(ctx);
    if(space != "screen" || manager == nullptr || !manager->is_inside_work_zone(manager->remap_to_work_zone(position)))
    {
        error = space != "screen" ? "space must be game or screen" : "The point is outside the Game panel";
        return false;
    }
    manager->set_virtual_mouse_position(position);
    push_mouse_motion_event(window_id, position);
    return true;
}

} // namespace

auto get_main_window_id(rtti::context& ctx, uint32_t& out, std::string& error) -> bool
{
    if(!ctx.has<renderer>())
    {
        error = "renderer unavailable";
        return false;
    }
    const auto& window = ctx.get_cached<renderer>().get_main_window();
    if(!window)
    {
        error = "No main window";
        return false;
    }
    out = window->get_window().get_id();
    return true;
}

void push_text_input_event(uint32_t window_id, const std::string& text)
{
    os::event event{};
    event.type = os::events::text_input;
    event.text.window_id = window_id;
    event.text.text = text;
    os::push_event(event);
}

auto get_input_manager(rtti::context& ctx) -> input::os_input_manager*
{
    // The editor adds its input system before the MCP server starts. get_cached keeps the reference after the first
    // call, so worker threads do not read the context's map while the main thread changes it.
    return &ctx.get_cached<input_system>().manager;
}

auto wait_input_frames(rtti::context& ctx, uint64_t frames) -> bool
{
    auto* manager = get_input_manager(ctx);
    if(manager == nullptr)
    {
        return false;
    }
    const auto target = manager->get_input_frame() + frames;
    const auto deadline = std::chrono::steady_clock::now() + k_frame_wait_timeout + k_frame_budget * frames;
    const auto& mcp = ctx.get_cached<mcp_manager>();
    while(manager->get_input_frame() < target)
    {
        if(std::chrono::steady_clock::now() > deadline || mcp.is_stopping())
        {
            return false;
        }
        std::this_thread::sleep_for(k_frame_poll_interval);
    }
    return true;
}

auto prepare_game_input(mcp_manager& mcp,
                        rtti::context& ctx,
                        bool focus_game_panel,
                        uint32_t& window_id,
                        std::string& error) -> bool
{
    std::string failure;
    auto focused = mcp.invoke_on_main(
        [&]() -> bool
        {
            auto* manager = get_input_manager(ctx);
            if(manager == nullptr)
            {
                error = "input_system unavailable";
                return false;
            }
            if(!get_main_window_id(ctx, window_id, error) ||
               (focus_game_panel && !editor_actions::focus_game_panel(ctx, &error)))
            {
                return false;
            }
            return true;
        },
        std::chrono::milliseconds(5000),
        &failure);
    if(!focused || !*focused)
    {
        error = error.empty() ? failure : error;
        return false;
    }
    // Read on the main thread, which sets it each frame.
    const auto is_input_allowed = [&mcp, &ctx]()
    {
        const auto allowed = mcp.invoke_on_main(
            [&ctx]()
            {
                return get_input_manager(ctx)->is_input_allowed();
            });
        return allowed && *allowed;
    };
    for(uint64_t frame = 0; focus_game_panel && !is_input_allowed() && frame < k_focus_frames; ++frame)
    {
        if(!wait_input_frames(ctx, 1))
        {
            error = "The editor is not advancing frames";
            return false;
        }
    }
    if(focus_game_panel && !is_input_allowed())
    {
        error = "The Game panel did not take input focus (is play mode running and the panel visible?)";
        return false;
    }
    return true;
}

auto push_on_main(mcp_manager& mcp, const std::function<void()>& push) -> bool
{
    const auto pushed = mcp.invoke_on_main(
        [&push]() -> bool
        {
            push();
            return true;
        });
    return pushed.has_value();
}

auto release_on_main(mcp_manager& mcp, std::function<void()> push) -> bool
{
    return mcp.post_on_main(std::move(push));
}

auto lock_input_sequence() -> std::unique_lock<std::mutex>
{
    static std::mutex sequence_mutex;
    return std::unique_lock<std::mutex>(sequence_mutex);
}

auto move_game_pointer(rtti::context& ctx, uint32_t window_id, input::coord game_position, std::string& error)
    -> bool
{
    auto* manager = get_input_manager(ctx);
    if(manager == nullptr)
    {
        error = "input_system unavailable";
        return false;
    }
    // The work zone test takes game pixels, the space the game's pointer reads.
    if(!manager->is_inside_work_zone(game_position))
    {
        error = fmt::format("Game point [{}, {}] is outside the Game panel's view; a press there would land on the "
                            "editor",
                            game_position.x,
                            game_position.y);
        return false;
    }
    const auto screen_position = manager->remap_from_work_zone(game_position);
    manager->set_virtual_mouse_position(screen_position);
    push_mouse_motion_event(window_id, screen_position);
    return true;
}

auto click_mouse_button(mcp_manager& mcp,
                        rtti::context& ctx,
                        uint32_t window_id,
                        os::mouse::button button,
                        uint64_t frames,
                        std::string& error) -> bool
{
    const bool pressed = push_on_main(mcp,
                                      [&]
                                      {
                                          push_mouse_button_event(window_id, button, true, virtual_pointer(ctx));
                                      });
    if(!pressed)
    {
        error = "Timed out reaching the editor main thread; nothing was pressed";
        return false;
    }
    const bool held = wait_input_frames(ctx, frames);
    // Released whatever happened above, and never dropped, so a stalled editor does not keep the button held.
    const bool released = release_on_main(mcp,
                                          [context = &ctx, window_id, button]()
                                          {
                                              push_mouse_button_event(window_id, button, false, virtual_pointer(*context));
                                          });
    if(!released)
    {
        error = "The editor's main thread is busy: the release is queued and lets go of the button once it is free";
        return false;
    }
    if(!held || !wait_input_frames(ctx, 1))
    {
        error = "The editor stopped advancing frames during the click; the button was released";
        return false;
    }
    return true;
}

namespace
{
/// Frames the UI gets to see the pointer hover before a press (hover-gated widgets, tooltips).
constexpr uint64_t k_ui_hover_frames = 2;
/// Pointer steps of a UI drag; every step is one frame.
constexpr int k_ui_drag_steps = 8;

/// Screen position of a point of the main window's client area; refuses points outside it.
auto client_to_screen(rtti::context& ctx, input::coord client, input::coord& screen, std::string& error) -> bool
{
    const auto& window = ctx.get_cached<renderer>().get_main_window();
    const auto origin = window->get_window().get_position();
    const auto size = window->get_window().get_size();
    if(client.x < 0 || client.y < 0 || client.x >= int(size.w) || client.y >= int(size.h))
    {
        error = fmt::format("[{}, {}] is outside the editor window's client area ({}x{})",
                            client.x,
                            client.y,
                            size.w,
                            size.h);
        return false;
    }
    screen = {origin.x + client.x, origin.y + client.y};
    return true;
}

/// Puts the virtual pointer at a screen position for the editor UI. Main thread.
void set_ui_pointer(rtti::context& ctx, uint32_t window_id, input::coord screen)
{
    get_input_manager(ctx)->set_virtual_mouse_position(screen);
    push_mouse_motion_event(window_id, screen);
}

/// Hover, then scroll the panel under the virtual pointer. Worker thread only.
auto scroll_ui(mcp_manager& mcp, rtti::context& ctx, uint32_t window_id, float wheel, std::string& error) -> bool
{
    const bool scrolled = wait_input_frames(ctx, k_ui_hover_frames) &&
                          push_on_main(mcp,
                                       [&]
                                       {
                                           os::event event{};
                                           event.type = os::events::mouse_wheel;
                                           event.wheel.window_id = window_id;
                                           event.wheel.y = wheel;
                                           os::push_event(event);
                                       }) &&
                          wait_input_frames(ctx, 2);
    if(!scrolled)
    {
        error = "The editor is not advancing frames";
    }
    return scrolled;
}

/// Hover, then a click, a double click or a drag at the virtual pointer. Worker thread only.
auto run_ui_gesture(mcp_manager& mcp,
                    rtti::context& ctx,
                    uint32_t window_id,
                    os::mouse::button button,
                    bool is_double,
                    bool is_drag,
                    input::coord start,
                    input::coord end,
                    std::string& error) -> bool
{
    if(!wait_input_frames(ctx, k_ui_hover_frames))
    {
        error = "The editor is not advancing frames";
        return false;
    }
    if(!is_drag)
    {
        return click_mouse_button(mcp, ctx, window_id, button, 1, error) &&
               (!is_double || click_mouse_button(mcp, ctx, window_id, button, 1, error));
    }
    bool moved = push_on_main(mcp,
                              [&]
                              {
                                  push_mouse_button_event(window_id, button, true, virtual_pointer(ctx));
                              }) &&
                 wait_input_frames(ctx, 1);
    for(int step = 1; moved && step <= k_ui_drag_steps; ++step)
    {
        const float t = float(step) / float(k_ui_drag_steps);
        const input::coord point{start.x + int(std::lround(float(end.x - start.x) * t)),
                                 start.y + int(std::lround(float(end.y - start.y) * t))};
        moved = push_on_main(mcp,
                             [&]
                             {
                                 set_ui_pointer(ctx, window_id, point);
                             }) &&
                wait_input_frames(ctx, 1);
    }
    // Released whatever happened above, and never dropped, so no button stays held.
    const bool released = release_on_main(mcp,
                                          [context = &ctx, window_id, button]()
                                          {
                                              push_mouse_button_event(window_id, button, false, virtual_pointer(*context));
                                          });
    if(!released)
    {
        error = "The editor's main thread is busy: the release is queued and lets go of the button once it is free";
        return false;
    }
    if(!moved || !wait_input_frames(ctx, 1))
    {
        error = "The editor stopped advancing frames during the drag; the button was released";
        return false;
    }
    return true;
}
/// The buttons and axes one input_send_gamepad call names.
struct gamepad_request
{
    std::vector<input::gamepad_button> buttons;
    std::vector<std::pair<input::gamepad_axis, float>> axes;
};

/// Reads buttons and axes by the names the action maps show; false with @p error on an unknown name or none.
auto read_gamepad_request(const simdjson::dom::object& args, gamepad_request& out, std::string& error) -> bool
{
    simdjson::dom::array buttons;
    if(args["buttons"].get(buttons) == simdjson::SUCCESS)
    {
        for(auto element : buttons)
        {
            hpp::string_view name;
            const auto found = element.get(name) == simdjson::SUCCESS
                                   ? gamepad_button_table().find(normalize_input_name(name))
                                   : gamepad_button_table().end();
            if(found == gamepad_button_table().end())
            {
                error = fmt::format("Unknown gamepad button {} (South, East, West, North, DPad Up, Left Shoulder, "
                                    "Start, ... as input_list_actions shows them)",
                                    simdjson::minify(element));
                return false;
            }
            out.buttons.push_back(found->second);
        }
    }
    simdjson::dom::array axes;
    if(args["axes"].get(axes) == simdjson::SUCCESS)
    {
        for(auto element : axes)
        {
            simdjson::dom::object axis;
            hpp::string_view name;
            if(element.get(axis) != simdjson::SUCCESS || axis["axis"].get(name) != simdjson::SUCCESS)
            {
                error = "Each axis is {axis, value}";
                return false;
            }
            const auto found = gamepad_axis_table().find(normalize_input_name(name));
            if(found == gamepad_axis_table().end())
            {
                error = "Unknown gamepad axis " + std::string(name) +
                        " (Left X, Left Y, Right X, Right Y, Left Trigger, Right Trigger)";
                return false;
            }
            // A missing value centers the axis: up needs none.
            double value = 0.0;
            (void)axis["value"].get(value);
            out.axes.emplace_back(found->second, static_cast<float>(std::clamp(value, -1.0, 1.0)));
        }
    }
    if(out.buttons.empty() && out.axes.empty())
    {
        error = "Pass buttons, axes or release_all";
        return false;
    }
    return true;
}

/// Holds the request's buttons and axes (down) or lets them go and centers the axes. Main thread.
void apply_gamepad_request(rtti::context& ctx, const gamepad_request& request, bool down)
{
    auto* manager = get_input_manager(ctx);
    if(manager == nullptr)
    {
        return;
    }
    auto& gamepad = manager->get_virtual_gamepad();
    for(const auto button : request.buttons)
    {
        gamepad.set_button(button, down);
    }
    for(const auto& [axis, value] : request.axes)
    {
        gamepad.set_axis(axis, down ? value : 0.0f);
    }
}

/// Queues a finger event at @p game_position (pixels of the game's render target); false with @p error when the
/// point lies outside the Game panel. Main thread.
auto push_finger_event(rtti::context& ctx,
                       uint32_t window_id,
                       os::events type,
                       uint64_t finger,
                       input::coord game_position,
                       std::string& error) -> bool
{
    auto* manager = get_input_manager(ctx);
    if(manager == nullptr)
    {
        error = "input_system unavailable";
        return false;
    }
    if(type != os::events::finger_up && !manager->is_inside_work_zone(game_position))
    {
        error = fmt::format("Game point [{}, {}] is outside the Game panel's view", game_position.x, game_position.y);
        return false;
    }
    const auto normalized = manager->unmap_finger_position(game_position);
    os::event event{};
    event.type = type;
    event.finger.touch_id = k_mcp_touch_device;
    event.finger.finger_id = finger;
    event.finger.window_id = window_id;
    event.finger.x = normalized[0];
    event.finger.y = normalized[1];
    event.finger.pressure = type == os::events::finger_up ? 0.0f : 1.0f;
    os::push_event(event);
    return true;
}

} // namespace

void register_input_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "input_get_state",
         .description =
             "Whether synthesized input will reach the game: input_allowed is false while the Game panel is not "
             "focused, and nothing reaches the action map then. Also reports the work zone (the Game panel's "
             "rect), the virtual mouse position if set, the input frame counter, and the cursor a script holds "
             "(Cursor.lockState / visible, whether the lock applies now - play running, Game panel focused, not "
             "freed by Escape - and whether the cursor is hidden; a virtual mouse turns the lock off). Check this "
             "first when a key or click seems to do nothing.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             return {.text = input_state_json(ctx), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "input_send_keys",
         .description =
             "Synthesize keyboard input for the game; scripts read it in play mode. state press (default) holds "
             "the keys for `frames` input frames, then releases them (1 is a tap, what Input.IsPressed sees; 60 "
             "walks a character for 60 frames); down holds them until a later up. Key names are the platform's, "
             "ignoring case, spaces and dashes (\"w\", \"space\", \"left shift\", \"f1\"); \"ctrl+s\" or {key, "
             "ctrl, shift, alt} adds modifiers. text types characters into the focused text field. Focuses the "
             "Game panel first (unless focus_game_panel is false) and waits until it accepts input; with "
             "focus_game_panel false the keys reach whatever has focus, the editor's shortcuts included. Keys held "
             "with state down are released when play ends or by input_send_mouse clear. Input tools run one call at "
             "a time.",
         .input_schema_json =
             R"json({"type":"object","properties":{"keys":{"type":"array","items":{"anyOf":[{"type":"string"},{"type":"object","properties":{"key":{"type":"string"},"alt":{"type":"boolean"},"ctrl":{"type":"boolean"},"shift":{"type":"boolean"}},"required":["key"]}]},"description":"Key names, or objects with modifiers"},"text":{"type":"string","description":"Characters to type (UTF-8), sent after the keys go down"},"state":{"type":"string","enum":["press","down","up"],"description":"Default press"},"frames":{"type":"integer","minimum":1,"maximum":600,"description":"state press: frames to hold; default 1"},"focus_game_panel":{"type":"boolean","description":"Default true"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto sequence = lock_input_sequence();
             std::string text;
             read_string(args, "text", text);
             std::vector<key_request> keys;
             std::string error;
             const bool has_keys = args["keys"].error() == simdjson::SUCCESS;
             if((has_keys || text.empty()) && !read_key_requests(args, keys, error))
             {
                 return {.text = text.empty() ? error + " (or pass text)" : error, .is_error = true};
             }
             std::string state = "press";
             read_string(args, "state", state);
             if(state != "down" && state != "up" && state != "press")
             {
                 return {.text = "state must be press, down or up", .is_error = true};
             }
             if(state == "up" && !text.empty())
             {
                 return {.text = "text is typed as keys go down; state up only lets go of keys", .is_error = true};
             }
             uint32_t window_id = 0;
             if(!prepare_game_input(mcp, ctx, read_focus_choice(args), window_id, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto codes = key_sequence(keys);
             const bool goes_down = state != "up";
             const bool pushed = push_on_main(mcp,
                                              [&]
                                              {
                                                  push_key_sequence(window_id, codes, goes_down);
                                                  if(goes_down && !text.empty())
                                                  {
                                                      push_text_input_event(window_id, text);
                                                  }
                                                  if(state != "press")
                                                  {
                                                      track_held_input(ctx, window_id, codes, {}, goes_down);
                                                  }
                                              });
             if(!pushed)
             {
                 return {.text = "Timed out reaching the editor main thread; nothing was sent", .is_error = true};
             }
             if(state != "press")
             {
                 return {.text = fmt::format(R"({{"ok":true,"state":{},"keys":{}}})", make_json_string(state), codes.size())};
             }
             // A tap has to span a frame boundary, or the key is released before anything reads it.
             const auto frames = read_hold_frames(args);
             const bool held = wait_input_frames(ctx, frames);
             const bool released = release_on_main(mcp,
                                                   [window_id, codes]()
                                                   {
                                                       push_key_sequence(window_id, codes, false);
                                                   });
             if(!released)
             {
                 return {.text = "The editor's main thread is busy: the release is queued and lets go of the keys once "
                                 "it is free",
                         .is_error = true};
             }
             if(!held)
             {
                 return {.text = "The editor stopped advancing frames; the keys were released", .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"state":"press","keys":{},"frames":{}}})", codes.size(), frames)};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "input_send_mouse",
         .description =
             "Synthesize mouse input for the game through a virtual pointer that replaces the real cursor until "
             "clear:true (the user's mouse does not reach the editor meanwhile). x, y move it - space game "
             "(default: pixels of the game's render target, what Input.mousePosition reads) or screen (desktop "
             "pixels) - and must lie inside the Game panel. button (x1 / x2 are the side buttons action maps call "
             "Button 4 / Button 5) with state click (default) presses for "
             "`frames` frames, then releases; down / up hold and release. wheel scrolls (positive away from the "
             "viewer). Move, button and wheel combine, in that order. Play mode only; the pointer clears itself "
             "and held keys and buttons are released when play ends. Input tools run one call at a time.",
         .input_schema_json =
             R"json({"type":"object","properties":{"x":{"type":"number","minimum":-100000,"maximum":100000},"y":{"type":"number","minimum":-100000,"maximum":100000},"space":{"type":"string","enum":["game","screen"],"description":"Default game"},"button":{"type":"string","enum":["left","right","middle","x1","x2"]},"state":{"type":"string","enum":["click","down","up"],"description":"Default click"},"frames":{"type":"integer","minimum":1,"maximum":600,"description":"state click: frames to hold; default 1"},"wheel":{"type":"number"},"clear":{"type":"boolean","description":"Hand the pointer back to the mouse and release every key and button held with state down"},"focus_game_panel":{"type":"boolean","description":"Default true"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto sequence = lock_input_sequence();
             bool clear = false;
             read_bool(args, "clear", clear);
             if(clear)
             {
                 // Never dropped: the user's mouse comes back and nothing MCP held stays down.
                 const bool cleared = release_on_main(mcp,
                                                      [context = &ctx]()
                                                      {
                                                          release_held_input();
                                                          if(auto* manager = get_input_manager(*context))
                                                          {
                                                              manager->set_virtual_mouse_position({});
                                                          }
                                                      });
                 return {.text = cleared ? R"({"ok":true,"cleared":true})"
                                         : R"({"ok":true,"cleared":false,"queued":true})"};
             }
             std::string button_name;
             const bool has_button = read_string(args, "button", button_name);
             os::mouse::button button{};
             if(has_button && !parse_mouse_button(button_name, button))
             {
                 return {.text = "button must be left, right, middle, x1 or x2", .is_error = true};
             }
             std::string state = "click";
             read_string(args, "state", state);
             if(state != "down" && state != "up" && state != "click")
             {
                 return {.text = "state must be click, down or up", .is_error = true};
             }
             double wheel = 0.0;
             const bool has_wheel = args["wheel"].get(wheel) == simdjson::SUCCESS;
             const bool has_move = args["x"].error() == simdjson::SUCCESS || args["y"].error() == simdjson::SUCCESS;
             if(!has_move && !has_button && !has_wheel)
             {
                 return {.text = "Pass x/y, button, wheel or clear", .is_error = true};
             }
             std::string error;
             uint32_t window_id = 0;
             const auto playing = mcp.invoke_on_main(
                 [&]() -> bool
                 {
                     return editor_actions::get_play_state(ctx).is_active;
                 });
             if(!playing || !*playing)
             {
                 return {.text = "Play mode only: the virtual pointer exists only while the game plays",
                         .is_error = true};
             }
             if(!prepare_game_input(mcp, ctx, read_focus_choice(args), window_id, error))
             {
                 return {.text = error, .is_error = true};
             }
             // Move, then make sure a pointer exists before any button lands at it.
             auto placed = mcp.invoke_on_main(
                 [&]() -> bool
                 {
                     if(has_move && !move_pointer(ctx, window_id, args, error))
                     {
                         return false;
                     }
                     auto* manager = get_input_manager(ctx);
                     const auto& pointer = manager->get_virtual_mouse_position();
                     if((has_button || has_wheel) && !pointer)
                     {
                         error = "No virtual pointer: pass x and y first";
                         return false;
                     }
                     // The pointer is kept in screen pixels: a Game panel moved or resized since may have left it on
                     // the editor, where a press would land.
                     if(!has_move && (has_button || has_wheel) &&
                        !manager->is_inside_work_zone(manager->remap_to_work_zone(*pointer)))
                     {
                         error = "The pointer is outside the Game panel now (the panel moved or was resized); pass x "
                                 "and y again";
                         return false;
                     }
                     return true;
                 });
             if(!placed || !*placed)
             {
                 return {.text = error.empty() ? "Timed out reaching the editor main thread" : error, .is_error = true};
             }
             if(has_move && !wait_input_frames(ctx, 1))
             {
                 return {.text = "The editor stopped advancing frames", .is_error = true};
             }
             if(has_button && state == "click" && !click_mouse_button(mcp, ctx, window_id, button, read_hold_frames(args), error))
             {
                 return {.text = error, .is_error = true};
             }
             const bool pushed = push_on_main(mcp,
                                              [&]
                                              {
                                                  if(has_button && state != "click")
                                                  {
                                                      push_mouse_button_event(window_id, button, state == "down", virtual_pointer(ctx));
                                                      track_held_input(ctx, window_id, {}, {button}, state == "down");
                                                  }
                                                  if(has_wheel)
                                                  {
                                                      os::event event{};
                                                      event.type = os::events::mouse_wheel;
                                                      event.wheel.window_id = window_id;
                                                      event.wheel.y = wheel;
                                                      os::push_event(event);
                                                  }
                                              });
             if(!pushed)
             {
                 return {.text = "Timed out reaching the editor main thread", .is_error = true};
             }
             const auto state_json = mcp.invoke_on_main(
                 [&]() -> std::string
                 {
                     return input_state_json(ctx);
                 });
             return {.text = fmt::format(R"({{"ok":true,"state":{}}})", state_json ? *state_json : "null")};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "input_send_gamepad",
         .description =
             "Drive the game's virtual gamepad in play mode - the pad on-screen controls drive, which actions mapped "
             "to gamepad buttons and axes answer to as they do to a connected pad. buttons by the names "
             "input_list_actions shows (\"South\", \"DPad Up\", \"Left Shoulder\"); axes as {axis, value}: "
             "\"Left X\" from -1 (left) to 1 (right), \"Left Y\" from -1 (up) to 1 (down), triggers from 0 to 1. "
             "state press (default) holds them for `frames` frames, then lets go and centers them; down holds them "
             "until a later up (axes an up names are centered) or play ends. release_all lets go of everything. "
             "Input tools run one call at a time.",
         .input_schema_json =
             R"json({"type":"object","properties":{"buttons":{"type":"array","items":{"type":"string"}},"axes":{"type":"array","items":{"type":"object","properties":{"axis":{"type":"string"},"value":{"type":"number","minimum":-1,"maximum":1}},"required":["axis"]}},"state":{"type":"string","enum":["press","down","up"],"description":"Default press"},"frames":{"type":"integer","minimum":1,"maximum":600,"description":"state press: frames to hold; default 1"},"release_all":{"type":"boolean","description":"Let go of every button and center every axis"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto sequence = lock_input_sequence();
             bool release_all = false;
             read_bool(args, "release_all", release_all);
             gamepad_request request;
             std::string error;
             if(!release_all && !read_gamepad_request(args, request, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string state = "press";
             read_string(args, "state", state);
             if(state != "down" && state != "up" && state != "press")
             {
                 return {.text = "state must be press, down or up", .is_error = true};
             }
             const auto playing = mcp.invoke_on_main(
                 [&]() -> bool
                 {
                     return editor_actions::get_play_state(ctx).is_active;
                 });
             if(!playing || !*playing)
             {
                 return {.text = "Play mode only: the running game reads the virtual gamepad (play_set_active)",
                         .is_error = true};
             }
             if(release_all)
             {
                 const bool released = release_on_main(mcp,
                                                       [context = &ctx]()
                                                       {
                                                           if(auto* manager = get_input_manager(*context))
                                                           {
                                                               manager->get_virtual_gamepad().release_all();
                                                           }
                                                       });
                 return {.text = released ? R"({"ok":true,"released":true})"
                                          : R"({"ok":true,"released":false,"queued":true})"};
             }
             const bool goes_down = state != "up";
             const bool pushed = push_on_main(mcp,
                                              [&]
                                              {
                                                  apply_gamepad_request(ctx, request, goes_down);
                                              });
             if(!pushed)
             {
                 return {.text = "Timed out reaching the editor main thread; nothing was sent", .is_error = true};
             }
             if(state != "press")
             {
                 return {.text = fmt::format(R"({{"ok":true,"state":{},"buttons":{},"axes":{}}})",
                                             make_json_string(state),
                                             request.buttons.size(),
                                             request.axes.size())};
             }
             const auto frames = read_hold_frames(args);
             const bool held = wait_input_frames(ctx, frames);
             const bool released = release_on_main(mcp,
                                                   [context = &ctx, request]()
                                                   {
                                                       apply_gamepad_request(*context, request, false);
                                                   });
             if(!released)
             {
                 return {.text = "The editor's main thread is busy: the release is queued and lets go once it is free",
                         .is_error = true};
             }
             if(!held)
             {
                 return {.text = "The editor stopped advancing frames; the gamepad was let go", .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"state":"press","buttons":{},"axes":{},"frames":{}}})",
                                         request.buttons.size(),
                                         request.axes.size(),
                                         frames)};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "input_send_touch",
         .description =
             "Synthesize touch for the game in play mode: a finger at x, y, in pixels of the game's render target "
             "(what Input.mousePosition reads), inside the Game panel. state tap (default) puts it down, holds it "
             "for `frames` frames - moving it to to_x, to_y over them when given, a swipe or the drag of an "
             "on-screen stick - and lifts it; down puts it down and leaves it, move moves a finger that is down, up "
             "lifts it. finger (0-9, default 0) tells fingers apart for multi-touch. The game reads them as a touch "
             "screen's: touches, on-screen controls, and the pointer when touches drive the mouse. Fingers left down "
             "are lifted when play ends. Focuses the Game panel first (unless focus_game_panel is false). Input "
             "tools run one call at a time.",
         .input_schema_json =
             R"json({"type":"object","properties":{"x":{"type":"number","minimum":-100000,"maximum":100000},"y":{"type":"number","minimum":-100000,"maximum":100000},"to_x":{"type":"number","minimum":-100000,"maximum":100000},"to_y":{"type":"number","minimum":-100000,"maximum":100000},"state":{"type":"string","enum":["tap","down","move","up"],"description":"Default tap"},"frames":{"type":"integer","minimum":1,"maximum":600,"description":"state tap: frames the finger stays down; default 1"},"finger":{"type":"integer","minimum":0,"maximum":9,"description":"Default 0"},"focus_game_panel":{"type":"boolean","description":"Default true"}},"required":["x","y"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto sequence = lock_input_sequence();
             double x = 0.0;
             double y = 0.0;
             if(!read_double(args, "x", x) || !read_double(args, "y", y))
             {
                 return {.text = "Pass x and y", .is_error = true};
             }
             double to_x = 0.0;
             double to_y = 0.0;
             const bool has_to_x = read_double(args, "to_x", to_x);
             const bool has_to_y = read_double(args, "to_y", to_y);
             if(has_to_x != has_to_y)
             {
                 return {.text = "Pass to_x and to_y together", .is_error = true};
             }
             std::string state = "tap";
             read_string(args, "state", state);
             if(state != "tap" && state != "down" && state != "move" && state != "up")
             {
                 return {.text = "state must be tap, down, move or up", .is_error = true};
             }
             if(has_to_x && state != "tap")
             {
                 return {.text = "to_x and to_y are for state tap; move a finger that is down with state move",
                         .is_error = true};
             }
             const auto finger = static_cast<uint64_t>(std::clamp<int64_t>(read_int64_or(args, "finger", 0), 0, k_max_finger_index));
             std::string error;
             const auto playing = mcp.invoke_on_main(
                 [&]() -> bool
                 {
                     return editor_actions::get_play_state(ctx).is_active;
                 });
             if(!playing || !*playing)
             {
                 return {.text = "Play mode only: touches reach the running game (play_set_active)", .is_error = true};
             }
             uint32_t window_id = 0;
             if(!prepare_game_input(mcp, ctx, read_focus_choice(args), window_id, error))
             {
                 return {.text = error, .is_error = true};
             }
             const input::coord start{static_cast<int>(x), static_cast<int>(y)};
             const input::coord end = has_to_x ? input::coord{static_cast<int>(to_x), static_cast<int>(to_y)} : start;
             const auto first_event = state == "tap" || state == "down" ? os::events::finger_down
                                      : state == "move"                 ? os::events::finger_motion
                                                                        : os::events::finger_up;
             bool is_pushed = false;
             const bool reached = push_on_main(mcp,
                                               [&]
                                               {
                                                   is_pushed = push_finger_event(ctx, window_id, first_event, finger, start, error);
                                               });
             if(!reached || !is_pushed)
             {
                 return {.text = reached ? error : "Timed out reaching the editor main thread; nothing was sent",
                         .is_error = true};
             }
             if(state != "tap")
             {
                 return {.text = fmt::format(R"({{"ok":true,"state":{},"finger":{}}})", make_json_string(state), finger)};
             }
             const auto frames = read_hold_frames(args);
             bool held = true;
             for(uint64_t frame = 1; frame <= frames && held; ++frame)
             {
                 held = wait_input_frames(ctx, 1);
                 if(has_to_x && held)
                 {
                     const double share = static_cast<double>(frame) / static_cast<double>(frames);
                     const input::coord point{static_cast<int>(std::lround(x + (to_x - x) * share)),
                                              static_cast<int>(std::lround(y + (to_y - y) * share))};
                     push_on_main(mcp,
                                  [&]
                                  {
                                      std::string ignored;
                                      push_finger_event(ctx, window_id, os::events::finger_motion, finger, point, ignored);
                                  });
                 }
             }
             const bool lifted = release_on_main(mcp,
                                                 [context = &ctx, window_id, finger, end]()
                                                 {
                                                     std::string ignored;
                                                     push_finger_event(*context, window_id, os::events::finger_up, finger, end, ignored);
                                                 });
             if(!lifted)
             {
                 return {.text = "The editor's main thread is busy: the lift is queued and runs once it is free",
                         .is_error = true};
             }
             if(!held)
             {
                 return {.text = "The editor stopped advancing frames; the finger was lifted", .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"state":"tap","finger":{},"frames":{}}})", finger, frames)};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "editor_ui_click",
         .description =
             "Click the editor's own UI - a panel tab, an inspector button, a timeline - at a point of the editor "
             "main window's client area (in the pixels the platform reports the window's size in: physical on "
             "Windows, logical on HiDPI macOS and Wayland), to verify editor "
             "tools. A virtual pointer stands in for the user's mouse during the call; at its end the pointer is as "
             "it was (the user's mouse, or the game pointer input_send_mouse placed). "
             "button (default left); double:true double-clicks; to_x/to_y drag from x,y to there instead of "
             "clicking; wheel scrolls the panel under x,y instead (positive scrolls up). Edit and play mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"x":{"type":"number","minimum":0,"maximum":100000},"y":{"type":"number","minimum":0,"maximum":100000},"button":{"type":"string","enum":["left","right","middle"]},"double":{"type":"boolean"},"to_x":{"type":"number","minimum":0,"maximum":100000},"to_y":{"type":"number","minimum":0,"maximum":100000},"wheel":{"type":"number"}},"required":["x","y"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto sequence = lock_input_sequence();
             double x = 0.0;
             double y = 0.0;
             if(args["x"].get(x) != simdjson::SUCCESS || args["y"].get(y) != simdjson::SUCCESS)
             {
                 return {.text = "Pass x and y", .is_error = true};
             }
             std::string button_name = "left";
             read_string(args, "button", button_name);
             os::mouse::button button{};
             if(!parse_mouse_button(button_name, button))
             {
                 return {.text = "button must be left, right or middle", .is_error = true};
             }
             bool is_double = false;
             read_bool(args, "double", is_double);
             double to_x = 0.0;
             double to_y = 0.0;
             const bool has_to_x = args["to_x"].get(to_x) == simdjson::SUCCESS;
             const bool has_to_y = args["to_y"].get(to_y) == simdjson::SUCCESS;
             const bool is_drag = has_to_x && has_to_y;
             double wheel = 0.0;
             const bool is_wheel = args["wheel"].get(wheel) == simdjson::SUCCESS;
             if(has_to_x != has_to_y)
             {
                 return {.text = "A drag needs both to_x and to_y", .is_error = true};
             }
             if(is_wheel && (is_drag || is_double))
             {
                 return {.text = "wheel scrolls instead of clicking: leave out double, to_x and to_y", .is_error = true};
             }
             std::string error;
             uint32_t window_id = 0;
             input::coord start{};
             input::coord end{};
             // The pointer a game input call left: given back at the end instead of the user's mouse.
             hpp::optional<input::coord> previous_pointer;
             const auto placed = mcp.invoke_on_main(
                 [&]() -> bool
                 {
                     if(!get_main_window_id(ctx, window_id, error) ||
                        !client_to_screen(ctx, {int(x), int(y)}, start, error) ||
                        (is_drag && !client_to_screen(ctx, {int(to_x), int(to_y)}, end, error)))
                     {
                         return false;
                     }
                     previous_pointer = get_input_manager(ctx)->get_virtual_mouse_position();
                     set_ui_pointer(ctx, window_id, start);
                     return true;
                 });
             if(!placed || !*placed)
             {
                 return {.text = error.empty() ? "Timed out reaching the editor main thread" : error, .is_error = true};
             }
             const bool done = is_wheel ? scroll_ui(mcp, ctx, window_id, float(wheel), error)
                                        : run_ui_gesture(mcp, ctx, window_id, button, is_double, is_drag, start, end, error);
             // The pointer goes back as it was whatever happened, never dropped: the user's mouse, or the game pointer
             // input_send_mouse placed.
             const bool released = release_on_main(mcp,
                                                   [context = &ctx, previous_pointer]()
                                                   {
                                                       get_input_manager(*context)->set_virtual_mouse_position(previous_pointer);
                                                   });
             wait_input_frames(ctx, 1);
             if(!done)
             {
                 return {.text = error, .is_error = true};
             }
             if(!released)
             {
                 return {.text = "The gesture ran; the editor's main thread is busy, so the pointer goes back to the "
                                 "mouse once it is free",
                         .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"screen":[{},{}]}})", start.x, start.y)};
         },
         .mutates_scene = false,
         .requires_main_thread = false});
}

} // namespace unravel::mcp
