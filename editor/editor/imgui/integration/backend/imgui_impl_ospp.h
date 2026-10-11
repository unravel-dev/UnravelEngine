#pragma once
#include <engine/rendering/render_window.h>
#include <imgui/imgui.h> // IMGUI_IMPL_API

using ImGui_ImplOSPP_RenderWindow_Callback =
    std::function<void(unravel::render_window* window, ImGuiViewport* viewport, void*)>;
using ImGui_ImplOSPP_SwapBuffers_Callback =
    std::function<void(unravel::render_window* window, ImGuiViewport* viewport, void*)>;

IMGUI_IMPL_API auto ImGui_ImplOSPP_Init(unravel::render_window* window,
                                        ImGui_ImplOSPP_RenderWindow_Callback render_callback,
                                        ImGui_ImplOSPP_SwapBuffers_Callback swap_callback) -> bool;

IMGUI_IMPL_API void ImGui_ImplOSPP_Shutdown();
IMGUI_IMPL_API void ImGui_ImplOSPP_NewFrame(float delta_time);
IMGUI_IMPL_API void ImGui_ImplOSPP_EndFrame();

IMGUI_IMPL_API auto ImGui_ImplOSPP_ProcessEvent(os::event& event) -> bool;

/// Supplies a pointer position in screen space that stands in for the OS cursor while it returns
/// true: real motion is ignored and the position is fed every frame. The editor points it at the
/// game input's virtual pointer, so the editor UI and the game see one pointer during synthesized
/// input - otherwise a synthesized press lands wherever the real cursor rests.
using ImGui_ImplOSPP_MouseOverride_Callback = std::function<bool(os::point& screen_position)>;
IMGUI_IMPL_API void ImGui_ImplOSPP_SetMouseOverride(ImGui_ImplOSPP_MouseOverride_Callback callback);
IMGUI_IMPL_API auto ImGui_ImplOSPP_KeycodeToImGuiKey(os::key::code keycode) -> ImGuiKey;
IMGUI_IMPL_API auto ImGui_ImplOSPP_ImGuiKeyToKeycode(ImGuiKey key) -> os::key::code;
