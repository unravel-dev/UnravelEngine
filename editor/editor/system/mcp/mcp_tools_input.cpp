#include "mcp_input_names.h"
#include "mcp_input_utils.h"
#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>
#include <editor/system/mcp_manager.h>
#include <engine/input/input.h>
#include <engine/rendering/renderer.h>

#include <ospp/event.h>
#include <ospp/mouse.h>

#include <hpp/string_view.hpp>

#include <chrono>
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

auto parse_mouse_button(std::string_view name, os::mouse::button& out) -> bool
{
    constexpr std::pair<const char*, os::mouse::button> buttons[] = {{"left", os::mouse::button::left},
                                                                     {"right", os::mouse::button::right},
                                                                     {"middle", os::mouse::button::middle},
                                                                     {"x1", os::mouse::button::x1},
                                                                     {"x2", os::mouse::button::x2}};
    for(const auto& [button_name, button] : buttons)
    {
        if(name == button_name)
        {
            out = button;
            return true;
        }
    }
    return false;
}

/// One key of a call, with the modifiers held around it.
struct key_request
{
    os::key::code code{};
    bool alt{false};
    bool ctrl{false};
    bool shift{false};
};

void push_key_event(uint32_t window_id, os::key::code code, bool down)
{
    os::event event{};
    event.type = down ? os::events::key_down : os::events::key_up;
    event.key.window_id = window_id;
    event.key.code = code;
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

/// Presses (down) or lets go of (up) a sequence: presses in order, releases in reverse.
void push_key_sequence(uint32_t window_id, const std::vector<os::key::code>& codes, bool down)
{
    if(down)
    {
        std::for_each(codes.begin(),
                      codes.end(),
                      [window_id](os::key::code code)
                      {
                          push_key_event(window_id, code, true);
                      });
        return;
    }
    std::for_each(codes.rbegin(),
                  codes.rend(),
                  [window_id](os::key::code code)
                  {
                      push_key_event(window_id, code, false);
                  });
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
    std::string_view name;
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
    return fmt::format(
        R"({{"available":true,"input_allowed":{},"play_active":{},"input_frame":{},)"
        R"("work_zone":{{"x":{},"y":{},"w":{},"h":{}}},"virtual_mouse":{},)"
        R"("game_pointer":[{},{}],"real_cursor":[{},{}],"real_cursor_in_game_view":{}}})",
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
        bool_to_json(real_cursor_in_view));
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
    if(!ctx.has<input_system>())
    {
        return nullptr;
    }
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
    while(manager->get_input_frame() < target)
    {
        if(std::chrono::steady_clock::now() > deadline)
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
    auto* manager = get_input_manager(ctx);
    for(uint64_t frame = 0; focus_game_panel && !manager->is_input_allowed() && frame < k_focus_frames; ++frame)
    {
        if(!wait_input_frames(ctx, 1))
        {
            error = "The editor is not advancing frames";
            return false;
        }
    }
    if(focus_game_panel && !manager->is_input_allowed())
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
    const auto push_button = [&](bool down)
    {
        return push_on_main(mcp,
                            [&]
                            {
                                push_mouse_button_event(window_id, button, down, virtual_pointer(ctx));
                            });
    };
    if(!push_button(true))
    {
        error = "Timed out reaching the editor main thread; nothing was pressed";
        return false;
    }
    const bool held = wait_input_frames(ctx, frames);
    // Released whatever happened above, so a stalled editor does not keep the button held.
    const bool released = push_button(false) && wait_input_frames(ctx, 1);
    if(!held || !released)
    {
        error = "The editor stopped advancing frames during the click";
        return false;
    }
    return true;
}

void register_input_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "input_get_state",
         .description =
             "Whether synthesized input will reach the game: input_allowed is false while the Game panel is not "
             "focused, and nothing reaches the action map then. Also reports the work zone (the Game panel's "
             "rect), the virtual mouse position if set, and the input frame counter. Check this first when a key "
             "or click seems to do nothing.",
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
             "Game panel first (unless focus_game_panel is false) and waits until it accepts input.",
         .input_schema_json =
             R"json({"type":"object","properties":{"keys":{"type":"array","items":{"anyOf":[{"type":"string"},{"type":"object","properties":{"key":{"type":"string"},"alt":{"type":"boolean"},"ctrl":{"type":"boolean"},"shift":{"type":"boolean"}},"required":["key"]}]},"description":"Key names, or objects with modifiers"},"text":{"type":"string","description":"Characters to type (UTF-8), sent after the keys go down"},"state":{"type":"string","enum":["press","down","up"],"description":"Default press"},"frames":{"type":"integer","minimum":1,"maximum":600,"description":"state press: frames to hold; default 1"},"focus_game_panel":{"type":"boolean","description":"Default true"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
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
             const bool released = push_on_main(mcp,
                                                [&]
                                                {
                                                    push_key_sequence(window_id, codes, false);
                                                });
             if(!held || !released)
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
             "pixels) - and must lie inside the Game panel. button with state click (default) presses for "
             "`frames` frames, then releases; down / up hold and release. wheel scrolls (positive away from the "
             "viewer). Move, button and wheel combine, in that order. Play mode only; the pointer clears itself "
             "when play ends.",
         .input_schema_json =
             R"json({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"space":{"type":"string","enum":["game","screen"],"description":"Default game"},"button":{"type":"string","enum":["left","right","middle","x1","x2"]},"state":{"type":"string","enum":["click","down","up"],"description":"Default click"},"frames":{"type":"integer","minimum":1,"maximum":600,"description":"state click: frames to hold; default 1"},"wheel":{"type":"number"},"clear":{"type":"boolean","description":"Release the virtual pointer"},"focus_game_panel":{"type":"boolean","description":"Default true"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             bool clear = false;
             read_bool(args, "clear", clear);
             if(clear)
             {
                 push_on_main(mcp,
                              [&]
                              {
                                  if(auto* manager = get_input_manager(ctx))
                                  {
                                      manager->set_virtual_mouse_position({});
                                  }
                              });
                 return {.text = R"({"ok":true,"cleared":true})"};
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
                     if((has_button || has_wheel) && !manager->get_virtual_mouse_position())
                     {
                         error = "No virtual pointer: pass x and y first";
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
}

} // namespace unravel::mcp
