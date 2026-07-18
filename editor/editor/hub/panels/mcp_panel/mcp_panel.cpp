#include "mcp_panel.h"

#include "../panels_defs.h"

#include <editor/system/mcp_manager.h>

#include <imgui/imgui.h>
#include <imgui_widgets/tooltips.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace unravel
{
namespace
{
auto format_timestamp(std::chrono::system_clock::time_point tp) -> std::string
{
    const auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%H:%M:%S");
    return oss.str();
}
} // namespace

mcp_panel::mcp_panel(imgui_panels* parent) : parent_(parent)
{
}

void mcp_panel::init(rtti::context& ctx)
{
}

void mcp_panel::deinit(rtti::context& ctx)
{
}

void mcp_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        show_request_ = false;
        show_ = true;
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.45f, ImGuiCond_Once);
    }

    if(!show_)
    {
        return;
    }

    if(ImGui::Begin(name, &show_))
    {
        draw_ui(ctx);
    }
    ImGui::End();
}

void mcp_panel::show(bool s)
{
    show_request_ = s;
}

void mcp_panel::draw_ui(rtti::context& ctx)
{
    auto& mcp = ctx.get_cached<mcp_manager>();
    const bool running = mcp.is_running();
    const auto endpoint = mcp.get_endpoint_url();
    const auto health = mcp.get_health_url();

    ImGui::TextUnformatted("Status");
    ImGui::SameLine();
    if(running)
    {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), ICON_MDI_LAN_CONNECT " Listening");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.35f, 1.0f), ICON_MDI_LAN_DISCONNECT " Stopped");
    }

    ImGui::Separator();

    ImGui::BeginTable("##mcp_info", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings);
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Endpoint");
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(endpoint.c_str());
    ImGui::SameLine();
    if(ImGui::SmallButton(ICON_MDI_CONTENT_COPY "##copy_endpoint"))
    {
        ImGui::SetClipboardText(endpoint.c_str());
    }
    ImGui::SetItemTooltipEx("Copy MCP endpoint URL");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Health");
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(health.c_str());

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Bind");
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%s:%d (localhost only)", mcp.get_host(), mcp.get_port());

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Tools");
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%zu registered", mcp.get_tool_count());

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Requests");
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%llu  |  tool calls: %llu  |  errors: %llu",
                static_cast<unsigned long long>(mcp.get_request_count()),
                static_cast<unsigned long long>(mcp.get_tool_call_count()),
                static_cast<unsigned long long>(mcp.get_error_count()));

    ImGui::EndTable();

    ImGui::Spacing();
    if(running)
    {
        if(ImGui::Button(ICON_MDI_STOP " Stop"))
        {
            mcp.stop();
        }
    }
    else
    {
        if(ImGui::Button(ICON_MDI_PLAY " Start"))
        {
            mcp.start();
        }
    }
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_DELETE " Clear Log"))
    {
        mcp.clear_activity();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &auto_scroll_);

    ImGui::Separator();
    ImGui::TextUnformatted("Activity");

    const auto activity = mcp.snapshot_activity();
    ImGui::BeginChild("##mcp_activity", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

    if(activity.empty())
    {
        ImGui::TextDisabled("No MCP activity yet. Connect a client to %s", endpoint.c_str());
    }
    else
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(activity.size()));
        while(clipper.Step())
        {
            for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& entry = activity[static_cast<size_t>(i)];
                ImGui::PushID(i);

                const ImVec4 color = entry.is_error ? ImVec4(0.95f, 0.45f, 0.4f, 1.0f)
                                                   : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.6f, 1.0f), "%s", format_timestamp(entry.timestamp).c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.45f, 0.7f, 0.95f, 1.0f), "[%s]", entry.category.c_str());
                ImGui::SameLine();
                ImGui::TextColored(color, "%s", entry.message.c_str());

                ImGui::PopID();
            }
        }

        if(auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        {
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();
}

} // namespace unravel
