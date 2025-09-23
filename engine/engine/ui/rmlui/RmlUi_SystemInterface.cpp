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

    ctx_ = &ctx;
    init_cursors();
    
    return true;
}

void RmlUi_SystemInterface::shutdown()
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    
    cleanup_cursors();
    ctx_ = nullptr;
    window_ = nullptr;
}

void RmlUi_SystemInterface::set_window(const std::unique_ptr<render_window>& window)
{
    window_ = &window;
}

double RmlUi_SystemInterface::GetElapsedTime()
{
    if (!ctx_ || !ctx_->has<simulation>())
    {
        return 0.0;
    }

    const auto& sim = ctx_->get_cached<simulation>();
    return std::chrono::duration<double>(sim.get_time_since_launch()).count();
}

void RmlUi_SystemInterface::SetMouseCursor(const Rml::String& cursor_name)
{
    if (!window_ || !*window_)
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
    (*window_)->get_window().set_cursor(cursor_type);
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
    // For desktop platforms, this is typically a no-op
    // On mobile platforms, this would show the virtual keyboard
    APPLOG_TRACE("ActivateKeyboard at ({}, {}) height: {}", caret_position.x, caret_position.y, line_height);
}

void RmlUi_SystemInterface::DeactivateKeyboard()
{
    // For desktop platforms, this is typically a no-op
    // On mobile platforms, this would hide the virtual keyboard
    APPLOG_TRACE("DeactivateKeyboard");
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
        case os::key::code::kp_digit0: return Rml::Input::KI_0;
        case os::key::code::kp_digit1: return Rml::Input::KI_1;
        case os::key::code::kp_digit2: return Rml::Input::KI_2;
        case os::key::code::kp_digit3: return Rml::Input::KI_3;
        case os::key::code::kp_digit4: return Rml::Input::KI_4;
        case os::key::code::kp_digit5: return Rml::Input::KI_5;
        case os::key::code::kp_digit6: return Rml::Input::KI_6;
        case os::key::code::kp_digit7: return Rml::Input::KI_7;
        case os::key::code::kp_digit8: return Rml::Input::KI_8;
        case os::key::code::kp_digit9: return Rml::Input::KI_9;
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

auto get_key_modifier_state() -> int
{
    int modifiers = 0;
    
    if (os::key::is_pressed(os::key::code::lctrl) || os::key::is_pressed(os::key::code::rctrl))
        modifiers |= Rml::Input::KM_CTRL;
    if (os::key::is_pressed(os::key::code::lshift) || os::key::is_pressed(os::key::code::rshift))
        modifiers |= Rml::Input::KM_SHIFT;
    if (os::key::is_pressed(os::key::code::lalt) || os::key::is_pressed(os::key::code::ralt))
        modifiers |= Rml::Input::KM_ALT;
    if (os::key::is_pressed(os::key::code::lgui) || os::key::is_pressed(os::key::code::rgui))
        modifiers |= Rml::Input::KM_META;
    if (os::key::is_pressed(os::key::code::capslock))
        modifiers |= Rml::Input::KM_CAPSLOCK;
    if (os::key::is_pressed(os::key::code::numlockclear))
        modifiers |= Rml::Input::KM_NUMLOCK;
    if (os::key::is_pressed(os::key::code::scrolllock))
        modifiers |= Rml::Input::KM_SCROLLLOCK;
    
    return modifiers;
}

} // namespace RmlEngine

} // namespace unravel
