#include "version_manager.h"
#include "imgui/imgui.h"
#include "threadpp/future.hpp"
#include <context/context.hpp>
#include <editor/imgui/integration/imgui_notify.h>

namespace unravel
{
auto version_manager::init(rtti::context& ctx) -> bool
{
    tpp::async([]()
    {
        //TODO check for new version from github releases
        return 12;
        
    }).then(tpp::this_thread::get_id(), [](auto result)
    {
        ImGuiToast toast(ImGuiToastType_Warning, 99999);
        toast.set_title("New version available.");
        toast.set_show_dismiss_button(true);
        toast.set_draw_callback([](const ImGuiToast& toast, float opacity, const ImVec4& text_color)
        {
            ImGui::Text("Download from ");
            ImGui::SameLine();
            ImGui::TextLinkOpenURL("Releases.", "https://github.com/unravel-dev/UnravelEngine/releases");
        });
        ImGui::PushNotification(toast);

    });
    return true;
}

auto version_manager::deinit(rtti::context& ctx) -> bool
{
    return true;
}
} // namespace unravel
