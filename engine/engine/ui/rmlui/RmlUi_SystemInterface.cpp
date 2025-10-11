/*
 * RmlUi Engine Platform Interface Implementation
 */

#include "RmlUi_SystemInterface.h"

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Context.h>

#include <logging/logging.h>
#include <engine/engine.h>
#include <engine/rendering/renderer.h>
#include <engine/rendering/render_window.h>
#include <simulation/simulation.h>
#include <ospp/clipboard.h>
#include <ospp/keyboard.h>
#include <ospp/mouse.h>
#include <engine/input/action_map/key.hpp>

namespace unravel
{

RmlUi_SystemInterface::RmlUi_SystemInterface()
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
}

RmlUi_SystemInterface::~RmlUi_SystemInterface()
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    cleanup_cursors();
}

auto RmlUi_SystemInterface::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    init_cursors();
    
    return true;
}

void RmlUi_SystemInterface::shutdown()
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    
    cleanup_cursors();
}


double RmlUi_SystemInterface::GetElapsedTime()
{
    auto& ctx = engine::context();
    if (!ctx.has<simulation>())
    {
        return 0.0;
    }

    const auto& sim = ctx.get_cached<simulation>();
    return std::chrono::duration<double>(sim.get_time_since_launch()).count();
}

void RmlUi_SystemInterface::SetMouseCursor(const Rml::String& cursor_name)
{
    auto& ctx = engine::context();
    auto window = ctx.get_cached<renderer>().get_main_window();
    if (!window)
    {
        return;
    }

    // Map RmlUi cursor names to engine cursor types
    os::cursor::type cursor_type = os::cursor::type::arrow;
    
    if (cursor_name == "pointer" || cursor_name == "hand")
    {
        cursor_type = os::cursor::type::hand;
    }
    else if (cursor_name == "text" || cursor_name == "caret")
    {
        cursor_type = os::cursor::type::ibeam;
    }
    else if (cursor_name == "cross")
    {
        cursor_type = os::cursor::type::crosshair;
    }
    else if (cursor_name == "move")
    {
        cursor_type = os::cursor::type::size_all;
    }
    else if (cursor_name.find("resize") != Rml::String::npos)
    {
        // Handle different resize cursors
        if (cursor_name.find("n-resize") != Rml::String::npos || 
            cursor_name.find("s-resize") != Rml::String::npos)
        {
            cursor_type = os::cursor::type::size_ns;
        }
        else if (cursor_name.find("e-resize") != Rml::String::npos || 
                 cursor_name.find("w-resize") != Rml::String::npos)
        {
            cursor_type = os::cursor::type::size_we;
        }
        else if (cursor_name.find("ne-resize") != Rml::String::npos || 
                 cursor_name.find("sw-resize") != Rml::String::npos)
        {
            cursor_type = os::cursor::type::size_nesw;
        }
        else if (cursor_name.find("nw-resize") != Rml::String::npos || 
                 cursor_name.find("se-resize") != Rml::String::npos)
        {
            cursor_type = os::cursor::type::size_nwse;
        }
    }
    else if (cursor_name == "unavailable" || cursor_name == "not-allowed")
    {
        cursor_type = os::cursor::type::not_allowed;
    }

    // Set cursor on the window
    window->get_window().set_cursor(cursor_type);
}

void RmlUi_SystemInterface::SetClipboardText(const Rml::String& text)
{
    os::clipboard::set_text(text);
}

void RmlUi_SystemInterface::GetClipboardText(Rml::String& text)
{
    text = os::clipboard::get_text();
}

void RmlUi_SystemInterface::ActivateKeyboard(Rml::Vector2f caret_position, float line_height)
{

#if UNRAVEL_PLATFORM_OS_MOBILE
    // For desktop platforms, this is typically a no-op
    // On mobile platforms, this would show the virtual keyboard
    auto& ctx = engine::context();
    auto& window = ctx.get_cached<renderer>().get_main_window();
    os::set_text_input_area(window->get_window(), os::point(caret_position.x, caret_position.y), os::area(1, line_height), 0);
    os::start_text_input(window->get_window());
#endif
}

void RmlUi_SystemInterface::DeactivateKeyboard()
{
#if UNRAVEL_PLATFORM_OS_MOBILE

    // For desktop platforms, this is typically a no-op
    // On mobile platforms, this would hide the virtual keyboard
    APPLOG_TRACE("DeactivateKeyboard");
    auto& ctx = engine::context();
    auto& window = ctx.get_cached<renderer>().get_main_window();
    os::stop_text_input(window->get_window());
#endif
}

auto RmlUi_SystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) -> bool
{
    switch (type)
    {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT: APPLOG_ERROR("{}", message); break;
        case Rml::Log::LT_WARNING: APPLOG_WARNING("{}", message); break;
        case Rml::Log::LT_INFO: APPLOG_INFO("{}", message); break;
        case Rml::Log::LT_DEBUG: APPLOG_DEBUG("{}", message); break;
        case Rml::Log::LT_ALWAYS: APPLOG_TRACE("{}", message); break;
        default: APPLOG_TRACE("{}", message); break;
    }
    return true;
}

void RmlUi_SystemInterface::init_cursors()
{
    // Initialize cursor handles if needed
    // For now, we'll rely on the ospp cursor system
}

void RmlUi_SystemInterface::cleanup_cursors()
{
    // Cleanup cursor resources if any were allocated
}

// Utility functions for input conversion
namespace RmlEngine
{

auto input_event_handler(Rml::Context* context, const os::event& event) -> bool
{
    if (!context)
    {
        return true; // Event continues propagating
    }

    bool handled = false;

    switch (event.type)
    {
        case os::events::key_down:
        {
            auto rml_key = convert_key(event.key.code);
            auto modifiers = get_key_modifier_state();
            handled = context->ProcessKeyDown(rml_key, modifiers);
            if (event.key.code == os::key::code::enter || event.key.code == os::key::code::kp_enter)
            {
                handled |= context->ProcessTextInput('\n');
            }
            break;
        }
        
        case os::events::key_up:
        {
            auto rml_key = convert_key(event.key.code);
            auto modifiers = get_key_modifier_state();
            handled = context->ProcessKeyUp(rml_key, modifiers);
            break;
        }
        
        case os::events::text_input:
        {
            // Convert text input to RmlUi character events
            for (char c : event.text.text)
            {
                if (c != 0)
                {
                    handled = context->ProcessTextInput(c) || handled;
                }
            }
            break;
        }
        
        case os::events::mouse_button:
        {
            auto rml_button = convert_mouse_button(event.button.button);
            auto modifiers = get_key_modifier_state();
            
            if (event.button.state_id == os::state::pressed)
            {
                handled = context->ProcessMouseButtonDown(rml_button, modifiers);
            }
            else if (event.button.state_id == os::state::released)
            {
                handled = context->ProcessMouseButtonUp(rml_button, modifiers);
            }
            break;
        }
        
        // case os::events::mouse_motion:
        // {
        //     auto modifiers = get_key_modifier_state();
        //     handled = context->ProcessMouseMove(event.motion.x, event.motion.y, modifiers);
        //     break;
        // }
        
        case os::events::mouse_wheel:
        {
            auto modifiers = get_key_modifier_state();
            // RmlUi expects wheel delta as integer, ospp provides float
            float wheel_delta = static_cast<float>(event.wheel.y);
            handled = context->ProcessMouseWheel(-wheel_delta, modifiers);
            break;
        }
   
        default:
            // Event not handled by RmlUi
            break;
    }

    return !handled; // Return true if event should continue propagating
}

auto convert_key(os::key::code ospp_key) -> Rml::Input::KeyIdentifier
{
    // Map ospp key codes to RmlUi key identifiers
    switch (ospp_key)
    {
        case os::key::code::unknown: return Rml::Input::KI_UNKNOWN;
        case os::key::code::space: return Rml::Input::KI_SPACE;
        // Regular digit keys
        case os::key::code::digit0: return Rml::Input::KI_0;
        case os::key::code::digit1: return Rml::Input::KI_1;
        case os::key::code::digit2: return Rml::Input::KI_2;
        case os::key::code::digit3: return Rml::Input::KI_3;
        case os::key::code::digit4: return Rml::Input::KI_4;
        case os::key::code::digit5: return Rml::Input::KI_5;
        case os::key::code::digit6: return Rml::Input::KI_6;
        case os::key::code::digit7: return Rml::Input::KI_7;
        case os::key::code::digit8: return Rml::Input::KI_8;
        case os::key::code::digit9: return Rml::Input::KI_9;
        
        // Numpad digit keys
        case os::key::code::kp_digit0: return Rml::Input::KI_NUMPAD0;
        case os::key::code::kp_digit1: return Rml::Input::KI_NUMPAD1;
        case os::key::code::kp_digit2: return Rml::Input::KI_NUMPAD2;
        case os::key::code::kp_digit3: return Rml::Input::KI_NUMPAD3;
        case os::key::code::kp_digit4: return Rml::Input::KI_NUMPAD4;
        case os::key::code::kp_digit5: return Rml::Input::KI_NUMPAD5;
        case os::key::code::kp_digit6: return Rml::Input::KI_NUMPAD6;
        case os::key::code::kp_digit7: return Rml::Input::KI_NUMPAD7;
        case os::key::code::kp_digit8: return Rml::Input::KI_NUMPAD8;
        case os::key::code::kp_digit9: return Rml::Input::KI_NUMPAD9;
        case os::key::code::a: return Rml::Input::KI_A;
        case os::key::code::b: return Rml::Input::KI_B;
        case os::key::code::c: return Rml::Input::KI_C;
        case os::key::code::d: return Rml::Input::KI_D;
        case os::key::code::e: return Rml::Input::KI_E;
        case os::key::code::f: return Rml::Input::KI_F;
        case os::key::code::g: return Rml::Input::KI_G;
        case os::key::code::h: return Rml::Input::KI_H;
        case os::key::code::i: return Rml::Input::KI_I;
        case os::key::code::j: return Rml::Input::KI_J;
        case os::key::code::k: return Rml::Input::KI_K;
        case os::key::code::l: return Rml::Input::KI_L;
        case os::key::code::m: return Rml::Input::KI_M;
        case os::key::code::n: return Rml::Input::KI_N;
        case os::key::code::o: return Rml::Input::KI_O;
        case os::key::code::p: return Rml::Input::KI_P;
        case os::key::code::q: return Rml::Input::KI_Q;
        case os::key::code::r: return Rml::Input::KI_R;
        case os::key::code::s: return Rml::Input::KI_S;
        case os::key::code::t: return Rml::Input::KI_T;
        case os::key::code::u: return Rml::Input::KI_U;
        case os::key::code::v: return Rml::Input::KI_V;
        case os::key::code::w: return Rml::Input::KI_W;
        case os::key::code::x: return Rml::Input::KI_X;
        case os::key::code::y: return Rml::Input::KI_Y;
        case os::key::code::z: return Rml::Input::KI_Z;
        case os::key::code::escape: return Rml::Input::KI_ESCAPE;
        case os::key::code::enter: return Rml::Input::KI_RETURN;
        case os::key::code::tab: return Rml::Input::KI_TAB;
        case os::key::code::backspace: return Rml::Input::KI_BACK;
        case os::key::code::insert: return Rml::Input::KI_INSERT;
        case os::key::code::del: return Rml::Input::KI_DELETE;
        case os::key::code::right: return Rml::Input::KI_RIGHT;
        case os::key::code::left: return Rml::Input::KI_LEFT;
        case os::key::code::down: return Rml::Input::KI_DOWN;
        case os::key::code::up: return Rml::Input::KI_UP;
        case os::key::code::pageup: return Rml::Input::KI_PRIOR;
        case os::key::code::pagedown: return Rml::Input::KI_NEXT;
        case os::key::code::home: return Rml::Input::KI_HOME;
        case os::key::code::end: return Rml::Input::KI_END;
        case os::key::code::capslock: return Rml::Input::KI_CAPITAL;
        case os::key::code::scrolllock: return Rml::Input::KI_SCROLL;
        case os::key::code::numlockclear: return Rml::Input::KI_NUMLOCK;
        case os::key::code::printscreen: return Rml::Input::KI_SNAPSHOT;
        case os::key::code::pause: return Rml::Input::KI_PAUSE;
        case os::key::code::f1: return Rml::Input::KI_F1;
        case os::key::code::f2: return Rml::Input::KI_F2;
        case os::key::code::f3: return Rml::Input::KI_F3;
        case os::key::code::f4: return Rml::Input::KI_F4;
        case os::key::code::f5: return Rml::Input::KI_F5;
        case os::key::code::f6: return Rml::Input::KI_F6;
        case os::key::code::f7: return Rml::Input::KI_F7;
        case os::key::code::f8: return Rml::Input::KI_F8;
        case os::key::code::f9: return Rml::Input::KI_F9;
        case os::key::code::f10: return Rml::Input::KI_F10;
        case os::key::code::f11: return Rml::Input::KI_F11;
        case os::key::code::f12: return Rml::Input::KI_F12;
        case os::key::code::lshift: return Rml::Input::KI_LSHIFT;
        case os::key::code::rshift: return Rml::Input::KI_RSHIFT;
        case os::key::code::lctrl: return Rml::Input::KI_LCONTROL;
        case os::key::code::rctrl: return Rml::Input::KI_RCONTROL;
        case os::key::code::lalt: return Rml::Input::KI_LMENU;
        case os::key::code::ralt: return Rml::Input::KI_RMENU;
        
        // Additional function keys
        case os::key::code::f13: return Rml::Input::KI_F13;
        case os::key::code::f14: return Rml::Input::KI_F14;
        case os::key::code::f15: return Rml::Input::KI_F15;
        case os::key::code::f16: return Rml::Input::KI_F16;
        case os::key::code::f17: return Rml::Input::KI_F17;
        case os::key::code::f18: return Rml::Input::KI_F18;
        case os::key::code::f19: return Rml::Input::KI_F19;
        case os::key::code::f20: return Rml::Input::KI_F20;
        case os::key::code::f21: return Rml::Input::KI_F21;
        case os::key::code::f22: return Rml::Input::KI_F22;
        case os::key::code::f23: return Rml::Input::KI_F23;
        case os::key::code::f24: return Rml::Input::KI_F24;
        
        // Numpad operations
        case os::key::code::kp_enter: return Rml::Input::KI_NUMPADENTER;
        case os::key::code::kp_multiply: return Rml::Input::KI_MULTIPLY;
        case os::key::code::kp_plus: return Rml::Input::KI_ADD;
        case os::key::code::kp_minus: return Rml::Input::KI_SUBTRACT;
        case os::key::code::kp_period: return Rml::Input::KI_DECIMAL;
        case os::key::code::kp_divide: return Rml::Input::KI_DIVIDE;
        case os::key::code::kp_equals: return Rml::Input::KI_OEM_NEC_EQUAL;
        
        // OEM keys (punctuation and symbols)
        case os::key::code::semicolon: return Rml::Input::KI_OEM_1;
        case os::key::code::equals: return Rml::Input::KI_OEM_PLUS;
        case os::key::code::comma: return Rml::Input::KI_OEM_COMMA;
        case os::key::code::minus: return Rml::Input::KI_OEM_MINUS;
        case os::key::code::period: return Rml::Input::KI_OEM_PERIOD;
        case os::key::code::slash: return Rml::Input::KI_OEM_2;
        case os::key::code::grave: return Rml::Input::KI_OEM_3;
        case os::key::code::leftbracket: return Rml::Input::KI_OEM_4;
        case os::key::code::backslash: return Rml::Input::KI_OEM_5;
        case os::key::code::rightbracket: return Rml::Input::KI_OEM_6;
        case os::key::code::apostrophe: return Rml::Input::KI_OEM_7;
        case os::key::code::nonusbackslash: return Rml::Input::KI_OEM_102;
        
        // System keys
        case os::key::code::clear: return Rml::Input::KI_CLEAR;
        case os::key::code::select: return Rml::Input::KI_SELECT;
        case os::key::code::execute: return Rml::Input::KI_EXECUTE;
        case os::key::code::help: return Rml::Input::KI_HELP;
        case os::key::code::lgui: return Rml::Input::KI_LWIN;
        case os::key::code::rgui: return Rml::Input::KI_RWIN;
        case os::key::code::application: return Rml::Input::KI_APPS;
        case os::key::code::power: return Rml::Input::KI_POWER;
        case os::key::code::sleep: return Rml::Input::KI_SLEEP;
        
        // Media keys
        case os::key::code::volumeup: return Rml::Input::KI_VOLUME_UP;
        case os::key::code::volumedown: return Rml::Input::KI_VOLUME_DOWN;
        case os::key::code::mute: return Rml::Input::KI_VOLUME_MUTE;
        case os::key::code::media_next: return Rml::Input::KI_MEDIA_NEXT_TRACK;
        case os::key::code::media_prev: return Rml::Input::KI_MEDIA_PREV_TRACK;
        case os::key::code::media_stop: return Rml::Input::KI_MEDIA_STOP;
        case os::key::code::media_play_pause: return Rml::Input::KI_MEDIA_PLAY_PAUSE;
        case os::key::code::media_select: return Rml::Input::KI_LAUNCH_MEDIA_SELECT;
        
        // Browser keys
        case os::key::code::ac_back: return Rml::Input::KI_BROWSER_BACK;
        case os::key::code::ac_forward: return Rml::Input::KI_BROWSER_FORWARD;
        case os::key::code::ac_refresh: return Rml::Input::KI_BROWSER_REFRESH;
        case os::key::code::ac_stop: return Rml::Input::KI_BROWSER_STOP;
        case os::key::code::ac_search: return Rml::Input::KI_BROWSER_SEARCH;
        case os::key::code::ac_bookmarks: return Rml::Input::KI_BROWSER_FAVORITES;
        case os::key::code::ac_home: return Rml::Input::KI_BROWSER_HOME;
        
        // International keys
        case os::key::code::lang1: return Rml::Input::KI_KANA;
        case os::key::code::lang2: return Rml::Input::KI_HANGUL;
        case os::key::code::international1: return Rml::Input::KI_CONVERT;
        case os::key::code::international2: return Rml::Input::KI_NONCONVERT;
        case os::key::code::international3: return Rml::Input::KI_ACCEPT;
        case os::key::code::international4: return Rml::Input::KI_MODECHANGE;
        
        // Additional system keys
        // Note: KI_CANCEL doesn't exist in RmlUi, mapping to unknown
        case os::key::code::cancel: return Rml::Input::KI_UNKNOWN;
        case os::key::code::prior: return Rml::Input::KI_PRIOR;
        case os::key::code::crsel: return Rml::Input::KI_CRSEL;
        case os::key::code::exsel: return Rml::Input::KI_EXSEL;
        case os::key::code::separator: return Rml::Input::KI_SEPARATOR;
        
        // Additional IME and international keys
        case os::key::code::lang3: return Rml::Input::KI_JUNJA;
        case os::key::code::lang4: return Rml::Input::KI_FINAL;
        case os::key::code::lang5: return Rml::Input::KI_HANJA;
        case os::key::code::lang6: return Rml::Input::KI_KANJI;
        
        // Meta keys (same as GUI keys on most systems)
        // Note: Using lgui/rgui as meta key equivalents since ospp doesn't have separate meta keys
        
        default: return Rml::Input::KI_UNKNOWN;
    }
}

auto convert_mouse_button(os::mouse::button ospp_button) -> int
{
    switch (ospp_button)
    {
        case os::mouse::button::left: return 0;
        case os::mouse::button::right: return 1;
        case os::mouse::button::middle: return 2;
        case os::mouse::button::x1: return 3;
        case os::mouse::button::x2: return 4;
        default: return -1;
    }
}

auto convert_rml_key_to_ospp(Rml::Input::KeyIdentifier rml_key) -> os::key::code
{
    // Map RmlUi key identifiers to ospp key codes
    switch (rml_key)
    {
        case Rml::Input::KI_UNKNOWN: return os::key::code::unknown;
        case Rml::Input::KI_SPACE: return os::key::code::space;
        
        // Regular digit keys
        case Rml::Input::KI_0: return os::key::code::digit0;
        case Rml::Input::KI_1: return os::key::code::digit1;
        case Rml::Input::KI_2: return os::key::code::digit2;
        case Rml::Input::KI_3: return os::key::code::digit3;
        case Rml::Input::KI_4: return os::key::code::digit4;
        case Rml::Input::KI_5: return os::key::code::digit5;
        case Rml::Input::KI_6: return os::key::code::digit6;
        case Rml::Input::KI_7: return os::key::code::digit7;
        case Rml::Input::KI_8: return os::key::code::digit8;
        case Rml::Input::KI_9: return os::key::code::digit9;
        
        // Letter keys
        case Rml::Input::KI_A: return os::key::code::a;
        case Rml::Input::KI_B: return os::key::code::b;
        case Rml::Input::KI_C: return os::key::code::c;
        case Rml::Input::KI_D: return os::key::code::d;
        case Rml::Input::KI_E: return os::key::code::e;
        case Rml::Input::KI_F: return os::key::code::f;
        case Rml::Input::KI_G: return os::key::code::g;
        case Rml::Input::KI_H: return os::key::code::h;
        case Rml::Input::KI_I: return os::key::code::i;
        case Rml::Input::KI_J: return os::key::code::j;
        case Rml::Input::KI_K: return os::key::code::k;
        case Rml::Input::KI_L: return os::key::code::l;
        case Rml::Input::KI_M: return os::key::code::m;
        case Rml::Input::KI_N: return os::key::code::n;
        case Rml::Input::KI_O: return os::key::code::o;
        case Rml::Input::KI_P: return os::key::code::p;
        case Rml::Input::KI_Q: return os::key::code::q;
        case Rml::Input::KI_R: return os::key::code::r;
        case Rml::Input::KI_S: return os::key::code::s;
        case Rml::Input::KI_T: return os::key::code::t;
        case Rml::Input::KI_U: return os::key::code::u;
        case Rml::Input::KI_V: return os::key::code::v;
        case Rml::Input::KI_W: return os::key::code::w;
        case Rml::Input::KI_X: return os::key::code::x;
        case Rml::Input::KI_Y: return os::key::code::y;
        case Rml::Input::KI_Z: return os::key::code::z;
        
        // Numpad digit keys
        case Rml::Input::KI_NUMPAD0: return os::key::code::kp_digit0;
        case Rml::Input::KI_NUMPAD1: return os::key::code::kp_digit1;
        case Rml::Input::KI_NUMPAD2: return os::key::code::kp_digit2;
        case Rml::Input::KI_NUMPAD3: return os::key::code::kp_digit3;
        case Rml::Input::KI_NUMPAD4: return os::key::code::kp_digit4;
        case Rml::Input::KI_NUMPAD5: return os::key::code::kp_digit5;
        case Rml::Input::KI_NUMPAD6: return os::key::code::kp_digit6;
        case Rml::Input::KI_NUMPAD7: return os::key::code::kp_digit7;
        case Rml::Input::KI_NUMPAD8: return os::key::code::kp_digit8;
        case Rml::Input::KI_NUMPAD9: return os::key::code::kp_digit9;
        
        // Navigation and control keys
        case Rml::Input::KI_ESCAPE: return os::key::code::escape;
        case Rml::Input::KI_RETURN: return os::key::code::enter;
        case Rml::Input::KI_TAB: return os::key::code::tab;
        case Rml::Input::KI_BACK: return os::key::code::backspace;
        case Rml::Input::KI_INSERT: return os::key::code::insert;
        case Rml::Input::KI_DELETE: return os::key::code::del;
        case Rml::Input::KI_RIGHT: return os::key::code::right;
        case Rml::Input::KI_LEFT: return os::key::code::left;
        case Rml::Input::KI_DOWN: return os::key::code::down;
        case Rml::Input::KI_UP: return os::key::code::up;
        case Rml::Input::KI_PRIOR: return os::key::code::pageup;
        case Rml::Input::KI_NEXT: return os::key::code::pagedown;
        case Rml::Input::KI_HOME: return os::key::code::home;
        case Rml::Input::KI_END: return os::key::code::end;
        
        // Lock keys
        case Rml::Input::KI_CAPITAL: return os::key::code::capslock;
        case Rml::Input::KI_SCROLL: return os::key::code::scrolllock;
        case Rml::Input::KI_NUMLOCK: return os::key::code::numlockclear;
        case Rml::Input::KI_SNAPSHOT: return os::key::code::printscreen;
        case Rml::Input::KI_PAUSE: return os::key::code::pause;
        
        // Function keys
        case Rml::Input::KI_F1: return os::key::code::f1;
        case Rml::Input::KI_F2: return os::key::code::f2;
        case Rml::Input::KI_F3: return os::key::code::f3;
        case Rml::Input::KI_F4: return os::key::code::f4;
        case Rml::Input::KI_F5: return os::key::code::f5;
        case Rml::Input::KI_F6: return os::key::code::f6;
        case Rml::Input::KI_F7: return os::key::code::f7;
        case Rml::Input::KI_F8: return os::key::code::f8;
        case Rml::Input::KI_F9: return os::key::code::f9;
        case Rml::Input::KI_F10: return os::key::code::f10;
        case Rml::Input::KI_F11: return os::key::code::f11;
        case Rml::Input::KI_F12: return os::key::code::f12;
        case Rml::Input::KI_F13: return os::key::code::f13;
        case Rml::Input::KI_F14: return os::key::code::f14;
        case Rml::Input::KI_F15: return os::key::code::f15;
        case Rml::Input::KI_F16: return os::key::code::f16;
        case Rml::Input::KI_F17: return os::key::code::f17;
        case Rml::Input::KI_F18: return os::key::code::f18;
        case Rml::Input::KI_F19: return os::key::code::f19;
        case Rml::Input::KI_F20: return os::key::code::f20;
        case Rml::Input::KI_F21: return os::key::code::f21;
        case Rml::Input::KI_F22: return os::key::code::f22;
        case Rml::Input::KI_F23: return os::key::code::f23;
        case Rml::Input::KI_F24: return os::key::code::f24;
        
        // Modifier keys
        case Rml::Input::KI_LSHIFT: return os::key::code::lshift;
        case Rml::Input::KI_RSHIFT: return os::key::code::rshift;
        case Rml::Input::KI_LCONTROL: return os::key::code::lctrl;
        case Rml::Input::KI_RCONTROL: return os::key::code::rctrl;
        case Rml::Input::KI_LMENU: return os::key::code::lalt;
        case Rml::Input::KI_RMENU: return os::key::code::ralt;
        case Rml::Input::KI_LWIN: return os::key::code::lgui;
        case Rml::Input::KI_RWIN: return os::key::code::rgui;
        
        // Numpad operations
        case Rml::Input::KI_NUMPADENTER: return os::key::code::kp_enter;
        case Rml::Input::KI_MULTIPLY: return os::key::code::kp_multiply;
        case Rml::Input::KI_ADD: return os::key::code::kp_plus;
        case Rml::Input::KI_SUBTRACT: return os::key::code::kp_minus;
        case Rml::Input::KI_DECIMAL: return os::key::code::kp_period;
        case Rml::Input::KI_DIVIDE: return os::key::code::kp_divide;
        case Rml::Input::KI_OEM_NEC_EQUAL: return os::key::code::kp_equals;
        
        // OEM keys (punctuation and symbols)
        case Rml::Input::KI_OEM_1: return os::key::code::semicolon;
        case Rml::Input::KI_OEM_PLUS: return os::key::code::equals;
        case Rml::Input::KI_OEM_COMMA: return os::key::code::comma;
        case Rml::Input::KI_OEM_MINUS: return os::key::code::minus;
        case Rml::Input::KI_OEM_PERIOD: return os::key::code::period;
        case Rml::Input::KI_OEM_2: return os::key::code::slash;
        case Rml::Input::KI_OEM_3: return os::key::code::grave;
        case Rml::Input::KI_OEM_4: return os::key::code::leftbracket;
        case Rml::Input::KI_OEM_5: return os::key::code::backslash;
        case Rml::Input::KI_OEM_6: return os::key::code::rightbracket;
        case Rml::Input::KI_OEM_7: return os::key::code::apostrophe;
        case Rml::Input::KI_OEM_102: return os::key::code::nonusbackslash;
        
        // System keys
        case Rml::Input::KI_CLEAR: return os::key::code::clear;
        case Rml::Input::KI_SELECT: return os::key::code::select;
        case Rml::Input::KI_EXECUTE: return os::key::code::execute;
        case Rml::Input::KI_HELP: return os::key::code::help;
        case Rml::Input::KI_APPS: return os::key::code::application;
        case Rml::Input::KI_POWER: return os::key::code::power;
        case Rml::Input::KI_SLEEP: return os::key::code::sleep;
        
        // Media keys
        case Rml::Input::KI_VOLUME_UP: return os::key::code::volumeup;
        case Rml::Input::KI_VOLUME_DOWN: return os::key::code::volumedown;
        case Rml::Input::KI_VOLUME_MUTE: return os::key::code::mute;
        case Rml::Input::KI_MEDIA_NEXT_TRACK: return os::key::code::media_next;
        case Rml::Input::KI_MEDIA_PREV_TRACK: return os::key::code::media_prev;
        case Rml::Input::KI_MEDIA_STOP: return os::key::code::media_stop;
        case Rml::Input::KI_MEDIA_PLAY_PAUSE: return os::key::code::media_play_pause;
        case Rml::Input::KI_LAUNCH_MEDIA_SELECT: return os::key::code::media_select;
        
        // Browser keys
        case Rml::Input::KI_BROWSER_BACK: return os::key::code::ac_back;
        case Rml::Input::KI_BROWSER_FORWARD: return os::key::code::ac_forward;
        case Rml::Input::KI_BROWSER_REFRESH: return os::key::code::ac_refresh;
        case Rml::Input::KI_BROWSER_STOP: return os::key::code::ac_stop;
        case Rml::Input::KI_BROWSER_SEARCH: return os::key::code::ac_search;
        case Rml::Input::KI_BROWSER_FAVORITES: return os::key::code::ac_bookmarks;
        case Rml::Input::KI_BROWSER_HOME: return os::key::code::ac_home;
        
        // International keys
        case Rml::Input::KI_KANA: return os::key::code::lang1;
        case Rml::Input::KI_HANGUL: return os::key::code::lang2;
        case Rml::Input::KI_CONVERT: return os::key::code::international1;
        case Rml::Input::KI_NONCONVERT: return os::key::code::international2;
        case Rml::Input::KI_ACCEPT: return os::key::code::international3;
        case Rml::Input::KI_MODECHANGE: return os::key::code::international4;
        
        // Additional system keys
        // Note: KI_CANCEL doesn't exist in RmlUi, but we handle cancel key in the fallback
        case Rml::Input::KI_CRSEL: return os::key::code::crsel;
        case Rml::Input::KI_EXSEL: return os::key::code::exsel;
        case Rml::Input::KI_SEPARATOR: return os::key::code::separator;
        
        // Additional IME and international keys
        case Rml::Input::KI_JUNJA: return os::key::code::lang3;
        case Rml::Input::KI_FINAL: return os::key::code::lang4;
        case Rml::Input::KI_HANJA: return os::key::code::lang5;
        case Rml::Input::KI_KANJI: return os::key::code::lang6;
        
        // Meta keys - map to GUI keys since they're equivalent on most systems
        case Rml::Input::KI_LMETA: return os::key::code::lgui;
        case Rml::Input::KI_RMETA: return os::key::code::rgui;
        
        // Keys that don't have direct ospp equivalents - return unknown
        case Rml::Input::KI_OEM_8:
        case Rml::Input::KI_OEM_FJ_JISHO:
        case Rml::Input::KI_OEM_FJ_MASSHOU:
        case Rml::Input::KI_OEM_FJ_TOUROKU:
        case Rml::Input::KI_OEM_FJ_LOYA:
        case Rml::Input::KI_OEM_FJ_ROYA:
        case Rml::Input::KI_LAUNCH_MAIL:
        case Rml::Input::KI_LAUNCH_APP1:
        case Rml::Input::KI_LAUNCH_APP2:
        case Rml::Input::KI_OEM_AX:
        case Rml::Input::KI_ICO_HELP:
        case Rml::Input::KI_ICO_00:
        case Rml::Input::KI_PROCESSKEY:
        case Rml::Input::KI_ICO_CLEAR:
        case Rml::Input::KI_ATTN:
        case Rml::Input::KI_EREOF:
        case Rml::Input::KI_PLAY:
        case Rml::Input::KI_ZOOM:
        case Rml::Input::KI_PA1:
        case Rml::Input::KI_OEM_CLEAR:
        case Rml::Input::KI_PRINT:
        case Rml::Input::KI_WAKE:
        
        // Custom key range - no ospp equivalents
        case Rml::Input::KI_FIRST_CUSTOM_KEY:
        case Rml::Input::KI_LAST_CUSTOM_KEY:
        default: return os::key::code::unknown;
    }
}

auto convert_rml_key_to_input(Rml::Input::KeyIdentifier rml_key) -> input::key_code
{
    // Map RmlUi key identifiers to engine input key codes
    switch (rml_key)
    {
        case Rml::Input::KI_UNKNOWN: return input::key_code::unknown;
        case Rml::Input::KI_SPACE: return input::key_code::space;
        
        // Regular digit keys
        case Rml::Input::KI_0: return input::key_code::digit0;
        case Rml::Input::KI_1: return input::key_code::digit1;
        case Rml::Input::KI_2: return input::key_code::digit2;
        case Rml::Input::KI_3: return input::key_code::digit3;
        case Rml::Input::KI_4: return input::key_code::digit4;
        case Rml::Input::KI_5: return input::key_code::digit5;
        case Rml::Input::KI_6: return input::key_code::digit6;
        case Rml::Input::KI_7: return input::key_code::digit7;
        case Rml::Input::KI_8: return input::key_code::digit8;
        case Rml::Input::KI_9: return input::key_code::digit9;
        
        // Letter keys
        case Rml::Input::KI_A: return input::key_code::a;
        case Rml::Input::KI_B: return input::key_code::b;
        case Rml::Input::KI_C: return input::key_code::c;
        case Rml::Input::KI_D: return input::key_code::d;
        case Rml::Input::KI_E: return input::key_code::e;
        case Rml::Input::KI_F: return input::key_code::f;
        case Rml::Input::KI_G: return input::key_code::g;
        case Rml::Input::KI_H: return input::key_code::h;
        case Rml::Input::KI_I: return input::key_code::i;
        case Rml::Input::KI_J: return input::key_code::j;
        case Rml::Input::KI_K: return input::key_code::k;
        case Rml::Input::KI_L: return input::key_code::l;
        case Rml::Input::KI_M: return input::key_code::m;
        case Rml::Input::KI_N: return input::key_code::n;
        case Rml::Input::KI_O: return input::key_code::o;
        case Rml::Input::KI_P: return input::key_code::p;
        case Rml::Input::KI_Q: return input::key_code::q;
        case Rml::Input::KI_R: return input::key_code::r;
        case Rml::Input::KI_S: return input::key_code::s;
        case Rml::Input::KI_T: return input::key_code::t;
        case Rml::Input::KI_U: return input::key_code::u;
        case Rml::Input::KI_V: return input::key_code::v;
        case Rml::Input::KI_W: return input::key_code::w;
        case Rml::Input::KI_X: return input::key_code::x;
        case Rml::Input::KI_Y: return input::key_code::y;
        case Rml::Input::KI_Z: return input::key_code::z;
        
        // Numpad digit keys
        case Rml::Input::KI_NUMPAD0: return input::key_code::kp_digit0;
        case Rml::Input::KI_NUMPAD1: return input::key_code::kp_digit1;
        case Rml::Input::KI_NUMPAD2: return input::key_code::kp_digit2;
        case Rml::Input::KI_NUMPAD3: return input::key_code::kp_digit3;
        case Rml::Input::KI_NUMPAD4: return input::key_code::kp_digit4;
        case Rml::Input::KI_NUMPAD5: return input::key_code::kp_digit5;
        case Rml::Input::KI_NUMPAD6: return input::key_code::kp_digit6;
        case Rml::Input::KI_NUMPAD7: return input::key_code::kp_digit7;
        case Rml::Input::KI_NUMPAD8: return input::key_code::kp_digit8;
        case Rml::Input::KI_NUMPAD9: return input::key_code::kp_digit9;
        
        // Navigation and control keys
        case Rml::Input::KI_ESCAPE: return input::key_code::escape;
        case Rml::Input::KI_RETURN: return input::key_code::enter;
        case Rml::Input::KI_TAB: return input::key_code::tab;
        case Rml::Input::KI_BACK: return input::key_code::backspace;
        case Rml::Input::KI_INSERT: return input::key_code::insert;
        case Rml::Input::KI_DELETE: return input::key_code::del;
        case Rml::Input::KI_RIGHT: return input::key_code::right;
        case Rml::Input::KI_LEFT: return input::key_code::left;
        case Rml::Input::KI_DOWN: return input::key_code::down;
        case Rml::Input::KI_UP: return input::key_code::up;
        case Rml::Input::KI_PRIOR: return input::key_code::prior;
        case Rml::Input::KI_NEXT: return input::key_code::pagedown;
        case Rml::Input::KI_HOME: return input::key_code::home;
        case Rml::Input::KI_END: return input::key_code::end;
        
        // Lock keys
        case Rml::Input::KI_CAPITAL: return input::key_code::capslock;
        case Rml::Input::KI_SCROLL: return input::key_code::scrolllock;
        case Rml::Input::KI_NUMLOCK: return input::key_code::numlockclear;
        case Rml::Input::KI_SNAPSHOT: return input::key_code::printscreen;
        case Rml::Input::KI_PAUSE: return input::key_code::pause;
        
        // Function keys
        case Rml::Input::KI_F1: return input::key_code::f1;
        case Rml::Input::KI_F2: return input::key_code::f2;
        case Rml::Input::KI_F3: return input::key_code::f3;
        case Rml::Input::KI_F4: return input::key_code::f4;
        case Rml::Input::KI_F5: return input::key_code::f5;
        case Rml::Input::KI_F6: return input::key_code::f6;
        case Rml::Input::KI_F7: return input::key_code::f7;
        case Rml::Input::KI_F8: return input::key_code::f8;
        case Rml::Input::KI_F9: return input::key_code::f9;
        case Rml::Input::KI_F10: return input::key_code::f10;
        case Rml::Input::KI_F11: return input::key_code::f11;
        case Rml::Input::KI_F12: return input::key_code::f12;
        case Rml::Input::KI_F13: return input::key_code::f13;
        case Rml::Input::KI_F14: return input::key_code::f14;
        case Rml::Input::KI_F15: return input::key_code::f15;
        case Rml::Input::KI_F16: return input::key_code::f16;
        case Rml::Input::KI_F17: return input::key_code::f17;
        case Rml::Input::KI_F18: return input::key_code::f18;
        case Rml::Input::KI_F19: return input::key_code::f19;
        case Rml::Input::KI_F20: return input::key_code::f20;
        case Rml::Input::KI_F21: return input::key_code::f21;
        case Rml::Input::KI_F22: return input::key_code::f22;
        case Rml::Input::KI_F23: return input::key_code::f23;
        case Rml::Input::KI_F24: return input::key_code::f24;
        
        // Modifier keys
        case Rml::Input::KI_LSHIFT: return input::key_code::lshift;
        case Rml::Input::KI_RSHIFT: return input::key_code::rshift;
        case Rml::Input::KI_LCONTROL: return input::key_code::lctrl;
        case Rml::Input::KI_RCONTROL: return input::key_code::rctrl;
        case Rml::Input::KI_LMENU: return input::key_code::lalt;
        case Rml::Input::KI_RMENU: return input::key_code::ralt;
        case Rml::Input::KI_LWIN: return input::key_code::lgui;
        case Rml::Input::KI_RWIN: return input::key_code::rgui;
        
        // Numpad operations
        case Rml::Input::KI_NUMPADENTER: return input::key_code::kp_enter;
        case Rml::Input::KI_MULTIPLY: return input::key_code::kp_multiply;
        case Rml::Input::KI_ADD: return input::key_code::kp_plus;
        case Rml::Input::KI_SUBTRACT: return input::key_code::kp_minus;
        case Rml::Input::KI_DECIMAL: return input::key_code::kp_period;
        case Rml::Input::KI_DIVIDE: return input::key_code::kp_divide;
        case Rml::Input::KI_OEM_NEC_EQUAL: return input::key_code::kp_equals;
        
        // OEM keys (punctuation and symbols)
        case Rml::Input::KI_OEM_1: return input::key_code::semicolon;
        case Rml::Input::KI_OEM_PLUS: return input::key_code::equals;
        case Rml::Input::KI_OEM_COMMA: return input::key_code::comma;
        case Rml::Input::KI_OEM_MINUS: return input::key_code::minus;
        case Rml::Input::KI_OEM_PERIOD: return input::key_code::period;
        case Rml::Input::KI_OEM_2: return input::key_code::slash;
        case Rml::Input::KI_OEM_3: return input::key_code::grave;
        case Rml::Input::KI_OEM_4: return input::key_code::leftbracket;
        case Rml::Input::KI_OEM_5: return input::key_code::backslash;
        case Rml::Input::KI_OEM_6: return input::key_code::rightbracket;
        case Rml::Input::KI_OEM_7: return input::key_code::apostrophe;
        case Rml::Input::KI_OEM_102: return input::key_code::nonusbackslash;
        
        // System keys
        case Rml::Input::KI_CLEAR: return input::key_code::clear;
        case Rml::Input::KI_SELECT: return input::key_code::select;
        case Rml::Input::KI_EXECUTE: return input::key_code::execute;
        case Rml::Input::KI_HELP: return input::key_code::help;
        case Rml::Input::KI_APPS: return input::key_code::application;
        case Rml::Input::KI_POWER: return input::key_code::power;
        case Rml::Input::KI_SLEEP: return input::key_code::sleep;
        
        // Media keys
        case Rml::Input::KI_VOLUME_UP: return input::key_code::volumeup;
        case Rml::Input::KI_VOLUME_DOWN: return input::key_code::volumedown;
        case Rml::Input::KI_VOLUME_MUTE: return input::key_code::mute;
        case Rml::Input::KI_MEDIA_NEXT_TRACK: return input::key_code::media_next;
        case Rml::Input::KI_MEDIA_PREV_TRACK: return input::key_code::media_prev;
        case Rml::Input::KI_MEDIA_STOP: return input::key_code::media_stop;
        case Rml::Input::KI_MEDIA_PLAY_PAUSE: return input::key_code::media_play_pause;
        case Rml::Input::KI_LAUNCH_MEDIA_SELECT: return input::key_code::media_select;
        
        // Browser keys
        case Rml::Input::KI_BROWSER_BACK: return input::key_code::ac_back;
        case Rml::Input::KI_BROWSER_FORWARD: return input::key_code::ac_forward;
        case Rml::Input::KI_BROWSER_REFRESH: return input::key_code::ac_refresh;
        case Rml::Input::KI_BROWSER_STOP: return input::key_code::ac_stop;
        case Rml::Input::KI_BROWSER_SEARCH: return input::key_code::ac_search;
        case Rml::Input::KI_BROWSER_FAVORITES: return input::key_code::ac_bookmarks;
        case Rml::Input::KI_BROWSER_HOME: return input::key_code::ac_home;
        
        // International keys
        case Rml::Input::KI_KANA: return input::key_code::lang1;
        case Rml::Input::KI_HANGUL: return input::key_code::lang2;
        case Rml::Input::KI_JUNJA: return input::key_code::lang3;
        case Rml::Input::KI_FINAL: return input::key_code::lang4;
        case Rml::Input::KI_HANJA: return input::key_code::lang5;
        case Rml::Input::KI_KANJI: return input::key_code::lang6;
        case Rml::Input::KI_CONVERT: return input::key_code::international1;
        case Rml::Input::KI_NONCONVERT: return input::key_code::international2;
        case Rml::Input::KI_ACCEPT: return input::key_code::international3;
        case Rml::Input::KI_MODECHANGE: return input::key_code::international4;
        
        // Additional system keys
        case Rml::Input::KI_CRSEL: return input::key_code::crsel;
        case Rml::Input::KI_EXSEL: return input::key_code::exsel;
        case Rml::Input::KI_SEPARATOR: return input::key_code::separator;
        case Rml::Input::KI_LMETA: return input::key_code::lgui; // Meta keys map to GUI keys
        case Rml::Input::KI_RMETA: return input::key_code::rgui;
        
        // Additional keys available in input::key_code
        // Note: Some input keys don't have RmlUi equivalents, but we handle the reverse here
        
        
        // Keys that don't have direct input::key_code equivalents - return unknown
        case Rml::Input::KI_OEM_8:
        case Rml::Input::KI_OEM_FJ_JISHO:
        case Rml::Input::KI_OEM_FJ_MASSHOU:
        case Rml::Input::KI_OEM_FJ_TOUROKU:
        case Rml::Input::KI_OEM_FJ_LOYA:
        case Rml::Input::KI_OEM_FJ_ROYA:
        case Rml::Input::KI_LAUNCH_MAIL:
        case Rml::Input::KI_LAUNCH_APP1:
        case Rml::Input::KI_LAUNCH_APP2:
        case Rml::Input::KI_OEM_AX:
        case Rml::Input::KI_ICO_HELP:
        case Rml::Input::KI_ICO_00:
        case Rml::Input::KI_PROCESSKEY:
        case Rml::Input::KI_ICO_CLEAR:
        case Rml::Input::KI_ATTN:
        case Rml::Input::KI_EREOF:
        case Rml::Input::KI_PLAY:
        case Rml::Input::KI_ZOOM:
        case Rml::Input::KI_PA1:
        case Rml::Input::KI_OEM_CLEAR:
        case Rml::Input::KI_PRINT:
        case Rml::Input::KI_WAKE:
        
        // Custom key range - no input::key_code equivalents
        case Rml::Input::KI_FIRST_CUSTOM_KEY:
        case Rml::Input::KI_LAST_CUSTOM_KEY:
        default: return input::key_code::unknown;
    }
}

auto get_key_modifier_state() -> int
{
    int modifiers = 0;
    
    if (os::key::is_pressed(os::key::code::lctrl) || os::key::is_pressed(os::key::code::rctrl))
    {
        modifiers |= Rml::Input::KM_CTRL;
    }
    if (os::key::is_pressed(os::key::code::lshift) || os::key::is_pressed(os::key::code::rshift))
    {
        modifiers |= Rml::Input::KM_SHIFT;
    }
    if (os::key::is_pressed(os::key::code::lalt) || os::key::is_pressed(os::key::code::ralt))
    {
        modifiers |= Rml::Input::KM_ALT;
    }
    if (os::key::is_pressed(os::key::code::lgui) || os::key::is_pressed(os::key::code::rgui))
    {
        modifiers |= Rml::Input::KM_META;
    }
    if (os::key::is_pressed(os::key::code::capslock))
    {
        modifiers |= Rml::Input::KM_CAPSLOCK;
    }
    if (os::key::is_pressed(os::key::code::numlockclear))
    {
        modifiers |= Rml::Input::KM_NUMLOCK;
    }
    if (os::key::is_pressed(os::key::code::scrolllock))
    {
        modifiers |= Rml::Input::KM_SCROLLLOCK;
    }
    
    return modifiers;
}

} // namespace RmlEngine

} // namespace unravel
