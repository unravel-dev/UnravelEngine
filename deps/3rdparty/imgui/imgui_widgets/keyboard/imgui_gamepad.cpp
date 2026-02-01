#include "imgui_gamepad.h"

namespace ImGamepad {

#ifndef IMGUI_DISABLE

ImGuiGamepadStyle::ImGuiGamepadStyle() {
	Scale = 1.0f;
	BodyWidth = 200.0f;
	BodyHeight = 120.0f;
	BodyRounding = 20.0f;
	ButtonSize = 20.0f;
	ButtonRounding = 10.0f;
	DPadSize = 40.0f;
	StickSize = 30.0f;
	TriggerWidth = 30.0f;
	TriggerHeight = 15.0f;

	Colors[ImGuiGamepadCol_Background] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
	Colors[ImGuiGamepadCol_Border] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
	Colors[ImGuiGamepadCol_ButtonBackground] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
	Colors[ImGuiGamepadCol_ButtonBorder] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
	Colors[ImGuiGamepadCol_ButtonLabel] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	Colors[ImGuiGamepadCol_ButtonPressed] = ImVec4(1.0f, 0.0f, 0.0f, 0.5f);
	Colors[ImGuiGamepadCol_ButtonHighlighted] = ImVec4(0.0f, 1.0f, 0.0f, 0.5f);
	Colors[ImGuiGamepadCol_DPadBackground] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
	Colors[ImGuiGamepadCol_StickBackground] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
	Colors[ImGuiGamepadCol_StickForeground] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
	Colors[ImGuiGamepadCol_TriggerBackground] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
	Colors[ImGuiGamepadCol_TriggerForeground] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
}

struct GamepadContext {
	ImVector<ImGuiKey> HighlightedButtons;
	ImGuiGamepadStyle Style;
};

static GamepadContext *GetContext() {
	static GamepadContext ctx;
	return &ctx;
}

ImGuiGamepadStyle &GetStyle() {
	return GetContext()->Style;
}

static ImU32 GetColorU32(ImGuiGamepadCol idx) {
	const ImGuiGamepadStyle &style = GetStyle();
	return ImGui::ColorConvertFloat4ToU32(style.Colors[idx]);
}

static bool IsButtonHighlighted(ImGuiKey button) {
	GamepadContext *ctx = GetContext();
	for (int i = 0; i < ctx->HighlightedButtons.Size; i++) {
		if (ctx->HighlightedButtons[i] == button) {
			return true;
		}
	}
	return false;
}

void HighlightButton(ImGuiKey button, bool highlight) {
	GamepadContext *ctx = GetContext();
	if (highlight) {
		if (!IsButtonHighlighted(button)) {
			ctx->HighlightedButtons.push_back(button);
		}
	} else {
		for (int i = 0; i < ctx->HighlightedButtons.Size; i++) {
			if (ctx->HighlightedButtons[i] == button) {
				ctx->HighlightedButtons.erase(&ctx->HighlightedButtons[i]);
				break;
			}
		}
	}
}

void ClearHighlights() {
	GamepadContext *ctx = GetContext();
	ctx->HighlightedButtons.clear();
}

static void RenderButton(ImDrawList *draw_list, ImVec2 center, float radius, const char *label, ImGuiKey key,
						 float scale, bool showPressed) {
	// Button background
	draw_list->AddCircleFilled(center, radius, GetColorU32(ImGuiGamepadCol_ButtonBackground));
	draw_list->AddCircle(center, radius, GetColorU32(ImGuiGamepadCol_ButtonBorder), 0, 1.5f * scale);

	// Label
	if (label) {
		ImVec2 text_size = ImGui::CalcTextSize(label);
		ImVec2 text_pos(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
		draw_list->AddText(text_pos, GetColorU32(ImGuiGamepadCol_ButtonLabel), label);
	}

	// Pressed/highlighted overlay
	bool pressed = showPressed && ImGui::IsKeyDown(key);
	bool highlighted = IsButtonHighlighted(key);
	if (pressed) {
		draw_list->AddCircleFilled(center, radius, GetColorU32(ImGuiGamepadCol_ButtonPressed));
	} else if (highlighted) {
		draw_list->AddCircleFilled(center, radius, GetColorU32(ImGuiGamepadCol_ButtonHighlighted));
	}
}

static void RenderDPad(ImDrawList *draw_list, ImVec2 center, float size, float scale, bool showPressed) {
	const float arm_width = size * 0.35f;
	const float arm_length = size * 0.5f;

	// D-pad cross shape background
	// Vertical arm
	draw_list->AddRectFilled(ImVec2(center.x - arm_width * 0.5f, center.y - arm_length),
							 ImVec2(center.x + arm_width * 0.5f, center.y + arm_length),
							 GetColorU32(ImGuiGamepadCol_DPadBackground), 3.0f * scale);
	// Horizontal arm
	draw_list->AddRectFilled(ImVec2(center.x - arm_length, center.y - arm_width * 0.5f),
							 ImVec2(center.x + arm_length, center.y + arm_width * 0.5f),
							 GetColorU32(ImGuiGamepadCol_DPadBackground), 3.0f * scale);

	// Border
	draw_list->AddRect(ImVec2(center.x - arm_width * 0.5f, center.y - arm_length),
					   ImVec2(center.x + arm_width * 0.5f, center.y + arm_length), GetColorU32(ImGuiGamepadCol_Border),
					   3.0f * scale);
	draw_list->AddRect(ImVec2(center.x - arm_length, center.y - arm_width * 0.5f),
					   ImVec2(center.x + arm_length, center.y + arm_width * 0.5f), GetColorU32(ImGuiGamepadCol_Border),
					   3.0f * scale);

	// Direction highlights
	if (showPressed) {
		if (ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp)) {
			draw_list->AddRectFilled(ImVec2(center.x - arm_width * 0.4f, center.y - arm_length),
									 ImVec2(center.x + arm_width * 0.4f, center.y - arm_width * 0.3f),
									 GetColorU32(ImGuiGamepadCol_ButtonPressed), 2.0f * scale);
		}
		if (ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown)) {
			draw_list->AddRectFilled(ImVec2(center.x - arm_width * 0.4f, center.y + arm_width * 0.3f),
									 ImVec2(center.x + arm_width * 0.4f, center.y + arm_length),
									 GetColorU32(ImGuiGamepadCol_ButtonPressed), 2.0f * scale);
		}
		if (ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft)) {
			draw_list->AddRectFilled(ImVec2(center.x - arm_length, center.y - arm_width * 0.4f),
									 ImVec2(center.x - arm_width * 0.3f, center.y + arm_width * 0.4f),
									 GetColorU32(ImGuiGamepadCol_ButtonPressed), 2.0f * scale);
		}
		if (ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight)) {
			draw_list->AddRectFilled(ImVec2(center.x + arm_width * 0.3f, center.y - arm_width * 0.4f),
									 ImVec2(center.x + arm_length, center.y + arm_width * 0.4f),
									 GetColorU32(ImGuiGamepadCol_ButtonPressed), 2.0f * scale);
		}
	}

	// Highlighted directions
	if (IsButtonHighlighted(ImGuiKey_GamepadDpadUp)) {
		draw_list->AddRectFilled(ImVec2(center.x - arm_width * 0.4f, center.y - arm_length),
								 ImVec2(center.x + arm_width * 0.4f, center.y - arm_width * 0.3f),
								 GetColorU32(ImGuiGamepadCol_ButtonHighlighted), 2.0f * scale);
	}
	if (IsButtonHighlighted(ImGuiKey_GamepadDpadDown)) {
		draw_list->AddRectFilled(ImVec2(center.x - arm_width * 0.4f, center.y + arm_width * 0.3f),
								 ImVec2(center.x + arm_width * 0.4f, center.y + arm_length),
								 GetColorU32(ImGuiGamepadCol_ButtonHighlighted), 2.0f * scale);
	}
	if (IsButtonHighlighted(ImGuiKey_GamepadDpadLeft)) {
		draw_list->AddRectFilled(ImVec2(center.x - arm_length, center.y - arm_width * 0.4f),
								 ImVec2(center.x - arm_width * 0.3f, center.y + arm_width * 0.4f),
								 GetColorU32(ImGuiGamepadCol_ButtonHighlighted), 2.0f * scale);
	}
	if (IsButtonHighlighted(ImGuiKey_GamepadDpadRight)) {
		draw_list->AddRectFilled(ImVec2(center.x + arm_width * 0.3f, center.y - arm_width * 0.4f),
								 ImVec2(center.x + arm_length, center.y + arm_width * 0.4f),
								 GetColorU32(ImGuiGamepadCol_ButtonHighlighted), 2.0f * scale);
	}
}

static void RenderAnalogStick(ImDrawList *draw_list, ImVec2 center, float size, ImGuiKey stickButton, float scale,
							  bool showPressed, bool showStickPos) {
	// Stick base (outer circle)
	draw_list->AddCircleFilled(center, size, GetColorU32(ImGuiGamepadCol_StickBackground));
	draw_list->AddCircle(center, size, GetColorU32(ImGuiGamepadCol_Border), 0, 1.5f * scale);

	// Stick position indicator (inner circle)
	ImVec2 stick_pos = center;
	if (showStickPos) {
		// In a real implementation, you'd read actual stick values
		// For demo, we just show center position
	}
	draw_list->AddCircleFilled(stick_pos, size * 0.6f, GetColorU32(ImGuiGamepadCol_StickForeground));

	// Click highlight
	bool pressed = showPressed && ImGui::IsKeyDown(stickButton);
	bool highlighted = IsButtonHighlighted(stickButton);
	if (pressed) {
		draw_list->AddCircleFilled(center, size, GetColorU32(ImGuiGamepadCol_ButtonPressed));
	} else if (highlighted) {
		draw_list->AddCircleFilled(center, size, GetColorU32(ImGuiGamepadCol_ButtonHighlighted));
	}
}

static void RenderTrigger(ImDrawList *draw_list, ImVec2 pos, float width, float height, ImGuiKey key, float scale,
						  bool showPressed, bool showTriggerLevel) {
	// Trigger background
	draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), GetColorU32(ImGuiGamepadCol_TriggerBackground),
							 3.0f * scale);
	draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), GetColorU32(ImGuiGamepadCol_Border), 3.0f * scale);

	// Trigger fill level (would show analog value in real implementation)
	if (showTriggerLevel) {
		float fill = 0.0f; // Would be actual trigger value
		if (showPressed && ImGui::IsKeyDown(key)) {
			fill = 1.0f;
		}
		if (fill > 0.0f) {
			draw_list->AddRectFilled(pos, ImVec2(pos.x + width * fill, pos.y + height),
									 GetColorU32(ImGuiGamepadCol_TriggerForeground), 3.0f * scale);
		}
	}

	// Pressed/highlighted
	bool pressed = showPressed && ImGui::IsKeyDown(key);
	bool highlighted = IsButtonHighlighted(key);
	if (pressed) {
		draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), GetColorU32(ImGuiGamepadCol_ButtonPressed),
								 3.0f * scale);
	} else if (highlighted) {
		draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
								 GetColorU32(ImGuiGamepadCol_ButtonHighlighted), 3.0f * scale);
	}
}

static void RenderShoulderButton(ImDrawList *draw_list, ImVec2 pos, float width, float height, ImGuiKey key,
								 const char *label, float scale, bool showPressed) {
	draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), GetColorU32(ImGuiGamepadCol_ButtonBackground),
							 5.0f * scale);
	draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), GetColorU32(ImGuiGamepadCol_ButtonBorder),
					   5.0f * scale);

	if (label) {
		ImVec2 text_size = ImGui::CalcTextSize(label);
		ImVec2 text_pos(pos.x + (width - text_size.x) * 0.5f, pos.y + (height - text_size.y) * 0.5f);
		draw_list->AddText(text_pos, GetColorU32(ImGuiGamepadCol_ButtonLabel), label);
	}

	bool pressed = showPressed && ImGui::IsKeyDown(key);
	bool highlighted = IsButtonHighlighted(key);
	if (pressed) {
		draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), GetColorU32(ImGuiGamepadCol_ButtonPressed),
								 5.0f * scale);
	} else if (highlighted) {
		draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
								 GetColorU32(ImGuiGamepadCol_ButtonHighlighted), 5.0f * scale);
	}
}

void Gamepad(ImGuiGamepadLayout layout, ImGuiGamepadFlags flags) {
	const ImGuiGamepadStyle &style = GetStyle();
	const float scale = style.Scale * (ImGui::GetFontSize() / 13.0f);

	const float body_width = style.BodyWidth * scale;
	const float body_height = style.BodyHeight * scale;
	const float body_rounding = style.BodyRounding * scale;
	const float button_size = style.ButtonSize * scale;
	const float dpad_size = style.DPadSize * scale;
	const float stick_size = style.StickSize * scale;
	const float trigger_width = style.TriggerWidth * scale;
	const float trigger_height = style.TriggerHeight * scale;

	const bool showPressed = (flags & ImGuiGamepadFlags_ShowPressed);
	const bool showSticks = (flags & ImGuiGamepadFlags_ShowSticks);
	const bool showTriggers = (flags & ImGuiGamepadFlags_ShowTriggers);
	const bool hideDPad = (flags & ImGuiGamepadFlags_HideDPad);
	const bool hideFaceButtons = (flags & ImGuiGamepadFlags_HideFaceButtons);
	const bool hideShoulderButtons = (flags & ImGuiGamepadFlags_HideShoulderButtons);
	const bool hideTriggerButtons = (flags & ImGuiGamepadFlags_HideTriggers);
	const bool hideSticks = (flags & ImGuiGamepadFlags_HideSticks);
	const bool hideCenterButtons = (flags & ImGuiGamepadFlags_HideCenterButtons);

	ImVec2 canvas_size(body_width + 20.0f * scale, body_height + trigger_height + 20.0f * scale);
	ImVec2 canvas_pos = ImGui::GetCursorScreenPos();

	ImGui::Dummy(canvas_size);
	ImDrawList *draw_list = ImGui::GetWindowDrawList();
	draw_list->PushClipRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

	ImVec2 body_pos(canvas_pos.x + 10.0f * scale, canvas_pos.y + trigger_height + 10.0f * scale);

	// Draw triggers at top
	if (!hideTriggerButtons) {
		RenderTrigger(draw_list, ImVec2(body_pos.x + 15.0f * scale, canvas_pos.y + 5.0f * scale), trigger_width,
					  trigger_height, ImGuiKey_GamepadL2, scale, showPressed, showTriggers);
		RenderTrigger(draw_list,
					  ImVec2(body_pos.x + body_width - trigger_width - 15.0f * scale, canvas_pos.y + 5.0f * scale),
					  trigger_width, trigger_height, ImGuiKey_GamepadR2, scale, showPressed, showTriggers);
	}

	// Draw controller body
	draw_list->AddRectFilled(body_pos, ImVec2(body_pos.x + body_width, body_pos.y + body_height),
							 GetColorU32(ImGuiGamepadCol_Background), body_rounding);
	draw_list->AddRect(body_pos, ImVec2(body_pos.x + body_width, body_pos.y + body_height),
					   GetColorU32(ImGuiGamepadCol_Border), body_rounding, 0, 2.0f * scale);

	// Shoulder buttons
	if (!hideShoulderButtons) {
		RenderShoulderButton(draw_list, ImVec2(body_pos.x + 10.0f * scale, body_pos.y + 5.0f * scale), trigger_width,
							 trigger_height * 0.8f, ImGuiKey_GamepadL1, "L1", scale, showPressed);
		RenderShoulderButton(draw_list,
							 ImVec2(body_pos.x + body_width - trigger_width - 10.0f * scale, body_pos.y + 5.0f * scale),
							 trigger_width, trigger_height * 0.8f, ImGuiKey_GamepadR1, "R1", scale, showPressed);
	}

	// D-pad (left side)
	if (!hideDPad) {
		ImVec2 dpad_center(body_pos.x + body_width * 0.22f, body_pos.y + body_height * 0.55f);
		RenderDPad(draw_list, dpad_center, dpad_size, scale, showPressed);
	}

	// Analog sticks
	if (!hideSticks) {
		// Left analog stick
		ImVec2 left_stick_center(body_pos.x + body_width * 0.35f, body_pos.y + body_height * 0.35f);
		RenderAnalogStick(draw_list, left_stick_center, stick_size * 0.5f, ImGuiKey_GamepadL3, scale, showPressed,
						  showSticks);

		// Right analog stick
		ImVec2 right_stick_center(body_pos.x + body_width * 0.65f, body_pos.y + body_height * 0.65f);
		RenderAnalogStick(draw_list, right_stick_center, stick_size * 0.5f, ImGuiKey_GamepadR3, scale, showPressed,
						  showSticks);
	}

	// Face buttons (right side) - layout varies by controller type
	if (!hideFaceButtons) {
		ImVec2 face_center(body_pos.x + body_width * 0.78f, body_pos.y + body_height * 0.45f);
		float face_spacing = button_size * 1.3f;

		const char *btn_top;
		const char *btn_right;
		const char *btn_bottom;
		const char *btn_left;

		switch (layout) {
		case ImGuiGamepadLayout_PlayStation:
			btn_top = "tri"; // Triangle
			btn_right = "O"; // Circle
			btn_bottom = "X";
			btn_left = "sq"; // Square
			break;
		case ImGuiGamepadLayout_SwitchPro:
			btn_top = "X";
			btn_right = "A";
			btn_bottom = "B";
			btn_left = "Y";
			break;
		case ImGuiGamepadLayout_Steam:
		case ImGuiGamepadLayout_Xbox:
		default:
			btn_top = "Y";
			btn_right = "B";
			btn_bottom = "A";
			btn_left = "X";
			break;
		}

		// Top button (Y/Triangle/X)
		RenderButton(draw_list, ImVec2(face_center.x, face_center.y - face_spacing * 0.5f), button_size * 0.5f, btn_top,
					 ImGuiKey_GamepadFaceUp, scale, showPressed);
		// Right button (B/Circle/A)
		RenderButton(draw_list, ImVec2(face_center.x + face_spacing * 0.5f, face_center.y), button_size * 0.5f, btn_right,
					 ImGuiKey_GamepadFaceRight, scale, showPressed);
		// Bottom button (A/Cross/B)
		RenderButton(draw_list, ImVec2(face_center.x, face_center.y + face_spacing * 0.5f), button_size * 0.5f, btn_bottom,
					 ImGuiKey_GamepadFaceDown, scale, showPressed);
		// Left button (X/Square/Y)
		RenderButton(draw_list, ImVec2(face_center.x - face_spacing * 0.5f, face_center.y), button_size * 0.5f, btn_left,
					 ImGuiKey_GamepadFaceLeft, scale, showPressed);
	}

	// Center buttons (Start/Select or equivalent)
	if (!hideCenterButtons) {
		float center_y = body_pos.y + body_height * 0.35f;
		float center_btn_size = button_size * 0.4f;

		// Back/Select/Share
		ImVec2 back_center(body_pos.x + body_width * 0.42f, center_y);
		RenderButton(draw_list, back_center, center_btn_size, nullptr, ImGuiKey_GamepadBack, scale, showPressed);

		// Start/Options
		ImVec2 start_center(body_pos.x + body_width * 0.58f, center_y);
		RenderButton(draw_list, start_center, center_btn_size, nullptr, ImGuiKey_GamepadStart, scale, showPressed);
	}

	draw_list->PopClipRect();
}

#ifndef IMGUI_DISABLE_DEMO_WINDOWS
void GamepadDemo() {
	static bool showPressed = true;
	static bool showSticks = true;
	static bool showTriggers = true;
	static int currentLayout = ImGuiGamepadLayout_Xbox;

	ImGui::Text("Gamepad Widget Demo");
	ImGui::Separator();

	// Layout selection
	ImGui::Text("Layout:");
	ImGui::SameLine();
	const char *layoutNames[] = {"Xbox", "PlayStation", "Steam", "Switch Pro"};
	if (ImGui::BeginCombo("##GamepadLayout", layoutNames[currentLayout])) {
		for (int i = 0; i < ImGuiGamepadLayout_Count; i++) {
			const bool isSelected = (currentLayout == i);
			if (ImGui::Selectable(layoutNames[i], isSelected)) {
				currentLayout = i;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	// Flags
	ImGui::Text("Options:");
	ImGui::Checkbox("Show Pressed Buttons", &showPressed);
	ImGui::Checkbox("Show Stick Positions", &showSticks);
	ImGui::Checkbox("Show Trigger Levels", &showTriggers);

	ImGui::Separator();

	// Render the gamepad
	ImGuiGamepadFlags flags = ImGuiGamepadFlags_None;
	if (showPressed) {
		flags |= ImGuiGamepadFlags_ShowPressed;
	}
	if (showSticks) {
		flags |= ImGuiGamepadFlags_ShowSticks;
	}
	if (showTriggers) {
		flags |= ImGuiGamepadFlags_ShowTriggers;
	}
	Gamepad((ImGuiGamepadLayout)currentLayout, flags);
}
#endif // IMGUI_DISABLE_DEMO_WINDOWS
#endif // IMGUI_DISABLE

} // namespace ImGamepad
