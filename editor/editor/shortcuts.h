#pragma once

#include <imgui/imgui.h>
#include <editor/imgui/integration/imgui.h>

namespace unravel
{
namespace shortcuts
{
// Modifier keys
inline constexpr ImGuiKey modifier_camera_speed_boost = ImGuiKey_LeftShift;
inline constexpr ImGuiKey modifier_snapping = ImGuiKey_LeftCtrl;

// File operations
inline const ImGuiKeyCombination new_scene = {ImGuiKey_LeftCtrl, ImGuiKey_N};
inline const ImGuiKeyCombination open_scene = {ImGuiKey_LeftCtrl, ImGuiKey_O};
inline const ImGuiKeyCombination save_scene = {ImGuiKey_LeftCtrl, ImGuiKey_S};
inline const ImGuiKeyCombination save_scene_as = {ImGuiKey_LeftCtrl, ImGuiKey_LeftShift, ImGuiKey_S};

// Play mode
inline constexpr ImGuiKey play_toggle = ImGuiKey_F5;
inline constexpr ImGuiKey pause_toggle = ImGuiKey_F6;
inline constexpr ImGuiKey step_frame = ImGuiKey_F7;

// Camera movement
inline constexpr ImGuiKey camera_forward = ImGuiKey_W;
inline constexpr ImGuiKey camera_backward = ImGuiKey_S;
inline constexpr ImGuiKey camera_left = ImGuiKey_A;
inline constexpr ImGuiKey camera_right = ImGuiKey_D;

// Content browser operations
inline constexpr ImGuiKey rename_item = ImGuiKey_F2;
inline constexpr ImGuiKey delete_item = ImGuiKey_Delete;
inline const ImGuiKeyCombination duplicate_item = {ImGuiKey_LeftCtrl, ImGuiKey_D};
inline constexpr ImGuiKey navigate_back = ImGuiKey_Backspace;
inline constexpr ImGuiKey item_action = ImGuiKey_Enter;
inline constexpr ImGuiKey item_action_alt = ImGuiKey_KeypadEnter;
inline constexpr ImGuiKey item_cancel = ImGuiKey_Escape;

// Selection and navigation
inline const ImGuiKeyCombination select_all = {ImGuiKey_LeftCtrl, ImGuiKey_A};
inline const ImGuiKeyCombination cut = {ImGuiKey_LeftCtrl, ImGuiKey_X};
inline const ImGuiKeyCombination copy = {ImGuiKey_LeftCtrl, ImGuiKey_C};
inline const ImGuiKeyCombination paste = {ImGuiKey_LeftCtrl, ImGuiKey_V};
inline const ImGuiKeyCombination undo = {ImGuiKey_LeftCtrl, ImGuiKey_Z};
inline const ImGuiKeyCombination redo = {ImGuiKey_LeftCtrl, ImGuiKey_Y};
inline const ImGuiKeyCombination redo_alt = {ImGuiKey_LeftCtrl, ImGuiKey_LeftShift, ImGuiKey_Z};

// Transform operations
inline constexpr ImGuiKey move_tool = ImGuiKey_W;
inline constexpr ImGuiKey rotate_tool = ImGuiKey_E;
inline constexpr ImGuiKey scale_tool = ImGuiKey_R;
inline constexpr ImGuiKey universal_tool = ImGuiKey_Q;
inline constexpr ImGuiKey bounds_tool = ImGuiKey_T;

// IK operations
inline constexpr ImGuiKey ik_ccd = ImGuiKey_K;
inline constexpr ImGuiKey ik_fabrik = ImGuiKey_L;

// View operations
inline constexpr ImGuiKey focus_selected = ImGuiKey_F;
inline const ImGuiKeyCombination frame_all = {ImGuiKey_LeftAlt, ImGuiKey_F};
inline constexpr ImGuiKey toggle_gizmo_mode = ImGuiKey_Z;
inline constexpr ImGuiKey toggle_local_global = ImGuiKey_L;
inline constexpr ImGuiKeyChord snap_scene_camera_to_selected_camera = ImGuiKey_F | ImGuiMod_Shift;

// Function keys
inline constexpr ImGuiKey help = ImGuiKey_F1;

// Utility functions
inline auto get_shortcut_name(const ImGuiKeyCombination& shortcut) -> std::string
{
    return ImGui::GetKeyCombinationName(shortcut);
}

inline auto get_shortcut_name(ImGuiKey key) -> std::string
{
    return ImGui::GetKeyName(key);
}

} // namespace shortcuts
} // namespace unravel 