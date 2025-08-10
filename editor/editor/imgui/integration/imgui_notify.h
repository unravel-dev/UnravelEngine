// imgui-notify by patrickcjk
// https://github.com/patrickcjk/imgui-notify
//
// Enhanced with custom draw callback support:
// - Set custom draw callbacks for notifications to add additional rendering AFTER text content
// - Callbacks receive toast reference, opacity, and text color for theming
// - Standard text content is rendered first, then callback is called for additional content
//
// Usage examples:
//
// 1. Basic text notification (existing functionality):
//    ImGui::PushNotification(ImGuiToast(ImGuiToastType_Info, "Hello World"));
//
// 2. Text + custom draw callback notification:
//    auto callback = [](const ImGuiToast& toast, float opacity, const ImVec4& text_color) {
//        ImGui::TextColored(text_color, "Additional content with opacity: %.2f", opacity);
//        ImGui::Button("Click me!");
//    };
//    ImGuiToast toast(ImGuiToastType_Success, callback);
//    toast.set_content("Main message");
//    ImGui::PushNotification(toast);
//
// 3. Title + text + custom draw:
//    ImGuiToast toast(ImGuiToastType_Warning, callback);
//    toast.set_title("Custom Title");
//    toast.set_content("Main content");
//    ImGui::PushNotification(toast);

#ifndef IMGUI_NOTIFY
#define IMGUI_NOTIFY

#pragma once
#include <vector>
#include <chrono>
#include <functional>
#include "fonts/icons/icons_material_design_icons.h"
#include <imgui_includes.h>

#define NOTIFY_MAX_TOASTS				10
#define NOTIFY_MAX_MSG_LENGTH			4096		// Max message content length
#define NOTIFY_PADDING_X				20.f		// Bottom-left X padding
#define NOTIFY_PADDING_Y				20.f		// Bottom-left Y padding
#define NOTIFY_PADDING_MESSAGE_Y		10.f		// Padding Y between each message
#define NOTIFY_FADE_IN_OUT_TIME			150			// Fade in and out duration
#define NOTIFY_DEFAULT_DISMISS			3000		// Auto dismiss after X ms (default, applied only of no data provided in constructors)
#define NOTIFY_OPACITY					1.0f		// 0-1 Toast opacity
#define NOTIFY_TOAST_FLAGS				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
// Comment out if you don't want any separator between title and content
#define NOTIFY_USE_SEPARATOR

#define NOTIFY_INLINE					inline
#define NOTIFY_NULL_OR_EMPTY(str)		(!str ||! strlen(str))
#define NOTIFY_FORMAT(fn, format, ...)	if (format) { va_list args; va_start(args, format); fn(format, args, ##__VA_ARGS__); va_end(args); }

typedef int ImGuiToastType;
typedef int ImGuiToastPhase;
typedef int ImGuiToastPos;

// Forward declaration
class ImGuiToast;

// Draw callback function signature
// Parameters: toast reference, opacity for fading, text_color for theming
typedef std::function<void(const ImGuiToast&, float, const ImVec4&)> ImGuiToastDrawCallback;

enum ImGuiToastType_
{
	ImGuiToastType_None,
	ImGuiToastType_Success,
	ImGuiToastType_Warning,
	ImGuiToastType_Error,
	ImGuiToastType_Info,
	ImGuiToastType_COUNT
};

enum ImGuiToastPhase_
{
	ImGuiToastPhase_FadeIn,
	ImGuiToastPhase_Wait,
	ImGuiToastPhase_FadeOut,
	ImGuiToastPhase_Expired,
	ImGuiToastPhase_COUNT
};

enum ImGuiToastPos_
{
	ImGuiToastPos_TopLeft,
	ImGuiToastPos_TopCenter,
	ImGuiToastPos_TopRight,
	ImGuiToastPos_BottomLeft,
	ImGuiToastPos_BottomCenter,
	ImGuiToastPos_BottomRight,
	ImGuiToastPos_Center,
	ImGuiToastPos_COUNT
};

class ImGuiToast
{
private:
	ImGuiToastType	type = ImGuiToastType_None;
	char			title[NOTIFY_MAX_MSG_LENGTH];
	char			content[NOTIFY_MAX_MSG_LENGTH];
	int				dismiss_time = NOTIFY_DEFAULT_DISMISS;
	uint64_t		creation_time = 0;
	ImGuiToastDrawCallback draw_callback = nullptr;
private:
	// Setters

	NOTIFY_INLINE auto set_title(const char* format, va_list args) { vsnprintf(this->title, sizeof(this->title), format, args); }

	NOTIFY_INLINE auto set_content(const char* format, va_list args) { vsnprintf(this->content, sizeof(this->content), format, args); }

public:
	uint64_t		unique_id = 0;

	NOTIFY_INLINE auto set_title(const char* format, ...) -> void { NOTIFY_FORMAT(this->set_title, format); }

	NOTIFY_INLINE auto set_content(const char* format, ...) -> void { NOTIFY_FORMAT(this->set_content, format); }

	NOTIFY_INLINE auto set_type(const ImGuiToastType& type) -> void { IM_ASSERT(type < ImGuiToastType_COUNT); this->type = type; };

	NOTIFY_INLINE auto set_draw_callback(const ImGuiToastDrawCallback& callback) -> void { this->draw_callback = callback; };

public:
	// Getters

	NOTIFY_INLINE auto get_title() -> char* { return this->title; };

	NOTIFY_INLINE auto get_default_title() -> const char*
	{
		if (!strlen(this->title))
		{
			switch (this->type)
			{
			case ImGuiToastType_None:
				return NULL;
			case ImGuiToastType_Success:
				return "Success";
			case ImGuiToastType_Warning:
				return "Warning";
			case ImGuiToastType_Error:
				return "Error";
			case ImGuiToastType_Info:
				return "Info";
			default:
				return NULL;
			}
		}

		return this->title;
	};

	NOTIFY_INLINE auto get_type() -> const ImGuiToastType& { return this->type; };

	
	static NOTIFY_INLINE auto get_color(ImGuiToastType type) -> const ImVec4
	{
		switch (type)
		{
		case ImGuiToastType_None:
			return { 255, 255, 255, 255 }; // White
		case ImGuiToastType_Success:
			return { 0, 255, 0, 255 }; // Green
		case ImGuiToastType_Warning:
			return { 255, 255, 0, 255 }; // Yellow
		case ImGuiToastType_Error:
			return { 255, 0, 0, 255 }; // Error
		case ImGuiToastType_Info:
			return { 255, 255, 255, 255 }; // Blue
		default:
			return { 255, 255, 255, 255 }; // White
		}
	}

	NOTIFY_INLINE auto get_color() -> const ImVec4
	{
		return get_color(this->type);
	}

	static NOTIFY_INLINE auto get_icon(ImGuiToastType type) -> const char*
	{
		switch (type)
		{
			case ImGuiToastType_None:
				return NULL;
			case ImGuiToastType_Success:
				return ICON_MDI_CHECK_CIRCLE;
			case ImGuiToastType_Warning:
				return ICON_MDI_ALERT_BOX;
			case ImGuiToastType_Error:
				return ICON_MDI_ALERT_CIRCLE;
			case ImGuiToastType_Info:
				return ICON_MDI_INFORMATION;
			default:
				return NULL;
		}
	}

	NOTIFY_INLINE auto get_icon() -> const char*
	{
		return get_icon(this->type);
	}

	NOTIFY_INLINE auto get_content() -> char* { return this->content; };

	NOTIFY_INLINE auto get_draw_callback() -> const ImGuiToastDrawCallback& { return this->draw_callback; };

	NOTIFY_INLINE auto has_draw_callback() -> bool { return this->draw_callback != nullptr; };

	NOTIFY_INLINE auto get_elapsed_time() { return get_tick_count() - this->creation_time; }

	NOTIFY_INLINE auto get_phase() -> const ImGuiToastPhase
	{
		const auto elapsed = get_elapsed_time();

		if (elapsed > NOTIFY_FADE_IN_OUT_TIME + this->dismiss_time + NOTIFY_FADE_IN_OUT_TIME)
		{
			return ImGuiToastPhase_Expired;
		}
		else if (elapsed > NOTIFY_FADE_IN_OUT_TIME + this->dismiss_time)
		{
			return ImGuiToastPhase_FadeOut;
		}
		else if (elapsed > NOTIFY_FADE_IN_OUT_TIME)
		{
			return ImGuiToastPhase_Wait;
		}
		else
		{
			return ImGuiToastPhase_FadeIn;
		}
	}

	NOTIFY_INLINE auto get_fade_percent() -> const float
	{
		const auto phase = get_phase();
		const auto elapsed = get_elapsed_time();

		if (phase == ImGuiToastPhase_FadeIn)
		{
			return ((float)elapsed / (float)NOTIFY_FADE_IN_OUT_TIME) * NOTIFY_OPACITY;
		}
		else if (phase == ImGuiToastPhase_FadeOut)
		{
			return (1.f - (((float)elapsed - (float)NOTIFY_FADE_IN_OUT_TIME - (float)this->dismiss_time) / (float)NOTIFY_FADE_IN_OUT_TIME)) * NOTIFY_OPACITY;
		}

		return 1.f * NOTIFY_OPACITY;
	}

	NOTIFY_INLINE static auto get_tick_count() -> const unsigned long long
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	NOTIFY_INLINE auto set_creation_time(uint64_t offset) -> const uint64_t { return this->creation_time = get_tick_count() + offset; }

public:
	// Constructors

	ImGuiToast(ImGuiToastType type, int dismiss_time = NOTIFY_DEFAULT_DISMISS)
	{
		IM_ASSERT(type < ImGuiToastType_COUNT);

		this->type = type;
		this->dismiss_time = dismiss_time;
		this->set_creation_time(0);

		memset(this->title, 0, sizeof(this->title));
		memset(this->content, 0, sizeof(this->content));
	}

	ImGuiToast(ImGuiToastType type, const char* format, ...) : ImGuiToast(type) { NOTIFY_FORMAT(this->set_content, format); }

	ImGuiToast(ImGuiToastType type, int dismiss_time, const char* format, ...) : ImGuiToast(type, dismiss_time) { NOTIFY_FORMAT(this->set_content, format); }

	// Constructor with draw callback
	ImGuiToast(ImGuiToastType type, const ImGuiToastDrawCallback& callback, int dismiss_time = NOTIFY_DEFAULT_DISMISS)
		: ImGuiToast(type, dismiss_time)
	{
		this->draw_callback = callback;
	}
};

namespace ImGui
{
	NOTIFY_INLINE std::vector<ImGuiToast> notifications;

	
	NOTIFY_INLINE ImGuiToast* GetNotification(uint64_t unique_id)
	{
		for(auto& toast : notifications)
		{
			if(toast.unique_id == unique_id)
			{
				return &toast;
			}
		}
		return nullptr;
	}

	/// <summary>
	/// Insert a new toast in the list
	/// </summary>
	NOTIFY_INLINE void PushNotification(const ImGuiToast& toast)
	{
		notifications.push_back(toast);
	}

	/// <summary>
	/// Insert or update a notification with a unique ID
	/// If the notification already exists, it updates the content and refreshes timing:
	/// - If still fading in: preserves current fade-in progress
	/// - If in wait/fade-out phase: resets to start of wait phase (fully visible)
	/// - If expired: starts fresh with fade-in
	/// </summary>
	NOTIFY_INLINE void PushNotification(uint64_t unique_id, const ImGuiToast& toast)
	{
		auto notification = GetNotification(unique_id);
		if(notification)
		{
			// Check current phase before updating
			auto current_phase = notification->get_phase();
			auto current_elapsed = notification->get_elapsed_time();
			
			// Update the notification contents
			*notification = toast;
			notification->unique_id = unique_id;
			
			// Refresh timing based on current phase
			if(current_phase == ImGuiToastPhase_FadeIn)
			{
				// Still fading in - preserve current fade-in progress
				notification->set_creation_time(static_cast<uint64_t>(-static_cast<int64_t>(current_elapsed)));
			}
			else if(current_phase == ImGuiToastPhase_Wait || current_phase == ImGuiToastPhase_FadeOut)
			{
				// Already fully visible or fading out - reset to start of wait phase
				notification->set_creation_time(static_cast<uint64_t>(-static_cast<int64_t>(NOTIFY_FADE_IN_OUT_TIME)));
			}
			else
			{
				// Expired - start fresh
				notification->set_creation_time(0);
			}
		}
		else
		{
			notifications.push_back(toast);
			notifications.back().unique_id = unique_id;
		}
	}

	/// <summary>
	/// Remove a toast from the list by its index
	/// </summary>
	/// <param name="index">index of the toast to remove</param>
	NOTIFY_INLINE void RemoveNotification(int index)
	{
		notifications.erase(notifications.begin() + index);
	}

	/// <summary>
	/// Render toasts, call at the end of your rendering!
	/// </summary>
	NOTIFY_INLINE void RenderNotifications()
	{
		const auto vp_pos = GetMainViewport()->Pos;
		const auto vp_size = GetMainViewport()->Size;

		float height = 0.f;

		for (auto i = 0; i < std::min<size_t>(notifications.size(), NOTIFY_MAX_TOASTS); i++)
		{
			auto* current_toast = &notifications[i];

			// Remove toast if expired
			if (current_toast->get_phase() == ImGuiToastPhase_Expired)
			{
				RemoveNotification(i);
				continue;
			}

			// Get icon, title and other data
			const auto icon = current_toast->get_icon();
			const auto title = current_toast->get_title();
			const auto content = current_toast->get_content();
			const auto default_title = current_toast->get_default_title();
			const auto opacity = current_toast->get_fade_percent(); // Get opacity based of the current phase

			// Window rendering
			auto text_color = current_toast->get_color();
			text_color.w = opacity;

			// Generate new unique name for this toast
			char window_name[50]{};
			snprintf(window_name, sizeof(window_name), "##TOAST%d", i);

			//PushStyleColor(ImGuiCol_Text, text_color);
			SetNextWindowBgAlpha(opacity);
			//SetNextWindowSizeConstraints(ImVec2(180, 0), ImVec2(FLT_MAX, FLT_MAX	));
			ImVec2 window_pos = vp_pos + ImVec2(vp_size.x - NOTIFY_PADDING_X, vp_size.y - NOTIFY_PADDING_Y - height);
			SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
			Begin(window_name, NULL, NOTIFY_TOAST_FLAGS);

			// Here we render the toast content
			{
				PushTextWrapPos(vp_size.x / 3.f); // We want to support multi-line text, this will wrap the text after 1/3 of the screen width

				bool was_title_rendered = false;

				// If an icon is set
				if (!NOTIFY_NULL_OR_EMPTY(icon))
				{
					//Text(icon); // Render icon text
					TextColored(text_color, "%s", icon);
					was_title_rendered = true;
				}

				// If a title is set
				if (!NOTIFY_NULL_OR_EMPTY(title))
				{
					// If a title and an icon is set, we want to render on same line
					if (!NOTIFY_NULL_OR_EMPTY(icon))
						SameLine();

					Text("%s",title); // Render title text
					was_title_rendered = true;
				}
				else if (!NOTIFY_NULL_OR_EMPTY(default_title))
				{
					if (!NOTIFY_NULL_OR_EMPTY(icon))
						SameLine();

					Text("%s", default_title); // Render default title text (ImGuiToastType_Success -> "Success", etc...)
					was_title_rendered = true;
				}

				// In case ANYTHING was rendered in the top, we want to add a small padding so the text (or icon) looks centered vertically
				if (was_title_rendered && !NOTIFY_NULL_OR_EMPTY(content))
				{
					SetCursorPosY(GetCursorPosY() + 5.f); // Must be a better way to do this!!!!
				}

				// If a content is set, render default text
				if (!NOTIFY_NULL_OR_EMPTY(content))
				{
					if (was_title_rendered)
					{
#ifdef NOTIFY_USE_SEPARATOR
						Separator();
#endif
					}

					Text("%s", content); // Render content text
				}

				// If a draw callback is set, call it after the text content
				if (current_toast->has_draw_callback())
				{
					// Call the custom draw callback for additional rendering
					current_toast->get_draw_callback()(*current_toast, opacity, text_color);
				}

				PopTextWrapPos();
			}

			// Save height for next toasts
			height += GetWindowHeight() + NOTIFY_PADDING_MESSAGE_Y;

			// End
			End();
		}
	}

}

#endif
