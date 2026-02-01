// License: MIT
// Copyright (c) 2026 Martin Gerhardy
//
// https://github.com/mgerhardy/imgui_keyboard
//
// The MIT License (MIT)
//
// Copyright (c) 2023 Martin Gerhardy
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <imgui/imgui.h>

namespace ImGamepad {

enum ImGuiGamepadLayout_ {
	ImGuiGamepadLayout_Xbox,		// Xbox controller layout
	ImGuiGamepadLayout_PlayStation, // PlayStation controller layout
	ImGuiGamepadLayout_Steam,		// Steam controller layout
	ImGuiGamepadLayout_SwitchPro,	// Nintendo Switch pro controller layout

	ImGuiGamepadLayout_Count
};
typedef int ImGuiGamepadLayout;

enum ImGuiGamepadFlags_ {
	ImGuiGamepadFlags_None = 0,
	ImGuiGamepadFlags_ShowPressed = 1 << 0,		  // Highlight buttons that are currently pressed
	ImGuiGamepadFlags_ShowSticks = 1 << 1,		  // Show analog stick positions
	ImGuiGamepadFlags_ShowTriggers = 1 << 2,	  // Show trigger positions
	ImGuiGamepadFlags_HideDPad = 1 << 3,		  // Hide the D-pad
	ImGuiGamepadFlags_HideFaceButtons = 1 << 4,	  // Hide the face buttons (A/B/X/Y etc)
	ImGuiGamepadFlags_HideShoulderButtons = 1 << 5, // Hide the shoulder buttons (L1/R1)
	ImGuiGamepadFlags_HideTriggers = 1 << 6,	  // Hide the trigger buttons (L2/R2)
	ImGuiGamepadFlags_HideSticks = 1 << 7,		  // Hide the analog sticks
	ImGuiGamepadFlags_HideCenterButtons = 1 << 8,  // Hide the center buttons (Start/Back)
};
typedef int ImGuiGamepadFlags;

enum ImGuiGamepadCol_ {
	ImGuiGamepadCol_Background,			// Controller body background
	ImGuiGamepadCol_Border,				// Controller body border
	ImGuiGamepadCol_ButtonBackground,	// Face button background
	ImGuiGamepadCol_ButtonBorder,		// Face button border
	ImGuiGamepadCol_ButtonLabel,		// Button label color
	ImGuiGamepadCol_ButtonPressed,		// Overlay when button pressed
	ImGuiGamepadCol_ButtonHighlighted,	// Overlay when button highlighted
	ImGuiGamepadCol_DPadBackground,		// D-pad background
	ImGuiGamepadCol_StickBackground,	// Analog stick background
	ImGuiGamepadCol_StickForeground,	// Analog stick position indicator
	ImGuiGamepadCol_TriggerBackground,	// Trigger background
	ImGuiGamepadCol_TriggerForeground,	// Trigger fill level

	ImGuiGamepadCol_COUNT
};
typedef int ImGuiGamepadCol;

struct ImGuiGamepadStyle {
	float Scale;		  // Overall scale factor (default: 1.0f)
	float BodyWidth;	  // Controller body width (default: 200.0f)
	float BodyHeight;	  // Controller body height (default: 120.0f)
	float BodyRounding;	  // Controller body rounding (default: 20.0f)
	float ButtonSize;	  // Face button size (default: 20.0f)
	float ButtonRounding; // Face button rounding (default: 10.0f)
	float DPadSize;		  // D-pad size (default: 40.0f)
	float StickSize;	  // Analog stick size (default: 30.0f)
	float TriggerWidth;	  // Trigger width (default: 30.0f)
	float TriggerHeight;  // Trigger height (default: 15.0f)

	ImVec4 Colors[ImGuiGamepadCol_COUNT];

	ImGuiGamepadStyle();
};

ImGuiGamepadStyle &GetStyle();
void HighlightButton(ImGuiKey button, bool highlight);
void ClearHighlights();
void Gamepad(ImGuiGamepadLayout layout, ImGuiGamepadFlags flags = 0);
void GamepadDemo();

} // namespace ImGamepad
