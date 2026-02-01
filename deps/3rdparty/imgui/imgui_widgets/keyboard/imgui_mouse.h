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

namespace ImMouse {

enum ImGuiMouseLayout_ {
	ImGuiMouseLayout_TwoButton,	  // Standard two-button mouse
	ImGuiMouseLayout_ThreeButton, // Three-button mouse (with middle click)

	ImGuiMouseLayout_Count
};
typedef int ImGuiMouseLayout;

enum ImGuiMouseFlags_ {
	ImGuiMouseFlags_None = 0,
	ImGuiMouseFlags_ShowPressed = 1 << 0, // Highlight buttons that are currently pressed
	ImGuiMouseFlags_ShowWheel = 1 << 1,	  // Show scroll wheel
	ImGuiMouseFlags_Recordable = 1 << 2,  // Enable button recording for keybinding selection (click buttons)
};
typedef int ImGuiMouseFlags;

// Style colors for the mouse widget
enum ImGuiMouseCol_ {
	ImGuiMouseCol_Background,		 // Mouse body background color
	ImGuiMouseCol_Border,			 // Mouse body border color
	ImGuiMouseCol_ButtonBackground,	 // Button background color
	ImGuiMouseCol_ButtonBorder,		 // Button border color
	ImGuiMouseCol_ButtonPressed,	 // Overlay color when button is pressed
	ImGuiMouseCol_ButtonHighlighted, // Overlay color when button is highlighted
	ImGuiMouseCol_ButtonRecorded,	 // Overlay color when button is recorded (for keybinding selection)
	ImGuiMouseCol_WheelBackground,	 // Scroll wheel background
	ImGuiMouseCol_WheelForeground,	 // Scroll wheel foreground/notches

	ImGuiMouseCol_COUNT
};
typedef int ImGuiMouseCol;

struct ImGuiMouseStyle {
	float Scale;		// Overall scale factor (default: 1.0f)
	float BodyWidth;	// Mouse body width (default: 60.0f)
	float BodyHeight;	// Mouse body height (default: 100.0f)
	float BodyRounding; // Mouse body rounding (default: 30.0f)
	float ButtonHeight; // Button height (default: 35.0f)
	float ButtonGap;	// Gap between buttons (default: 2.0f)
	float WheelWidth;	// Scroll wheel width (default: 10.0f)
	float WheelHeight;	// Scroll wheel height (default: 20.0f)

	ImVec4 Colors[ImGuiMouseCol_COUNT];

	ImGuiMouseStyle();
};

ImGuiMouseStyle &GetStyle();
void HighlightButton(int button, bool highlight);
void ClearHighlights();
void ClearRecorded();
const ImVector<int> &GetRecordedButtons();
void Mouse(ImGuiMouseLayout layout, ImGuiMouseFlags flags = 0);
void MouseDemo();

} // namespace ImMouse
