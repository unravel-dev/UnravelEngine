#include "imgui_interface.h"
#include "imgui_widgets/utils.h"
#include "integration/imgui_style.h"
#include <editor/events.h>

#include <engine/events.h>
#include <engine/rendering/renderer.h>

#include <logging/logging.h>

namespace unravel
{

imgui_interface::imgui_interface(rtti::context& ctx)
{
    auto& ev = ctx.get_cached<events>();

    ev.on_os_event.connect(sentinel_, 1000, this, &imgui_interface::on_os_event);
    ev.on_frame_render.connect(sentinel_, -100000, this, &imgui_interface::on_frame_ui_render);
}

imgui_interface::~imgui_interface()
{
    if(inited_)
    {
        imguiDestroy();
    }
}

auto imgui_interface::init_basic(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    const auto& rend = ctx.get_cached<renderer>();
    const auto& main_window = rend.get_main_window();
    imguiCreate(main_window, 14.0f);

    imgui_style::set_unity_theme();

    inited_ = true;
    return true;
}

auto imgui_interface::init_finalize(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    imguiCreateCubemapProgram();
    return true;
}

auto imgui_interface::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void imgui_interface::render_loading_frame(rtti::context& ctx,
                                           const std::string& stage,
                                           size_t completed,
                                           size_t total,
                                           const std::string& current_job)
{
    const auto& rend = ctx.get_cached<renderer>();
    auto window = rend.get_main_window();
    if(!window)
    {
        return;
    }

    os::event e{};
    while(os::poll_event(e))
    {
        imguiProcessEvent(e);
    }

    auto& present_pass = window->begin_present_pass();
    present_pass.clear();

    for(int i = 0; i < 2; ++i)
    {
        imguiBeginFrame(1.0f / 60.0f);
        draw_loading_overlay(stage, completed, total, current_job);
    
        auto& main_surface = window->get_surface();
        gfx::render_pass pass("loading_imgui_pass");
        pass.bind(main_surface.get());
        imguiEndFrame(pass.id);
    
        gfx::render_pass end_pass(gfx::render_pass::get_max_pass_id(), "loading_backbuffer");
        end_pass.bind();
        gfx::frame();
    }

}

void imgui_interface::draw_loading_overlay(const std::string& stage,
                                           size_t completed,
                                           size_t total,
                                           const std::string& current_job)
{
    const auto* viewport = ImGui::GetMainViewport();

    // Full-screen dimmed backdrop
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));

    ImGuiWindowFlags backdrop_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##loading_backdrop", nullptr, backdrop_flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Centered card
    constexpr float card_width = 480.0f;
    float card_x = (viewport->WorkSize.x - card_width) * 0.5f;
    float card_y = viewport->WorkSize.y * 0.38f;
    ImGui::SetCursorPos(ImVec2(card_x, card_y));

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f, 28.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));

    if(ImGui::BeginChild("##loading_card", ImVec2(card_width, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        // Title
        ImGui::PushFont(ImGui::Font::Bold);
        auto title_text = "Unravel Engine";
        float title_width = ImGui::CalcTextSize(title_text).x;

        ImGui::AlignedItem(0.5f, ImGui::GetContentRegionAvail().x, title_width, [&]() 
        {
            ImGui::TextUnformatted(title_text);
        });
      
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::Spacing();

        // Separator
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();

        // Stage name
        ImGui::PushFont(ImGui::Font::Medium);
        ImGui::TextUnformatted(stage.c_str());

        auto spinner_size = ImGui::GetTextLineHeight();

        ImGui::SameLine();
        ImGui::AlignedItem(1.0f, ImGui::GetContentRegionAvail().x, spinner_size, [&]() {
        ImSpinner::Spinner<ImSpinner::SpinnerTypeT::e_st_eclipse>("spinner", 
                ImSpinner::Radius{spinner_size * 0.5f},
                ImSpinner::Thickness{4.0f},
                ImSpinner::Color{ImSpinner::white},
                ImSpinner::Speed{6.0f});
        });
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::Spacing();

        // Custom progress bar
        float fraction = (total > 0) ? static_cast<float>(completed) / static_cast<float>(total) : 0.0f;
        constexpr float bar_height = 6.0f;
        float bar_width = ImGui::GetContentRegionAvail().x;
        ImVec2 bar_pos = ImGui::GetCursorScreenPos();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Background track
        draw_list->AddRectFilled(bar_pos,
                                 ImVec2(bar_pos.x + bar_width, bar_pos.y + bar_height),
                                 IM_COL32(30, 30, 30, 255),
                                 bar_height * 0.5f);

        // Filled portion with accent color
        if(fraction > 0.0f)
        {
            float fill_width = bar_width * fraction;
            draw_list->AddRectFilled(bar_pos,
                                     ImVec2(bar_pos.x + fill_width, bar_pos.y + bar_height),
                                     IM_COL32(58, 121, 187, 255),
                                     bar_height * 0.5f);
        }

        ImGui::Dummy(ImVec2(bar_width, bar_height));

        ImGui::Spacing();

        
        // Current job name
        if(!current_job.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
            ImGui::TextWrapped("%s", current_job.c_str());
            ImGui::PopStyleColor();
        }

        if(total > 0)
        {
            ImGui::SameLine();
            // Progress count -- right-aligned
            auto progress_text = fmt::format("{} / {}", completed, total);
            float count_width = ImGui::CalcTextSize(progress_text.c_str()).x;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
            ImGui::AlignedItem(1.0f, ImGui::GetContentRegionAvail().x, count_width, [&]() {
                ImGui::TextUnformatted(progress_text.c_str());
            });
            ImGui::PopStyleColor();
        }

    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::End();
}

void imgui_interface::on_os_event(rtti::context& ctx, os::event& e)
{
    imguiProcessEvent(e);
}

void imgui_interface::on_frame_ui_render(rtti::context& ctx, delta_t dt)
{
    const auto& ev = ctx.get_cached<ui_events>();

    const auto& rend = ctx.get_cached<renderer>();
    const auto& main_window = rend.get_main_window();
    const auto& main_surface = main_window->get_surface();

    imguiBeginFrame(dt.count());

    ev.on_frame_ui_render(ctx, dt);

    gfx::render_pass pass("imgui_pass");
    pass.bind(main_surface.get());
    imguiEndFrame(pass.id);
}

} // namespace unravel
