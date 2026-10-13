#include "mcp_input_names.h"
#include "mcp_input_utils.h"
#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>
#include <editor/system/mcp_manager.h>
#include <engine/input/input.h>
#include <engine/rendering/renderer.h>

#include <ospp/event.h>
#include <ospp/mouse.h>

#include <cctype>
#include <chrono>
#include <map>
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

/// The characters typing produces: key events carry no text, so a focused text field reads this.
void push_text_input_event(uint32_t window_id, const std::string& text)
{
    os::event event{};
    event.type = os::events::text_input;
    event.text.window_id = window_id;
    event.text.text = text;
    os::push_event(event);
}

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

auto get_input_manager(rtti::context& ctx) -> input::os_input_manager*
{
    if(!ctx.has<input_system>())
    {
        return nullptr;
    }
    return &ctx.get_cached<input_system>().manager;
}

/// Waits on the calling (worker) thread until the editor has consumed `frames` input
/// frames, so the pressed -> down -> released transitions a caller asked for actually
/// happen. Returns false when the editor stops pumping frames.
auto wait_input_frames(rtti::context& ctx, uint64_t frames) -> bool
{
    auto* manager = get_input_manager(ctx);
    if(manager == nullptr)
    {
        return false;
    }
    const auto target = manager->get_input_frame() + frames;
    const auto deadline = std::chrono::steady_clock::now() + k_frame_wait_timeout +
                          std::chrono::milliseconds(frames * 34);
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

/// Focuses the Game panel unless the caller opted out. Without focus the Game panel
/// clears is_input_allowed every frame and nothing reaches the action map.
auto ensure_game_input_allowed(rtti::context& ctx, const simdjson::dom::object& args, std::string& error)
    -> bool
{
    bool focus_game_panel = true;
    read_bool(args, "focus_game_panel", focus_game_panel);
    if(!focus_game_panel)
    {
        return true;
    }
    return editor_actions::focus_game_panel(ctx, &error);
}

void set_game_pointer(input::os_input_manager& manager, input::coord game_position)
{
    manager.set_virtual_mouse_position(manager.remap_from_work_zone(game_position));
}

namespace
{

constexpr int64_t k_max_hold_frames = 600;


auto parse_mouse_button(std::string_view name, os::mouse::button& out) -> bool
{
    if(name == "left")
    {
        out = os::mouse::button::left;
        return true;
    }
    if(name == "right")
    {
        out = os::mouse::button::right;
        return true;
    }
    if(name == "middle")
    {
        out = os::mouse::button::middle;
        return true;
    }
    if(name == "x1")
    {
        out = os::mouse::button::x1;
        return true;
    }
    if(name == "x2")
    {
        out = os::mouse::button::x2;
        return true;
    }
    return false;
}

auto mouse_button_names() -> const char*
{
    return "left, right, middle, x1, x2";
}

struct key_request
{
    os::key::code code{};
    std::string name;
    bool alt{false};
    bool ctrl{false};
    bool shift{false};
};

void push_key_event(uint32_t window_id, const key_request& key, bool down)
{
    os::event event{};
    event.type = down ? os::events::key_down : os::events::key_up;
    event.key.window_id = window_id;
    event.key.code = key.code;
    event.key.alt = key.alt;
    event.key.ctrl = key.ctrl;
    event.key.shift = key.shift;
    event.key.system = false;
    os::push_event(event);
}



auto read_key_requests(const simdjson::dom::object& args, std::vector<key_request>& out, std::string& error)
    -> bool
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
        std::string_view as_name;
        if(item.get_string().get(as_name) == simdjson::SUCCESS)
        {
            request.name = std::string(as_name);
        }
        else
        {
            simdjson::dom::object object;
            if(item.get_object().get(object) != simdjson::SUCCESS)
            {
                error = "Each key must be a name or an object with a key field";
                return false;
            }
            std::string_view name;
            if(object["key"].get_string().get(name) != simdjson::SUCCESS)
            {
                error = "Each key object needs a key field";
                return false;
            }
            request.name = std::string(name);
            read_bool(object, "alt", request.alt);
            read_bool(object, "ctrl", request.ctrl);
            read_bool(object, "shift", request.shift);
        }
        if(!parse_key_name(request.name, request.code, error))
        {
            return false;
        }
        out.push_back(std::move(request));
    }
    if(out.empty())
    {
        error = "keys is empty";
        return false;
    }
    return true;
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


} // namespace

void register_input_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "input_get_state",
         .description =
             "Whether synthesized input will reach the game, and the pointer state it would use. "
             "input_allowed is false while the Game panel is not focused - the Game panel clears it "
             "every frame - and nothing reaches the action map then. Also reports the work zone (the "
             "Game panel's rect), the virtual mouse position if one is set, and the input frame "
             "counter. Check this first when a key or click seems to do nothing.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             return {.text = input_state_json(ctx), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "input_send_keys",
         .description =
             "Synthesize keyboard input. state \"down\" holds the keys until a later \"up\" (a "
             "held key reads through Input.GetDigitalValue / GetAxis for as long as it is down); "
             "\"press\" taps them for one frame, which is what Input.IsPressed sees. Key names are "
             "the platform's own, matched ignoring case, spaces and dashes (\"w\", \"space\", "
             "\"left shift\", \"lshift\", \"f1\"); an unknown name is rejected "
             "with suggestions. text types characters into the focused text field (a UI input): key "
             "events carry no characters, so typing sends text as the OS does; pass keys, text or "
             "both. Scripts only run in play mode, so enter play mode first. Focuses the Game panel "
             "unless focus_game_panel is false, because input is dropped while it is unfocused.",
         .input_schema_json =
             R"json({"type":"object","properties":{"keys":{"type":"array","items":{"oneOf":[{"type":"string"},{"type":"object","properties":{"key":{"type":"string"},"alt":{"type":"boolean"},"ctrl":{"type":"boolean"},"shift":{"type":"boolean"}},"required":["key"]}]},"description":"Key names, or objects with modifiers"},"text":{"type":"string","description":"Characters to type (UTF-8), sent after the key presses"},"state":{"type":"string","enum":["down","up","press"],"description":"Default press"},"focus_game_panel":{"type":"boolean","description":"Default true"}}})json",
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
                 return {.text = "state must be down, up or press", .is_error = true};
             }

             const bool wants_down = state == "down" || state == "press";
             auto pushed = mcp.invoke_on_main(
                 [&ctx, &args, &keys, &text, wants_down]() -> tool_result
                 {
                     std::string focus_error;
                     if(!ensure_game_input_allowed(ctx, args, focus_error))
                     {
                         return {.text = focus_error, .is_error = true};
                     }
                     uint32_t window_id = 0;
                     std::string window_error;
                     if(!get_main_window_id(ctx, window_id, window_error))
                     {
                         return {.text = window_error, .is_error = true};
                     }
                     for(const auto& key : keys)
                     {
                         push_key_event(window_id, key, wants_down);
                     }
                     if(wants_down && !text.empty())
                     {
                         push_text_input_event(window_id, text);
                     }
                     return {.text = fmt::format("{}", window_id), .is_error = false};
                 });
             if(!pushed)
             {
                 return {.text = "Timed out reaching the editor main thread", .is_error = true};
             }
             if(pushed->is_error)
             {
                 return *pushed;
             }

             if(state != "press")
             {
                 return {.text = fmt::format(R"({{"ok":true,"state":{},"keys":{}}})",
                                             make_json_string(state),
                                             keys.size()),
                         .is_error = false};
             }

             // A tap has to span a frame boundary: pushing down and up into the same frame
             // leaves the key released, and the press is never observed.
             const uint32_t window_id = static_cast<uint32_t>(std::stoul(pushed->text));
             if(!wait_input_frames(ctx, 1))
             {
                 return {.text = "The editor is not advancing frames; the keys may still be held",
                         .is_error = true};
             }
             auto released = mcp.invoke_on_main(
                 [&keys, window_id]() -> tool_result
                 {
                     for(const auto& key : keys)
                     {
                         push_key_event(window_id, key, false);
                     }
                     return {.text = "ok", .is_error = false};
                 });
             if(!released)
             {
                 return {.text = "Timed out releasing the keys on the editor main thread",
                         .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"state":"press","keys":{}}})", keys.size()),
                     .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "input_hold_keys",
         .description =
             "Hold keys down for a number of frames and release them, waiting for the frames to "
             "actually elapse. This is how a movement action is driven: hold \"w\" for 60 frames and "
             "the character walks for 60 frames. Use viewport_capture_game before and after to see "
             "the result.",
         .input_schema_json =
             R"json({"type":"object","properties":{"keys":{"type":"array","items":{"oneOf":[{"type":"string"},{"type":"object","properties":{"key":{"type":"string"},"alt":{"type":"boolean"},"ctrl":{"type":"boolean"},"shift":{"type":"boolean"}},"required":["key"]}]}},"frames":{"type":"integer","minimum":1,"maximum":600,"description":"Default 30"},"focus_game_panel":{"type":"boolean","description":"Default true"}},"required":["keys"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();

             std::vector<key_request> keys;
             std::string error;
             if(!read_key_requests(args, keys, error))
             {
                 return {.text = error, .is_error = true};
             }
             auto frames = read_int64_or(args, "frames", 30);
             frames = std::clamp<int64_t>(frames, 1, k_max_hold_frames);

             auto pushed = mcp.invoke_on_main(
                 [&ctx, &args, &keys]() -> tool_result
                 {
                     std::string focus_error;
                     if(!ensure_game_input_allowed(ctx, args, focus_error))
                     {
                         return {.text = focus_error, .is_error = true};
                     }
                     uint32_t window_id = 0;
                     std::string window_error;
                     if(!get_main_window_id(ctx, window_id, window_error))
                     {
                         return {.text = window_error, .is_error = true};
                     }
                     for(const auto& key : keys)
                     {
                         push_key_event(window_id, key, true);
                     }
                     return {.text = fmt::format("{}", window_id), .is_error = false};
                 });
             if(!pushed)
             {
                 return {.text = "Timed out reaching the editor main thread", .is_error = true};
             }
             if(pushed->is_error)
             {
                 return *pushed;
             }
             const uint32_t window_id = static_cast<uint32_t>(std::stoul(pushed->text));

             const bool advanced = wait_input_frames(ctx, static_cast<uint64_t>(frames));

             // Release even when the wait failed, so a stalled editor does not leave the key
             // stuck down for every later call.
             auto released = mcp.invoke_on_main(
                 [&keys, window_id]() -> tool_result
                 {
                     for(const auto& key : keys)
                     {
                         push_key_event(window_id, key, false);
                     }
                     return {.text = "ok", .is_error = false};
                 });
             if(!released)
             {
                 return {.text = "Timed out releasing the keys on the editor main thread",
                         .is_error = true};
             }
             if(!advanced)
             {
                 return {.text = "The editor did not advance the requested frames; keys released",
                         .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"keys":{},"frames":{}}})", keys.size(), frames),
                     .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "input_set_mouse_position",
         .description =
             "Set a virtual pointer that stands in for the real cursor - for the game, its UI and the "
             "editor UI alike, as a real cursor over the Game panel would - without moving the user's "
             "cursor. While it is set the user's own mouse does not reach the editor. Play mode only; it "
             "clears itself when play ends, and clear:true hands the pointer back sooner. space "
             "\"game\" (default) is pixels in the game's render target, the space a script's "
             "Input.mousePosition reads; \"screen\" is desktop pixels. Keep it inside the Game panel: "
             "clicks and the wheel only register there.",
         .input_schema_json =
             R"json({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"space":{"type":"string","enum":["game","screen"],"description":"Default game"},"clear":{"type":"boolean","description":"Release the virtual pointer"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto* manager = get_input_manager(ctx);
             if(manager == nullptr)
             {
                 return {.text = "input_system unavailable", .is_error = true};
             }

             bool clear = false;
             read_bool(args, "clear", clear);
             std::string error;
             if(!clear && !editor_actions::get_play_state(ctx).is_active)
             {
                 return {.text = "Play mode only: the virtual pointer also stands in for the editor's mouse, "
                                 "and it clears itself when play ends",
                         .is_error = true};
             }
             if(clear)
             {
                 manager->set_virtual_mouse_position({});
                 return {.text = fmt::format(R"({{"ok":true,"cleared":true,"state":{}}})",
                                             input_state_json(ctx)),
                         .is_error = false};
             }

             double x = 0.0;
             double y = 0.0;
             if(args["x"].get(x) || args["y"].get(y))
             {
                 return {.text = "Pass x and y, or clear:true", .is_error = true};
             }
             std::string space = "game";
             read_string(args, "space", space);
             if(space != "game" && space != "screen")
             {
                 return {.text = "space must be game or screen", .is_error = true};
             }

             input::coord position{static_cast<int>(x), static_cast<int>(y)};
             if(space == "game")
             {
                 set_game_pointer(*manager, position);
                 position = *manager->get_virtual_mouse_position();
             }
             else
             {
                 manager->set_virtual_mouse_position(position);
             }

             return {.text = fmt::format(R"({{"ok":true,"screen":[{},{}],"state":{}}})",
                                         position.x,
                                         position.y,
                                         input_state_json(ctx)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "input_send_mouse",
         .description =
             "Synthesize a mouse button or the scroll wheel at the virtual pointer. Requires one: set it "
             "with input_set_mouse_position first - without it the press would land wherever the real "
             "cursor rests, on the editor itself. state \"down\" holds the button, \"up\" releases "
             "it, \"click\" taps it across one frame - what a UI button's click handler needs.",
         .input_schema_json =
             R"json({"type":"object","properties":{"button":{"type":"string","enum":["left","right","middle","x1","x2"]},"state":{"type":"string","enum":["down","up","click"],"description":"Default click"},"wheel":{"type":"number","description":"Scroll amount; positive scrolls away from the viewer"},"focus_game_panel":{"type":"boolean","description":"Default true"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();

             std::string button_name;
             const bool has_button = read_string(args, "button", button_name) && !button_name.empty();
             double wheel = 0.0;
             const bool has_wheel = args["wheel"].get(wheel) == simdjson::SUCCESS;
             if(!has_button && !has_wheel)
             {
                 return {.text = std::string("Pass button (") + mouse_button_names() + ") and/or wheel",
                         .is_error = true};
             }

             os::mouse::button button{};
             if(has_button && !parse_mouse_button(button_name, button))
             {
                 return {.text = std::string("Unknown button \"") + button_name + "\"; expected one of " +
                                 mouse_button_names(),
                         .is_error = true};
             }
             std::string state = "click";
             read_string(args, "state", state);
             if(state != "down" && state != "up" && state != "click")
             {
                 return {.text = "state must be down, up or click", .is_error = true};
             }

             const bool wants_down = state == "down" || state == "click";
             auto pushed = mcp.invoke_on_main(
                 [&ctx, &args, has_button, button, has_wheel, wheel, wants_down]() -> tool_result
                 {
                     std::string focus_error;
                     if(!ensure_game_input_allowed(ctx, args, focus_error))
                     {
                         return {.text = focus_error, .is_error = true};
                     }
                     uint32_t window_id = 0;
                     std::string window_error;
                     if(!get_main_window_id(ctx, window_id, window_error))
                     {
                         return {.text = window_error, .is_error = true};
                     }
                     auto* manager = get_input_manager(ctx);
                     if(manager == nullptr)
                     {
                         return {.text = "input_system unavailable", .is_error = true};
                     }
                     const auto& virtual_pos = manager->get_virtual_mouse_position();
                     if(!virtual_pos)
                     {
                         return {.text = "No virtual pointer: set one with input_set_mouse_position first, or "
                                         "the press would land wherever the real cursor rests",
                                 .is_error = true};
                     }
                     const input::coord position = *virtual_pos;
                     if(has_button)
                     {
                         push_mouse_button_event(window_id, button, wants_down, position);
                     }
                     if(has_wheel)
                     {
                         os::event event{};
                         event.type = os::events::mouse_wheel;
                         event.wheel.window_id = window_id;
                         event.wheel.x = 0.0;
                         event.wheel.y = wheel;
                         os::push_event(event);
                     }
                     return {.text = fmt::format("{}", window_id), .is_error = false};
                 });
             if(!pushed)
             {
                 return {.text = "Timed out reaching the editor main thread", .is_error = true};
             }
             if(pushed->is_error)
             {
                 return *pushed;
             }

             if(!has_button || state != "click")
             {
                 return {.text = fmt::format(R"({{"ok":true,"state":{}}})", make_json_string(state)),
                         .is_error = false};
             }

             const uint32_t window_id = static_cast<uint32_t>(std::stoul(pushed->text));
             if(!wait_input_frames(ctx, 1))
             {
                 return {.text = "The editor is not advancing frames; the button may still be held",
                         .is_error = true};
             }
             auto released = mcp.invoke_on_main(
                 [&ctx, window_id, button]() -> tool_result
                 {
                     auto* manager = get_input_manager(ctx);
                     const auto* virtual_pos =
                         manager != nullptr ? &manager->get_virtual_mouse_position() : nullptr;
                     const input::coord position =
                         (virtual_pos != nullptr && *virtual_pos) ? **virtual_pos : input::coord{};
                     push_mouse_button_event(window_id, button, false, position);
                     return {.text = "ok", .is_error = false};
                 });
             if(!released)
             {
                 return {.text = "Timed out releasing the button on the editor main thread",
                         .is_error = true};
             }
             return {.text = R"({"ok":true,"state":"click"})", .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});
}

} // namespace unravel::mcp
